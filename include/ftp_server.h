#define FTP_SERVER_H

#include <string>
#include <mutex>
#include <filesystem>

extern const std::string SERVER_ROOT;
extern const std::string USERS_FILE;
extern const std::string BASE_DIR;
extern std::mutex usersMutex;

namespace fs = std::filesystem;
using namespace std;

#define BUFFER_SIZE 4096

void list_directory_recursive(const fs::path& path, const string& prefix, string& result);

string generate_salt(size_t length);

void ensureDir(const std::string &p);

bool send_all(int sock, const char *data, size_t len);

std::string recv_line(int sock);

ssize_t recv_exact(int sock, char *buf, size_t n);

void sendFileToClient(int clientSock, const std::string &filepath);

void sendTextBlock(int clientSock, const std::string &text);

bool registerUser(const std::string &username, const std::string &password);

bool checkUser(const std::string &username, const std::string &password);

void handleClient(int clientSock);

