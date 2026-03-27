#include "httplib.h"
#include <bits/stdc++.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <mysql/mysql.h>

using namespace std;
using namespace httplib;
void storeDeletedLog(const string &line);

vector<string> liveCache;

/* ================= Rope (Treap-Based) ================= */

struct RopeNode {
    RopeNode *left, *right;
    string data;
    int priority;
    int size;

    RopeNode(const string& s)
        : left(nullptr), right(nullptr),
          data(s), priority(rand()), size(1) {}

    void recalc() {
        size = 1;
        if (left) size += left->size;
        if (right) size += right->size;
    }
};

using Node = RopeNode*;

Node rope = nullptr;
mutex ropeMutex;

const int MAX_LOGS = 50000;
atomic<bool> liveRunning(false);

/* ================= Treap Merge ================= */

Node merge(Node a, Node b) {
    if (!a) return b;
    if (!b) return a;

    if (a->priority > b->priority) {
        a->right = merge(a->right, b);
        a->recalc();
        return a;
    } else {
        b->left = merge(a, b->left);
        b->recalc();
        return b;
    }
}

/* ================= Collect ================= */

void collect(Node root, vector<string>& out) {
    if (!root) return;
    collect(root->left, out);
    out.push_back(root->data);
    collect(root->right, out);
}

/* ================= Add Log ================= */

void addLog(const string& line) {

    rope = merge(rope, new RopeNode(line));

    liveCache.push_back(line);

    if (liveCache.size() > MAX_LOGS)
        liveCache.erase(liveCache.begin());

    // Keep rope trimmed
    if (rope && rope->size > MAX_LOGS) {

        vector<string> all;
        collect(rope, all);

        Node newRope = nullptr;
        for (size_t i = all.size() - MAX_LOGS; i < all.size(); i++)
            newRope = merge(newRope, new RopeNode(all[i]));

        rope = newRope;
    }
}

/* ================= Load Historical Logs ================= */

void loadOldLogs() {

    // Load from last 7 days (avoids VM boot spam)
    string cmd =
        "journalctl --since \"7 days ago\" "
        "-o short-iso --no-pager";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    char buffer[4096];

    while (fgets(buffer, sizeof(buffer), pipe)) {
    
        lock_guard<mutex> lock(ropeMutex);
    
        if (rope && rope->size >= MAX_LOGS)
            break;
    
        string line(buffer);
    
        if (line.find("Access denied for user") != string::npos) continue;
        if (line.find("mariadbd") != string::npos) continue;
    
        addLog(line);
    }

    pclose(pipe);
}

/* ================= Live Monitor ================= */

void startLiveMonitor() {

    liveRunning = true;

    thread([](){

        // Exec monitor (command execution)
        const char* execCmd =
            "stdbuf -oL bpftrace -e "
            "'tracepoint:syscalls:sys_enter_execve "
            "{ printf(\"[EXEC] PID=%d CMD=%s\\n\", pid, str(args->filename)); }'";

        // Real Linux logs (with logger message content)
        const char* journalCmd =
            "stdbuf -oL journalctl -f "
            "-o short-iso --no-pager";

        FILE* execPipe = popen(execCmd, "r");
        FILE* journalPipe = popen(journalCmd, "r");

        char buffer[4096];

        while (liveRunning) {

            if (execPipe && fgets(buffer, sizeof(buffer), execPipe)) {
                lock_guard<mutex> lock(ropeMutex);
                addLog(string(buffer));
            }

            if (journalPipe && fgets(buffer, sizeof(buffer), journalPipe)) {
            
                string line(buffer);
                
                // Skip VM boot noise
                if (line.find("Detected virtualization") != string::npos) continue;
                if (line.find("Guest personality") != string::npos) continue;
                if (line.find("bpf-restrict-fs") != string::npos) continue;
                
                // Skip MariaDB spam
                if (line.find("Access denied for user") != string::npos) continue;
                if (line.find("mariadbd") != string::npos) continue;
                
                lock_guard<mutex> lock(ropeMutex);
                addLog(line);
            }
        }

        if (execPipe) pclose(execPipe);
        if (journalPipe) pclose(journalPipe);

    }).detach();
}

string hashLog(const string &log)
{
    hash<string> hasher;
    return to_string(hasher(log));
}

void storeDeletedLog(const string &line)
{
    MYSQL *conn = mysql_init(nullptr);

    if (!mysql_real_connect(
            conn,
            "localhost",
            "loguser",
            "logpass",
            "log_audit",
            0,
            nullptr,
            0))
    {
        cout << "DB connection failed: "
             << mysql_error(conn) << endl;
        return;
    }

    string logTime = "";

    if (line.size() >= 19)
        logTime = line.substr(0,19);

    if (logTime.size() > 10)
        logTime[10] = ' ';

    string logHash = hashLog(line);

    char escaped[16384];

    mysql_real_escape_string(
        conn,
        escaped,
        line.c_str(),
        line.length());

    string query =
        "INSERT IGNORE INTO deleted_logs(system_id, log_timestamp, log_hash, log_content) VALUES('KALI-VM-01','"
        + logTime + "','"
        + logHash + "','"
        + string(escaped) + "')";

    if (mysql_query(conn, query.c_str()))
    {
        cout << "Failed to store deleted log: "
             << mysql_error(conn) << endl;
    }

    mysql_close(conn);
}

