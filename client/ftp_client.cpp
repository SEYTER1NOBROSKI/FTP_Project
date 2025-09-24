#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 2121
#define BUFFER_SIZE 1024

using namespace std;

// Відправка файлу на сервер
void sendFile(int sock, const string &filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cout << "ERROR: Cannot open file " << filename << endl;
        return;
    }

    // Розмір файлу
    file.seekg(0, ios::end);
    size_t filesize = file.tellg();
    file.seekg(0, ios::beg);

    // Надсилаємо SIZE
    string sizeMsg = "SIZE " + to_string(filesize) + "\n";
    send(sock, sizeMsg.c_str(), sizeMsg.size(), 0);

    // Чекаємо READY від сервера
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0 || string(buffer, bytesReceived).find("READY") != 0) {
        cout << "Server error: " << buffer << endl;
        return;
    }

    // Надсилаємо файл
    size_t totalSent = 0;
    while (file.read(buffer, sizeof(buffer))) {
        ssize_t sent = send(sock, buffer, sizeof(buffer), 0);
        totalSent += sent;
    }
    if (file.gcount() > 0) {
        ssize_t sent = send(sock, buffer, file.gcount(), 0);
        totalSent += sent;
    }

    file.close();
    cout << "File " << filename << " uploaded (" << totalSent << " bytes)." << endl;
}

// Завантаження файлу з сервера
void receiveFile(int sock, const string &filename) {
    ofstream file(filename, ios::binary);
    if (!file) {
        cout << "ERROR: Cannot create file " << filename << endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytesReceived;
    while ((bytesReceived = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        file.write(buffer, bytesReceived);
        if (bytesReceived < BUFFER_SIZE) break;
    }

    file.close();
    cout << "File " << filename << " downloaded from server." << endl;
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        cerr << "Socket creation failed\n";
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Connection failed\n";
        close(sock);
        return 1;
    }

    cout << "Connected to FTP server." << endl;

    string command;
    char buffer[BUFFER_SIZE];

    while (true) {
        cout << "ftp> ";
        getline(cin, command);
        if (command == "EXIT") break;

        send(sock, command.c_str(), command.size(), 0);

        stringstream ss(command);
        string cmd;
        ss >> cmd;

        if (cmd == "PUT") {
            string filename;
            ss >> filename;
            sendFile(sock, filename);
        } else if (cmd == "GET") {
            string filename;
            ss >> filename;
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesReceived > 0 && string(buffer, bytesReceived).find("OK") == 0) {
                receiveFile(sock, filename);
            } else {
                cout << buffer << endl;
            }
        } else {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
            if (bytesReceived > 0) cout << string(buffer, bytesReceived);
        }
    }

    close(sock);
    cout << "Disconnected from server." << endl;
    return 0;
}