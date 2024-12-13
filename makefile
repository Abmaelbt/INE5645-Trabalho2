SERVER_SRC =  remcp_server.c
CLIENT_SRC = remcp.c
SERVER_BIN = remcp_server
CLIENT_BIN = remcp


all:
	gcc -o remcp_server remcp_server.c -lpthread -fopenmp file_controller.c socket.c
	gcc -o remcp remcp.c file_controller.c socket.c

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

help:
	@echo "  make            - Compila o servidor e o cliente."
	@echo "  make run-server - Executa o servidor."