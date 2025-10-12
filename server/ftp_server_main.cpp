#include "ftp_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

int main(int argc, char *argv[]) {
    int port = 2121;
    if (argc >= 2) port = stoi(argv[1]);

    ensureDir(SERVER_ROOT);
    ensureDir(BASE_DIR);
    // ensure users file exists
    {
        lock_guard<mutex> lock(usersMutex);
        if (!fs::exists(USERS_FILE)) {
            ofstream f(USERS_FILE);
            f.close();
        }
    }

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        cerr << "Socket create failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr *)&addr, sizeof(addr)) < 0) {
        cerr << "Bind failed\n";
        close(serverSock);
        return 1;
    }
    if (listen(serverSock, 10) < 0) {
        cerr << "Listen failed\n";
        close(serverSock);
        return 1;
    }

    cout << "[LOG] FTP server started on port " << port << endl;

    while (true) {
        int clientSock = accept(serverSock, nullptr, nullptr);
        if (clientSock < 0) {
            cerr << "Accept failed\n";
            continue;
        }
        cout << "[LOG] Client connected\n";
        thread t(handleClient, clientSock);
        t.detach();
    }

    close(serverSock);
    return 0;
}