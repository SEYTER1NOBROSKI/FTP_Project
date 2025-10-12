CXX = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS = -pthread

OBJ_DIR = obj
SRCDIR_SERVER = server
SRCDIR_CLIENT = client
INCLUDE_DIR = include

BIN_DIR = bin
SERVER_BIN = $(BIN_DIR)/ftp_server
CLIENT_BIN = $(BIN_DIR)/ftp_client

SERVER_OBJ = $(OBJ_DIR)/ftp_server.o $(OBJ_DIR)/ftp_server_main.o

CLIENT_OBJ = $(OBJ_DIR)/ftp_client.o $(OBJ_DIR)/ftp_client_main.o

.PHONY: all server client clean prepare rebuild

all: prepare server client
	@echo "Build finished: $(SERVER_BIN) and $(CLIENT_BIN)"

# create bin dir and ensure server data dirs exist
prepare:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(OBJ_DIR)

server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $(SERVER_BIN) $(SERVER_OBJ) $(LDFLAGS)
	@echo "Server built -> $(SERVER_BIN)"

$(OBJ_DIR)/ftp_server.o: $(SRCDIR_SERVER)/ftp_server.cpp $(INCLUDE_DIR)/ftp_server.h | prepare
	$(CXX) $(CXXFLAGS) -c $(SRCDIR_SERVER)/ftp_server.cpp -o $(OBJ_DIR)/ftp_server.o -I$(INCLUDE_DIR)

$(OBJ_DIR)/ftp_server_main.o: $(SRCDIR_SERVER)/ftp_server_main.cpp $(INCLUDE_DIR)/ftp_server.h | prepare
	$(CXX) $(CXXFLAGS) -c $(SRCDIR_SERVER)/ftp_server_main.cpp -o $(OBJ_DIR)/ftp_server_main.o -I$(INCLUDE_DIR)

client: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $(CLIENT_BIN) $(CLIENT_OBJ)
	@echo "Client built -> $(CLIENT_BIN)"

$(OBJ_DIR)/ftp_client.o: $(SRCDIR_CLIENT)/ftp_client.cpp $(INCLUDE_DIR)/ftp_client.h | prepare
	$(CXX) $(CXXFLAGS) -c $(SRCDIR_CLIENT)/ftp_client.cpp -o $(OBJ_DIR)/ftp_client.o -I$(INCLUDE_DIR)

$(OBJ_DIR)/ftp_client_main.o: $(SRCDIR_CLIENT)/ftp_client_main.cpp $(INCLUDE_DIR)/ftp_client.h | prepare
	$(CXX) $(CXXFLAGS) -c $(SRCDIR_CLIENT)/ftp_client_main.cpp -o $(OBJ_DIR)/ftp_client_main.o -I$(INCLUDE_DIR)

clean:
	@rm -rf $(BIN_DIR)
	@echo "Cleaned build artifacts."

# convenience target to rebuild everything
rebuild: clean all