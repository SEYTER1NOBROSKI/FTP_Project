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

const string SERVER_ROOT = "server/";
const string USERS_FILE = SERVER_ROOT + "users.txt";
const string BASE_DIR = SERVER_ROOT + "users/"; // users/<username>/

mutex usersMutex;

// helper: ensure directory exists
void ensureDir(const string &p) {
    if (!fs::exists(p)) fs::create_directories(p);
}

// reliable send_all
bool send_all(int sock, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t s = send(sock, data + sent, len - sent, 0);
        if (s <= 0) return false;
        sent += s;
    }
    return true;
}

// recv one line (until '\n'); returns empty string on error/close
string recv_line(int sock) {
    string line;
    char c;
    while (true) {
        ssize_t r = recv(sock, &c, 1, 0);
        if (r <= 0) return string(); // closed or error
        if (c == '\n') break;
        line.push_back(c);
    }
    return line;
}

// recv exactly n bytes into buffer; returns number of bytes actually read (should be n) or <n on error/close
ssize_t recv_exact(int sock, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(sock, buf + got, n - got, 0);
        if (r <= 0) return r; // error or closed
        got += r;
    }
    return (ssize_t)got;
}

// send "OK\n<size>\n" then send data from file
void sendFileToClient(int clientSock, const string &filepath) {
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        string err = "ERROR: File not found\n";
        send_all(clientSock, err.c_str(), err.size());
        return;
    }

    uintmax_t fsize = fs::file_size(filepath);
    // send header
    string header = "OK\n" + to_string((unsigned long long)fsize) + "\n";
    if (!send_all(clientSock, header.c_str(), header.size())) return;

    // send file bytes
    ifstream in(filepath, ios::binary);
    if (!in.is_open()) return;
    char buffer[BUFFER_SIZE];
    while (in) {
        in.read(buffer, sizeof(buffer));
        streamsize readBytes = in.gcount();
        if (readBytes > 0) {
            if (!send_all(clientSock, buffer, (size_t)readBytes)) break;
        }
    }
    in.close();
}

// helper to send a text message as a size-prefixed block (used for LIST, HELP, LISTALL)
void sendTextBlock(int clientSock, const string &text) {
    string header = "OK\n" + to_string((unsigned long long)text.size()) + "\n";
    if (!send_all(clientSock, header.c_str(), header.size())) return;
    send_all(clientSock, text.c_str(), text.size());
}

// users file operations
bool registerUser(const string &username, const string &password) {
    lock_guard<mutex> lock(usersMutex);
    // read existing
    ifstream in(USERS_FILE);
    string u, p;
    while (in >> u >> p) {
        if (u == username) return false;
    }
    in.close();
    ofstream out(USERS_FILE, ios::app);
    out << username << " " << password << "\n";
    out.close();

    ensureDir(BASE_DIR + username);
    cout << "[LOG] Registered user and created dir: " << BASE_DIR + username << endl;
    return true;
}

bool checkUser(const string &username, const string &password) {
    lock_guard<mutex> lock(usersMutex);
    ifstream in(USERS_FILE);
    string u, p;
    while (in >> u >> p) {
        if (u == username && p == password) return true;
    }
    return false;
}

