CC = gcc
CFLAGS = -Wall -pthread
SERVER_SRC = remcp_serv.c
CLIENT_SRC = remcp.c
SERVER_BIN = remcp_serv
CLIENT_BIN = remcp

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC)

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	@echo "Usage: make run-client ARGS='<IP> <source_file> <dest_file>'"
	./$(CLIENT_BIN) $(ARGS)

help:
	@echo "  make            - Compila o servidor e o cliente."
	@echo "  make run-server - Executa o servidor."
	@echo "  make run-client ARGS='<IP> <source_file> <dest_file>' - Executa o cliente." //testar melhor depois
