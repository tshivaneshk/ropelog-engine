#include <bits/stdc++.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <mysql/mysql.h>
#include <ctime>

using namespace std;

unordered_set<string> deletedHashes;

/* ================= Rope Structure ================= */

struct RopeNode
{
    RopeNode *left, *right;
    string data;
    int priority;
    int logCount;

    RopeNode(const string &s)
        : left(nullptr), right(nullptr),
          data(s), priority(rand()), logCount(1) {}

    RopeNode(RopeNode *l, RopeNode *r)
        : left(l), right(r), data(""),
          priority(rand())
    {
        recalc();
    }

    void recalc()
    {
        logCount = 0;
        if (left)
            logCount += left->logCount;
        if (right)
            logCount += right->logCount;
    }
};

using Node = RopeNode *;

Node rope = nullptr;
mutex ropeMutex;

const int MAX_LOGS = 50000;
atomic<bool> liveRunning(false);

string SYSTEM_ID = "KALI-VM-01";

bool isDeleted(const string &line);
void storeDeletedLog(const string &line);

string hashLog(const string &log)
{
    hash<string> hasher;
    return to_string(hasher(log));
}

/* ================= MySQL ================= */

MYSQL *connectDB()
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
        cout << "MySQL connection failed: "
             << mysql_error(conn) << endl;
        return nullptr;
    }

    return conn;
}

/* ================= Rope Operations ================= */

pair<Node, Node> split(Node root, int k)
{
    if (!root)
        return {nullptr, nullptr};

    if (!root->left && !root->right)
    {
        if (k <= 0)
            return {nullptr, root};
        return {root, nullptr};
    }

    int leftCount = root->left ? root->left->logCount : 0;

    if (k < leftCount)
    {
        auto parts = split(root->left, k);
        root->left = parts.second;
        root->recalc();
        return {parts.first, root};
    }
    else
    {
        auto parts = split(root->right, k - leftCount);
        root->right = parts.first;
        root->recalc();
        return {root, parts.second};
    }
}

Node merge(Node a, Node b)
{
    if (!a)
        return b;
    if (!b)
        return a;

    if (a->priority > b->priority)
    {
        a->right = merge(a->right, b);
        a->recalc();
        return a;
    }
    else
    {
        b->left = merge(a, b->left);
        b->recalc();
        return b;
    }
}

void collect(Node root, string &out)
{
    if (!root)
        return;

    if (!root->left && !root->right)
    {
        out += root->data;
        return;
    }

    collect(root->left, out);
    collect(root->right, out);
}

void trimToMax()
{
    if (!rope || rope->logCount <= MAX_LOGS)
        return;

    int drop = rope->logCount - MAX_LOGS;
    rope = split(rope, drop).second;
}

void addLog(const string &line)
{
    Node leaf = new RopeNode(line);
    rope = merge(rope, leaf);
    trimToMax();
}

/* ================= Load Old Logs ================= */

void loadOldLogs(int n)
{
    string cmd = "journalctl -n " + to_string(n) + " -o short-iso";
    FILE *pipe = popen(cmd.c_str(), "r");

    if (!pipe)
    {
        cout << "Failed to load logs.\n";
        return;
    }

    char buffer[4096];
    int count = 0;

    while (fgets(buffer, sizeof(buffer), pipe))
    {
        string line(buffer);
        cout << line;

        lock_guard<mutex> lock(ropeMutex);

        if (!isDeleted(line))
        {
            addLog(line);
        }

        count++;
    }

    pclose(pipe);

    cout << "\nLoaded " << count << " logs.\n";
}

/* ================= Live Monitor ================= */

void liveMode()
{
    liveRunning = true;

    const char *execCmd =
        "stdbuf -oL bpftrace -e "
        "'tracepoint:syscalls:sys_enter_execve "
        "/uid == 1000/ "
        "{ printf(\"[EXEC] PID=%d CMD=%s\\n\", pid, str(args->filename)); }'";

    const char *journalCmd =
        "stdbuf -oL journalctl -f -n 0 -o short-iso";

    int execPipe[2], journalPipe[2];

    if (pipe(execPipe) == -1 || pipe(journalPipe) == -1)
    {
        perror("pipe");
        return;
    }

    pid_t execPid = fork();

    if (execPid == 0)
    {
        setpgid(0, 0);
        dup2(execPipe[1], STDOUT_FILENO);

        close(execPipe[0]);
        close(execPipe[1]);

        execl("/bin/sh", "sh", "-c", execCmd, nullptr);
        exit(1);
    }

    pid_t journalPid = fork();

    if (journalPid == 0)
    {
        setpgid(0, 0);
        dup2(journalPipe[1], STDOUT_FILENO);

        close(journalPipe[0]);
        close(journalPipe[1]);

        execl("/bin/sh", "sh", "-c", journalCmd, nullptr);
        exit(1);
    }

    close(execPipe[1]);
    close(journalPipe[1]);

    cout << "\n--- LIVE SYSTEM + EXEC MONITOR (type q + Enter to stop) ---\n";

    thread stopThread([&]()
                      {
        string input;

        while (getline(cin, input))
        {
            if (input == "q")
            {
                liveRunning = false;
                kill(-execPid, SIGINT);
                kill(-journalPid, SIGINT);
                break;
            }
        } });

    auto reader = [&](int fd, bool filterNoise)
    {
        char buffer[4096];

        while (liveRunning)
        {
            ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);

            if (bytes <= 0)
                break;

            buffer[bytes] = '\0';

            string line(buffer);

            if (filterNoise)
            {
                if (line.find("sudo[") != string::npos)
                    continue;

                if (line.find("CRON[") != string::npos)
                    continue;

                if (line.find("systemd") != string::npos)
                    continue;
            }

            {
                lock_guard<mutex> lock(ropeMutex);

                if (!isDeleted(line))
                    addLog(line);
            }

            cout << line;
            cout.flush();
        }
    };

    thread execThread(reader, execPipe[0], false);
    thread journalThread(reader, journalPipe[0], true);

    execThread.join();
    journalThread.join();

    waitpid(execPid, nullptr, 0);
    waitpid(journalPid, nullptr, 0);

    close(execPipe[0]);
    close(journalPipe[0]);

    if (stopThread.joinable())
        stopThread.join();

    cout << "\n--- LIVE STOPPED ---\n";
}