void handleClient(int clientSock) {
    string username;
    bool authenticated = false;

    while (true) {
        string line = recv_line(clientSock);
        if (line.empty()) break; // connection closed or error

        // parse command
        istringstream iss(line);
        string cmd;
        iss >> cmd;

        if (cmd == "REGISTER") {
            string u, p;
            iss >> u >> p;
            if (u.empty() || p.empty()) {
                string msg = "ERROR: Wrong REGISTER format\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            if (registerUser(u, p)) {
                string msg = "REGISTERED\n";
                send_all(clientSock, msg.c_str(), msg.size());
            } else {
                string msg = "ERROR: User already exists\n";
                send_all(clientSock, msg.c_str(), msg.size());
            }
        } else if (cmd == "LOGIN") {
            string u, p;
            iss >> u >> p;
            if (u.empty() || p.empty()) {
                string msg = "ERROR: Wrong LOGIN format\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            if (checkUser(u, p)) {
                username = u;
                authenticated = true;
                string msg = "LOGGED IN\n";
                send_all(clientSock, msg.c_str(), msg.size());
                cout << "[LOG] User logged in: " << username << endl;
            } else {
                string msg = "ERROR: Invalid credentials\n";
                send_all(clientSock, msg.c_str(), msg.size());
            }
        } else if (cmd == "HELP") {
            string helpTxt =
                "Available commands:\n"
                "REGISTER <username> <password>\n"
                "LOGIN <username> <password>\n"
                "PUT <filename>\n"
                "GET <filename>\n"
                "LIST\n"
                "LISTALL\n"
                "GETALL <username>/<filename>\n"
                "HELP\n"
                "EXIT\n";
            sendTextBlock(clientSock, helpTxt);
        } else if (cmd == "LIST") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string list;
            string dir = BASE_DIR + username;
            if (fs::exists(dir)) {
                list = "Files:\n";
                for (auto &e : fs::directory_iterator(dir)) {
                    if (fs::is_regular_file(e.path()))
                        list += e.path().filename().string() + "\n";
                }
            } else {
                list = "Files:\n";
            }
            sendTextBlock(clientSock, list);
        } else if (cmd == "LISTALL") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string list = "All files:\n";
            if (fs::exists(BASE_DIR)) {
                for (auto &u : fs::directory_iterator(BASE_DIR)) {
                    if (fs::is_directory(u.path())) {
                        string uname = u.path().filename().string();
                        for (auto &f : fs::directory_iterator(u.path())) {
                            if (fs::is_regular_file(f.path()))
                                list += uname + "/" + f.path().filename().string() + "\n";
                        }
                    }
                }
            }
            sendTextBlock(clientSock, list);
        } else if (cmd == "PUT") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string filename;
            iss >> filename;
            if (filename.empty()) {
                string msg = "ERROR: No filename\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }

            // respond READY (client will send SIZE)
            string ready = "READY\n";
            send_all(clientSock, ready.c_str(), ready.size());

            // read SIZE line
            string sizeLine = recv_line(clientSock);
            if (sizeLine.rfind("SIZE", 0) != 0) {
                string msg = "ERROR: SIZE not received\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            // parse size
            size_t fsize = 0;
            try {
                fsize = stoull(sizeLine.substr(5));
            } catch (...) {
                string msg = "ERROR: Bad SIZE\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }

            // reply OK
            string ok = "OK\n";
            send_all(clientSock, ok.c_str(), ok.size());

            // write exact fsize bytes to file
            string cleanName = fs::path(filename).filename().string();
            string savePath = BASE_DIR + username + "/" + cleanName;
            ensureDir(BASE_DIR + username);
            ofstream out(savePath, ios::binary);
            if (!out.is_open()) {
                string msg = "ERROR: Cannot create file\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }

            // receive exact bytes
            size_t received = 0;
            char buf[BUFFER_SIZE];
            while (received < fsize) {
                size_t toRead = min((size_t)BUFFER_SIZE, fsize - received);
                ssize_t r = recv_exact(clientSock, buf, toRead);
                if (r <= 0) break;
                out.write(buf, r);
                received += r;
            }
            out.close();

            cout << "[LOG] PUT saved: " << savePath << " (" << received << " bytes)\n";
            string done = "OK\n";
            send_all(clientSock, done.c_str(), done.size());
        } else if (cmd == "GET") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string filename;
            iss >> filename;
            if (filename.empty()) {
                string msg = "ERROR: No filename\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string cleanName = fs::path(filename).filename().string();
            string path = BASE_DIR + username + "/" + cleanName;
            cout << "[LOG] GET request by " << username << " for " << path << endl;
            sendFileToClient(clientSock, path);
        } else if (cmd == "GETALL") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string rem;
            iss >> rem; // expected "username/filename"
            if (rem.empty()) {
                string msg = "ERROR: No remote path\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            // sanitize leading slash
            if (rem.size() > 0 && rem[0] == '/') rem.erase(0, 1);
            string path = BASE_DIR + rem;
            cout << "[LOG] GETALL request by " << username << " for " << path << endl;
            sendFileToClient(clientSock, path);
        } else if (cmd == "EXIT") {
            break;
        } else {
            string msg = "ERROR: Unknown command\n";
            send_all(clientSock, msg.c_str(), msg.size());
        }
    }

    close(clientSock);
    cout << "[LOG] Client disconnected\n";
}