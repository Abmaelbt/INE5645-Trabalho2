SERVER_SRC =  remcp_server.c
CLIENT_SRC = remcp.c
SERVER_BIN = remcp_server
CLIENT_BIN = remcp


all:
	gcc -o remcp_server remcp_server.c -lpthread -fopenmp  header.c
	gcc -o remcp remcp.c header.c

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	./$(SERVER_BIN) -v

help:
	@echo "  make            - Compila o servidor e o cliente."
	@echo "  make run-server - Executa o servidor."