/* ================= View Logs ================= */

void viewLastN(int n)
{
    lock_guard<mutex> lock(ropeMutex);

    if (!rope)
    {
        cout << "[No logs]\n";
        return;
    }

    int skip = max(0, rope->logCount - n);
    Node last = split(rope, skip).second;

    string out;
    collect(last, out);

    cout << out;
}

/* ================= Date Filter ================= */

void collectByDate(Node root, const string &date)
{
    if (!root)
        return;

    if (!root->left && !root->right)
    {
        string line = root->data;

        if (line.length() >= 10)
        {
            string logDate = line.substr(0, 10);

            if (logDate == date)
                cout << line;
        }

        return;
    }

    collectByDate(root->left, date);
    collectByDate(root->right, date);
}

void filterByDate(const string &date)
{
    lock_guard<mutex> lock(ropeMutex);

    if (!rope)
    {
        cout << "[No logs stored]\n";
        return;
    }

    cout << "\n--- LOGS FOR DATE: " << date << " ---\n";

    collectByDate(rope, date);

    cout << "--- END ---\n";
}

/* ================= Deleted Hash Loader ================= */

void loadDeletedHashes()
{
    MYSQL *conn = connectDB();
    if (!conn)
        return;

    string query = "SELECT log_hash FROM deleted_logs";

    if (mysql_query(conn, query.c_str()))
    {
        mysql_close(conn);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(res)))
    {
        deletedHashes.insert(row[0]);
    }

    mysql_free_result(res);
    mysql_close(conn);
}

/* ================= Store Deleted Log (FIXED) ================= */

void storeDeletedLog(const string &line)
{
    MYSQL *conn = connectDB();
    if (!conn)
        return;

    string logTime = "";

    if (line.size() >= 19)
        logTime = line.substr(0, 19);

    if (logTime.size() > 10)
        logTime[10] = ' ';

    string logHash = hashLog(line);

    deletedHashes.insert(logHash);

    char escaped[16384];

    mysql_real_escape_string(
        conn,
        escaped,
        line.c_str(),
        line.length());

    string query =
        "INSERT IGNORE INTO deleted_logs(system_id, log_timestamp, log_hash, log_content) VALUES('"
        + SYSTEM_ID + "','"
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

bool isDeleted(const string &line)
{
    string logHash = hashLog(line);
    return deletedHashes.count(logHash) > 0;
}

/* ================= Delete by Time Range ================= */

void deleteByTimeRange(string start, string end)
{
    lock_guard<mutex> lock(ropeMutex);

    if (!rope)
    {
        cout << "No logs stored\n";
        return;
    }

    string all;
    collect(rope, all);

    stringstream ss(all);
    string line;

    Node newRope = nullptr;

    while (getline(ss, line))
    {
        if (line.size() < 19)
            continue;

        string logTime = line.substr(0, 19);
        logTime[10] = ' ';

        if (logTime >= start && logTime <= end)
        {
            storeDeletedLog(line);
        }
        else
        {
            Node leaf = new RopeNode(line + "\n");
            newRope = merge(newRope, leaf);
        }
    }

    rope = newRope;

    cout << "Logs deleted between "
         << start << " and " << end << endl;
}

/* ================= Main ================= */

int main()
{
    srand(time(nullptr));

    loadDeletedHashes();

    while (true)
    {
        cout << "\n===== ROPE KERNEL MONITOR =====\n"
             << "1. Load old system logs\n"
             << "2. Start live exec monitor (eBPF)\n"
             << "3. View last N stored logs\n"
             << "4. Filter logs by date\n"
             << "5. Delete logs by time range (stored in DB)\n"
             << "6. Exit\n"
             << "Choice: " << flush;

        int choice;

        if (!(cin >> choice))
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');

            cout << "Invalid choice.\n";
            continue;
        }

        if (choice == 1)
        {
            int n;

            cout << "How many logs? ";
            cin >> n;

            loadOldLogs(n);
        }
        else if (choice == 2)
        {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            liveMode();
        }
        else if (choice == 3)
        {
            int n;

            cout << "Enter N: ";
            cin >> n;

            viewLastN(n);
        }
        else if (choice == 4)
        {
            string date;

            cin.ignore(numeric_limits<streamsize>::max(), '\n');

            cout << "Enter date (YYYY-MM-DD): ";
            getline(cin, date);

            filterByDate(date);
        }
        else if (choice == 5)
        {
            string start, end;

            cin.ignore();

            cout << "Start Time (YYYY-MM-DD HH:MM:SS): ";
            getline(cin, start);

            cout << "End Time (YYYY-MM-DD HH:MM:SS): ";
            getline(cin, end);

            deleteByTimeRange(start, end);
        }
        else if (choice == 6)
        {
            break;
        }
        else
        {
            cout << "Invalid choice.\n";
        }
    }

    return 0;
}
