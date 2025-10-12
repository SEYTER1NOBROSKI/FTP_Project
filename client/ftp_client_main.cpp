#include "ftp_client.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        cout << "Usage: ./ftp_client <server_ip> <port>\n";
        return 1;
    }
    string serverIp = argv[1];
    int port = stoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { cerr << "Socket failed\n"; return 1; }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, serverIp.c_str(), &srv.sin_addr);

    if (connect(sock, (sockaddr *)&srv, sizeof(srv)) < 0) {
        cerr << "Connect failed\n"; close(sock); return 1;
    }

    cout << "Connected to FTP server.\n";

    string line;
    while (true) {
        cout << "ftp> ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        istringstream iss(line);
        string cmd; iss >> cmd;
        if (cmd == "EXIT") {
            send_line(sock, "EXIT");
            break;
        } else if (cmd == "PUT") {
            string f; iss >> f;
            if (f.empty()) { cerr << "Usage: PUT <local_path>\n"; continue; }
            do_PUT(sock, f);
        } else if (cmd == "GET") {
            string f; iss >> f;
            if (f.empty()) { cerr << "Usage: GET <filename>\n"; continue; }
            do_GET_common(sock, "GET", f);
        } else if (cmd == "GETALL") {
            string f; iss >> f;
            if (f.empty()) { cerr << "Usage: GETALL <username/filename>\n"; continue; }
            do_GET_common(sock, "GETALL", f);
        } else if (cmd == "LIST" || cmd == "LISTALL" || cmd == "HELP") {
            do_LIST_like(sock, cmd);
        } else if (cmd == "REGISTER" || cmd == "LOGIN") {
            // send as-is and print single-line response or size-prefixed response
            send_line(sock, line);
            // server previously replies with either "REGISTERED\n" or error or "LOGGED IN\n"
            string resp = recv_line(sock);
            if (resp.empty()) cout << "No response\n"; else cout << resp << "\n";
        } else {
            // unknown: send raw and print response
            send_line(sock, line);
            string resp = recv_line(sock);
            if (resp.empty()) cout << "No response\n"; else cout << resp << "\n";
        }
    }

    close(sock);
    cout << "Disconnected.\n";
    return 0;
}