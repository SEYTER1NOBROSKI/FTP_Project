#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <filesystem>

#define PORT 2121
#define BUFFER_SIZE 1024

using namespace std;
namespace fs = std::filesystem;

string users_file = "users.txt";
string base_dir = "users/";  // Нова домашня тека для всіх користувачів

// Створення директорії, якщо не існує
void createDirectory(const string &path) {
    if (!fs::exists(path)) {
        fs::create_directories(path);
    }
}

// Реєстрація користувача
bool registerUser(const string &username, const string &password) {
    ifstream infile(users_file);
    string line;
    while (getline(infile, line)) {
        if (line.find(username + ":") == 0) {
            return false; // Користувач вже існує
        }
    }
    infile.close();

    ofstream outfile(users_file, ios::app);
    outfile << username << ":" << password << endl;
    outfile.close();

    // Створюємо домашню директорію користувача
    string userPath = base_dir + username;
    createDirectory(userPath);
    cout << "[LOG] User directory created: " << userPath << endl;

    return true;
}

// Логін користувача
bool loginUser(const string &username, const string &password) {
    ifstream infile(users_file);
    string line;
    while (getline(infile, line)) {
        if (line == username + ":" + password) {
            return true;
        }
    }
    return false;
}

// PUT - завантаження файлу з клієнта на сервер
void handlePUT(int clientSock, const string &username, const string &filename) {
    string filepath = base_dir + username + "/" + fs::path(filename).filename().string();
    cout << "[LOG] Starting PUT for file: " << filepath << endl;

    ofstream file(filepath, ios::binary);
    if (!file) {
        string msg = "ERROR: Cannot create file\n";
        send(clientSock, msg.c_str(), msg.size(), 0);
        cout << "[LOG] Cannot create file: " << filepath << endl;
        return;
    }

    // Читаємо SIZE від клієнта
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) return;

    string sizeMsg(buffer, bytesReceived);
    size_t filesize = 0;
    if (sizeMsg.find("SIZE") == 0) {
        filesize = stoull(sizeMsg.substr(5));
        cout << "[LOG] File size received: " << filesize << " bytes" << endl;
    } else {
        string msg = "ERROR: SIZE not received\n";
        send(clientSock, msg.c_str(), msg.size(), 0);
        return;
    }

    // Відправляємо READY
    string readyMsg = "READY\n";
    send(clientSock, readyMsg.c_str(), readyMsg.size(), 0);

    // Приймаємо файл
    size_t totalReceived = 0;
    while (totalReceived < filesize) {
        ssize_t chunk = recv(clientSock, buffer, sizeof(buffer), 0);
        if (chunk <= 0) break;
        file.write(buffer, chunk);
        totalReceived += chunk;
    }

    file.close();
    cout << "[LOG] File " << fs::path(filename).filename().string() << " saved (" << totalReceived << " bytes)" << endl;
}

// GET - віддача файлу клієнту
void handleGET(int clientSock, const string &username, const string &filename) {
    string filepath = base_dir + username + "/" + fs::path(filename).filename().string();
    cout << "[LOG] Requested GET for file: " << filepath << endl;

    ifstream file(filepath, ios::binary);
    if (!file) {
        string msg = "ERROR: File not found\n";
        send(clientSock, msg.c_str(), msg.size(), 0);
        cout << "[LOG] File not found: " << filepath << endl;
        return;
    }

    string msg = "OK\n";
    send(clientSock, msg.c_str(), msg.size(), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer))) {
        send(clientSock, buffer, sizeof(buffer), 0);
    }
    if (file.gcount() > 0) {
        send(clientSock, buffer, file.gcount(), 0);
    }

    file.close();
    cout << "[LOG] File transfer completed: " << filename << endl;
}

// LIST - список файлів користувача
void handleLIST(int clientSock, const string &username) {
    string userPath = base_dir + username;
    string files = "Files:\n";

    if (!fs::exists(userPath)) {
        send(clientSock, "ERROR: User directory not found\n", 31, 0);
        return;
    }

    for (const auto &entry : fs::directory_iterator(userPath)) {
        if (entry.is_regular_file())
            files += entry.path().filename().string() + "\n";
    }

    send(clientSock, files.c_str(), files.size(), 0);
    cout << "[LOG] Sent LIST to client, bytes: " << files.size() << endl;
}

int main() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == -1) {
        cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Bind failed\n";
        close(serverSock);
        return 1;
    }

    if (listen(serverSock, 5) < 0) {
        cerr << "Listen failed\n";
        close(serverSock);
        return 1;
    }

    cout << "FTP Server started on port " << PORT << endl;
    createDirectory(base_dir);

    while (true) {
        int clientSock = accept(serverSock, nullptr, nullptr);
        if (clientSock < 0) {
            cerr << "Accept failed\n";
            continue;
        }

        cout << "Client connected" << endl;

        char buffer[BUFFER_SIZE];
        string username, password;
        bool authenticated = false;

        while (true) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytesReceived = recv(clientSock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;

            string command(buffer, bytesReceived);
            stringstream ss(command);
            string cmd;
            ss >> cmd;

            if (cmd == "REGISTER") {
                ss >> username >> password;
                if (registerUser(username, password)) {
                    string msg = "REGISTERED\n";
                    send(clientSock, msg.c_str(), msg.size(), 0);
                } else {
                    string msg = "ERROR: User exists\n";
                    send(clientSock, msg.c_str(), msg.size(), 0);
                }
            } else if (cmd == "LOGIN") {
                ss >> username >> password;
                if (loginUser(username, password)) {
                    authenticated = true;
                    string msg = "LOGGED IN\n";
                    send(clientSock, msg.c_str(), msg.size(), 0);
                } else {
                    string msg = "ERROR: Wrong credentials\n";
                    send(clientSock, msg.c_str(), msg.size(), 0);
                }
            } else if (authenticated && cmd == "PUT") {
                string filename;
                ss >> filename;
                handlePUT(clientSock, username, filename);
            } else if (authenticated && cmd == "GET") {
                string filename;
                ss >> filename;
                handleGET(clientSock, username, filename);
            } else if (authenticated && cmd == "LIST") {
                handleLIST(clientSock, username);
            } else {
                string msg = "ERROR: Unknown command\n";
                send(clientSock, msg.c_str(), msg.size(), 0);
            }
        }

        close(clientSock);
        cout << "Client disconnected" << endl;
    }

    close(serverSock);
    return 0;
}