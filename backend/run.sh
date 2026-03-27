#!/bin/bash

echo "Starting MariaDB..."
sudo systemctl start mariadb

echo "Compiling server..."
g++ server.cpp -o rope_server -lmysqlclient -pthread

echo "Launching Rope Server..."
sudo ./rope_server
