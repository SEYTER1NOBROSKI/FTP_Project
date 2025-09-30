#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace std;

#define BUFFER_SIZE 4096

// send all
bool send_all(int sock, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t s = send(sock, data + sent, len - sent, 0);
        if (s <= 0) return false;
        sent += s;
    }
    return true;
}

// send line (adds '\n')
bool send_line(int sock, const string &line) {
    string s = line + "\n";
    return send_all(sock, s.c_str(), s.size());
}

// receive line until '\n'
string recv_line(int sock) {
    string line;
    char c;
    while (true) {
        ssize_t r = recv(sock, &c, 1, 0);
        if (r <= 0) return string();
        if (c == '\n') break;
        line.push_back(c);
    }
    return line;
}

// receive exact n bytes and write to stream
bool recv_to_stream(int sock, ostream &out, uint64_t n) {
    char buf[BUFFER_SIZE];
    uint64_t got = 0;
    while (got < n) {
        size_t want = (size_t)min<uint64_t>(BUFFER_SIZE, n - got);
        ssize_t r = recv(sock, buf, want, 0);
        if (r <= 0) return false;
        out.write(buf, r);
        got += r;
    }
    return true;
}

// expand ~ to HOME
string expand_path(const string &path) {
    if (!path.empty() && path[0] == '~') {
        const char *h = getenv("HOME");
        if (h) return string(h) + path.substr(1);
    }
    return path;
}

void do_PUT(int sock, const string &localPath) {
    string real = expand_path(localPath);
    if (!fs::exists(real) || !fs::is_regular_file(real)) {
        cerr << "ERROR: Cannot open file " << localPath << "\n";
        return;
    }
    uint64_t fsize = fs::file_size(real);
    string remoteName = fs::path(real).filename().string();

    // send command
    if (!send_line(sock, string("PUT ") + remoteName)) {
        cerr << "Send failed\n"; return;
    }

    // expect READY
    string ready = recv_line(sock);
    if (ready.rfind("READY", 0) != 0) {
        cerr << "Server error: " << ready << "\n";
        return;
    }

    // send SIZE
    if (!send_line(sock, string("SIZE ") + to_string((unsigned long long)fsize))) {
        cerr << "Send failed\n"; return;
    }

    // expect OK
    string ok = recv_line(sock);
    if (ok.rfind("OK", 0) != 0) {
        cerr << "Server error: " << ok << "\n";
        return;
    }

    // send file bytes
    ifstream in(real, ios::binary);
    char buf[BUFFER_SIZE];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        size_t toSend = (size_t)in.gcount();
        if (!send_all(sock, buf, toSend)) { cerr << "Send failed\n"; in.close(); return; }
    }
    in.close();

    // wait final OK
    string final = recv_line(sock);
    if (final.rfind("OK", 0) == 0) {
        cout << "File uploaded: " << remoteName << " (" << fsize << " bytes)\n";
    } else {
        cout << "Server response: " << final << "\n";
    }
}

void do_GET_common(int sock, const string &cmd, const string &arg) {
    // cmd: "GET" or "GETALL"
    if (!send_line(sock, cmd + " " + arg)) { cerr << "Send failed\n"; return; }

    string header = recv_line(sock);
    if (header.empty()) { cerr << "No response\n"; return; }
    if (header.rfind("ERROR", 0) == 0) {
        cout << header << "\n";
        return;
    }
    if (header.rfind("OK", 0) != 0) {
        cout << "Unexpected response: " << header << "\n";
        return;
    }
    string sizeLine = recv_line(sock);
    uint64_t fsize = 0;
    try { fsize = stoull(sizeLine); } catch (...) { cerr << "Bad size\n"; return; }

    string localName = fs::path(arg).filename().string();
    ofstream out(localName, ios::binary);
    if (!out.is_open()) { cerr << "Cannot create local file\n"; return; }

    if (!recv_to_stream(sock, out, fsize)) {
        cerr << "Receive failed\n"; out.close(); return;
    }
    out.close();
    cout << "File downloaded: " << localName << " (" << fsize << " bytes)\n";
}

void do_LIST_like(int sock, const string &cmd) {
    // cmd: LIST or LISTALL or HELP
    if (!send_line(sock, cmd)) { cerr << "Send failed\n"; return; }
    string header = recv_line(sock);
    if (header.empty()) { cerr << "No response\n"; return; }
    if (header.rfind("ERROR", 0) == 0) {
        cout << header << "\n";
        return;
    }
    if (header.rfind("OK", 0) != 0) {
        cout << header << "\n";
        return;
    }
    string sizeLine = recv_line(sock);
    uint64_t sz = 0;
    try { sz = stoull(sizeLine); } catch (...) { cerr << "Bad size\n"; return; }
    // read and output to stdout
    string outText;
    outText.resize(sz);
    size_t got = 0;
    while (got < sz) {
        ssize_t r = recv(sock, &outText[got], sz - got, 0);
        if (r <= 0) break;
        got += r;
    }
    cout << outText;
}

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