void deleteByTimeRange(string start, string end)
{
    lock_guard<mutex> lock(ropeMutex);

    if (!rope)
        return;

    vector<string> all;
    collect(rope, all);

    Node newRope = nullptr;

    for (auto &line : all)
    {
        if (line.size() < 19)
            continue;

        string logTime = line.substr(0,19);
        logTime[10] = ' ';

        if (logTime >= start && logTime <= end)
        {
            storeDeletedLog(line);
        }
        else
        {
            Node leaf = new RopeNode(line);
            newRope = merge(newRope, leaf);
        }
    }

    rope = newRope;
}

/* ================= MAIN ================= */

int main() {

    srand(time(nullptr));

    loadOldLogs();
    startLiveMonitor();

    Server svr;

    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Headers", "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"}
    });

    svr.set_mount_point("/", "./");

    svr.Get("/", [](const Request&, Response& res) {
        res.set_redirect("/index.html");
    });

    /* ===== Last N ===== */
    svr.Get("/api/last", [](const Request& req, Response& res) {

        int n = 100;
        if (req.has_param("n"))
            n = stoi(req.get_param_value("n"));

        vector<string> all;
        {
            lock_guard<mutex> lock(ropeMutex);
            collect(rope, all);
        }

        if (n > (int)all.size()) n = all.size();

        string output;
        for (int i = all.size() - n; i < (int)all.size(); i++)
            output += all[i];

        res.set_content(output, "text/plain");
    });

    /* ===== Date Filter ===== */
    svr.Get("/api/date", [](const Request& req, Response& res) {

        if (!req.has_param("d")) {
            res.set_content("Missing date parameter", "text/plain");
            return;
        }

        string date = req.get_param_value("d");

        vector<string> all;
        {
            lock_guard<mutex> lock(ropeMutex);
            collect(rope, all);
        }

        string output;

        for (auto &line : all) {
            if (line.size() >= 10 && line.substr(0, 10) == date)
                output += line;
        }

        res.set_content(output, "text/plain");
    });

    /* ===== SSE Live ===== */
    svr.Get("/api/live", [](const Request&, Response& res) {
    
        vector<string> snapshot;
    
        {
            lock_guard<mutex> lock(ropeMutex);
            collect(rope, snapshot);
        }
    
        size_t total = snapshot.size();
        size_t start = 0;
    
        // Send only last 100 logs for live view
        if (total > 100)
            start = total - 100;
    
        string output;
    
        for (size_t i = start; i < total; i++) {
            output += "data: " + snapshot[i] + "\n\n";
        }
    
        res.set_content(output, "text/event-stream");
    });

    /* ===== DATABASE FETCH ===== */
    svr.Get("/api/db", [](const Request& req, Response& res) {
    
        MYSQL *conn = mysql_init(nullptr);
    
        if (!mysql_real_connect(conn,"localhost","loguser","logpass","log_audit",0,nullptr,0))
        {
            res.set_content(mysql_error(conn), "text/plain");
            return;
        }
    
        string query =
        "SELECT system_id, log_timestamp, log_hash, log_content FROM deleted_logs WHERE 1=1 ";
    
        if (req.has_param("sys"))
            query += "AND system_id='" + req.get_param_value("sys") + "' ";
    
        if (req.has_param("start"))
            query += "AND log_timestamp >= '" + req.get_param_value("start") + "' ";
    
        if (req.has_param("end"))
            query += "AND log_timestamp <= '" + req.get_param_value("end") + "' ";
    
        if (req.has_param("pid"))
            query += "AND log_content LIKE '%PID=" + req.get_param_value("pid") + "%' ";
    
        query += "ORDER BY log_timestamp DESC";
    
        if (mysql_query(conn, query.c_str()))
        {
            res.set_content(mysql_error(conn), "text/plain");
            mysql_close(conn);
            return;
        }
    
        MYSQL_RES *result = mysql_store_result(conn);
        MYSQL_ROW row;
    
        string output;
    
        while ((row = mysql_fetch_row(result)))
        {
            string sysid = row[0] ? row[0] : "";
            string time  = row[1] ? row[1] : "";
            string log   = row[3] ? row[3] : "";
    
            output += "[DB] ";
            output += time + " ";
            output += "SYS=" + sysid + " ";
            output += log + "\n";
        }
    
        mysql_free_result(result);
        mysql_close(conn);
    
        res.set_content(output, "text/plain");
    });

    svr.Get("/api/delete", [](const Request& req, Response& res)
    {
        if (!req.has_param("start") || !req.has_param("end"))
        {
            res.set_content("Missing parameters", "text/plain");
            return;
        }
    
        string start = req.get_param_value("start");
        string end   = req.get_param_value("end");
    
        deleteByTimeRange(start, end);
    
        res.set_content(
            "Logs deleted between " + start + " and " + end,
            "text/plain"
        );
    });

    cout << "Server running at http://0.0.0.0:8080\n";

    svr.listen("0.0.0.0", 8080);
}
