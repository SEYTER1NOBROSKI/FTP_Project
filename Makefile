CXX = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS = -pthread

SRCDIR_SERVER = server
SRCDIR_CLIENT = client

SERVER_SRC = $(SRCDIR_SERVER)/ftp_server.cpp
CLIENT_SRC = $(SRCDIR_CLIENT)/ftp_client.cpp

BIN_DIR = bin
SERVER_BIN = $(BIN_DIR)/ftp_server
CLIENT_BIN = $(BIN_DIR)/ftp_client

USERS_FILE = $(SRCDIR_SERVER)/users.txt
HOME_ROOT = $(SRCDIR_SERVER)/home

.PHONY: all server client clean prepare run run-local

all: prepare server client
	@echo "Build finished: $(SERVER_BIN) and $(CLIENT_BIN)"

# create bin dir and ensure server data dirs exist
prepare:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(HOME_ROOT)
	@if [ ! -f $(USERS_FILE) ]; then echo "admin:admin" > $(USERS_FILE); echo "Created default $(USERS_FILE) (admin:admin)"; fi

server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_SRC) | prepare
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER_BIN) $(LDFLAGS)
	@echo "Server built -> $(SERVER_BIN)"

client: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_SRC) | prepare
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)
	@echo "Client built -> $(CLIENT_BIN)"

# quick way to run server and client in separate terminals (local dev)
# NOTE: this target uses x-terminal-emulator (Debian/Ubuntu). You can edit it to gnome-terminal, xterm, konsole, etc.
run-local:
	@echo "Starting server on port 2121 in new terminal..."
	@if command -v x-terminal-emulator >/dev/null 2>&1; then \
	  x-terminal-emulator -e "$(SERVER_BIN) 2121" & \
	else \
	  echo "x-terminal-emulator not found. To run manually: $(SERVER_BIN) 2121"; \
	fi
	@sleep 1
	@echo "You can start the client: $(CLIENT_BIN) 127.0.0.1 2121"

clean:
	@rm -rf $(BIN_DIR)
	@echo "Cleaned build artifacts."

# convenience target to rebuild everything
rebuild: clean all