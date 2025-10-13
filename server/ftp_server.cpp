#include "ftp_server.h"
#include "picosha2.h"
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
#include <random>

const string SERVER_ROOT = "server/";
const string USERS_FILE = SERVER_ROOT + "users.txt";
const string BASE_DIR = SERVER_ROOT + "users/"; // users/<username>/

mutex usersMutex;

void list_directory_recursive(const fs::path& path, const string& prefix, string& result) {
    for (const auto& entry : fs::directory_iterator(path)) {
        string entryName = entry.path().filename().string();
        if (fs::is_directory(entry.path())) {
            result += prefix + entryName + "/\n";
            list_directory_recursive(entry.path(), prefix + entryName + "/", result);
        } else if (fs::is_regular_file(entry.path())) {
            result += prefix + entryName + "\n";
        }
    }
}

string generate_salt(size_t length) {
    const std::string characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, characters.size() - 1);

    std::string salt;
    for (size_t i = 0; i < length; ++i) {
        salt += characters[distribution(generator)];
    }
    return salt;
}

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

    ifstream in_check(USERS_FILE);
    string u, s, h;
    while (in_check >> u >> s >> h) {
        if (u == username) {
            return false;
        }
    }
    in_check.close();

    string salt = generate_salt(16);
    string pass_with_salt = password + salt;
    string hashed_password = picosha2::hash256_hex_string(pass_with_salt);

    ofstream out(USERS_FILE, ios::app);
    out << username << " " << salt << " " << hashed_password << "\n";
    out.close();

    ensureDir(BASE_DIR + username);
    cout << "[LOG] Registered user: " << username << endl;
    return true;
}

bool checkUser(const string &username, const string &password) {
    lock_guard<mutex> lock(usersMutex);
    ifstream in(USERS_FILE);

    string u, stored_salt, stored_hash;
    while (in >> u >> stored_salt >> stored_hash) {
        if (u == username) {
            string pass_with_salt = password + stored_salt;
            string hashed_attempt = picosha2::hash256_hex_string(pass_with_salt);
            
            return hashed_attempt == stored_hash;
        }
    }
    return false;
}

void handleClient(int clientSock) {
    string username;
    bool authenticated = false;
    string userHomeDir;
    string currentPath;

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
                userHomeDir = fs::path(BASE_DIR + username).lexically_normal().string();
                currentPath = userHomeDir;
                ensureDir(currentPath);
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
                "PUT <local_path>          (Upload file to current dir)\n"
                "GET <filename>            (Download file from current dir)\n"
                "LIST                      (List files in current dir)\n"
                "PWD                       (Show current server directory)\n"
                "CD <dirname>              (Change server directory)\n"
                "MKDIR <dirname>           (Create directory)\n"
                "DELETE <filename>         (Delete file)\n"
                "LISTALL                   (List all files from all users)\n"
                "GETALL <user/file>        (Download any user's file)\n"
                "HELP\n"
                "EXIT\n";
            sendTextBlock(clientSock, helpTxt);
        } else if (cmd == "LIST") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string list = "Files in current directory:\n";
            if (fs::exists(currentPath)) {
                list_directory_recursive(currentPath, "", list);
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
                        list_directory_recursive(u.path(), uname + "/", list);
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

            string cleanName = fs::path(filename).filename().string();
            string savePath = currentPath + "/" + cleanName;

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
            string path = currentPath + "/" + cleanName;
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
        } else if (cmd == "PWD") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            
            string relativePath = "/";
            if (currentPath.length() > userHomeDir.length()) {
                relativePath += currentPath.substr(userHomeDir.length() + 1);
            }
            string msg = "OK\n" + relativePath + "\n";
            send_all(clientSock, msg.c_str(), msg.size());
        } else if (cmd == "MKDIR") {
                        if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string dirname;
            iss >> dirname;
            if (dirname.empty()) {
                string msg = "ERROR: No directory name specified\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }

            fs::path newDir = fs::path(currentPath) / dirname;
            string newDirStr = newDir.lexically_normal().string();

            if (newDirStr.rfind(userHomeDir, 0) != 0) {
                 string msg = "ERROR: Permission denied\n";
                 send_all(clientSock, msg.c_str(), msg.size());
                 continue;
            }

            if (fs::create_directory(newDir)) {
                string msg = "OK\nDirectory created\n";
                send_all(clientSock, msg.c_str(), msg.size());
            } else {
                string msg = "ERROR: Could not create directory\n";
                send_all(clientSock, msg.c_str(), msg.size());
            }
        } else if (cmd == "DELETE") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string filename;
            iss >> filename;
            if (filename.empty()) {
                string msg = "ERROR: No filename specified\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            
            fs::path fileToDelete = fs::path(currentPath) / filename;
            string fileStr = fileToDelete.lexically_normal().string();

            if (fileStr.rfind(userHomeDir, 0) != 0) {
                 string msg = "ERROR: Permission denied\n";
                 send_all(clientSock, msg.c_str(), msg.size());
                 continue;
            }

            if (fs::is_regular_file(fileToDelete) && fs::remove(fileToDelete)) {
                string msg = "OK\nFile deleted\n";
                send_all(clientSock, msg.c_str(), msg.size());
            } else {
                string msg = "ERROR: File not found or could not be deleted\n";
                send_all(clientSock, msg.c_str(), msg.size());
            }
        } else if (cmd == "CD") {
            if (!authenticated) {
                string msg = "ERROR: Not logged in\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }
            string dirname;
            iss >> dirname;
            if (dirname.empty()) {
                string msg = "ERROR: No directory specified\n";
                send_all(clientSock, msg.c_str(), msg.size());
                continue;
            }

            fs::path newPath = fs::path(currentPath) / dirname;
            newPath = newPath.lexically_normal();
            string newPathStr = newPath.string();

            if (newPathStr.rfind(userHomeDir, 0) != 0) {
                 string msg = "ERROR: Permission denied\n";
                 send_all(clientSock, msg.c_str(), msg.size());
                 continue;
            }

            if (fs::exists(newPath) && fs::is_directory(newPath)) {
                currentPath = newPathStr;
                string msg = "OK\nDirectory changed\n";
                send_all(clientSock, msg.c_str(), msg.size());
            } else {
                string msg = "ERROR: Directory not found\n";
                send_all(clientSock, msg.c_str(), msg.size());
            }
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