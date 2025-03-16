CC = g++
CFLAGS = -Wall -Wextra -std=c++11
LIBS = -lncurses
BIN_PATH = ./bin

SERVER = $(BIN_PATH)/server
CLIENT = $(BIN_PATH)/client

SRV_SRC = server.cpp
CLI_SRC = client.cpp

all: $(SERVER) $(CLIENT)

$(BIN_PATH):
	mkdir -p $(BIN_PATH)

$(SERVER): $(SRV_SRC) | $(BIN_PATH)
	$(CC) $(CFLAGS) $(SRV_SRC) -o $(SERVER)

$(CLIENT): $(CLI_SRC) | $(BIN_PATH)
	$(CC) $(CFLAGS) $(CLI_SRC) -o $(CLIENT) $(LIBS)

clean:
	rm -f $(SERVER) $(CLIENT)
