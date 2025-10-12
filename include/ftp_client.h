#define FTP_CLIENT_H

#include <string>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;

#define BUFFER_SIZE 4096

bool send_all(int sock, const char *data, size_t len);

bool send_line(int sock, const string &line);

string recv_line(int sock);

bool recv_to_stream(int sock, ostream &out, uint64_t n);

string expand_path(const string &path);

void do_PUT(int sock, const string &localPath);

void do_GET_common(int sock, const string &cmd, const string &arg);

void do_LIST_like(int sock, const string &cmd);