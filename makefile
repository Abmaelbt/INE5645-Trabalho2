all:
	gcc -o remcp_server remcp_server.c -lpthread -fopenmp file_controller.c socket.c
	gcc -o remcp remcp.c file_controller.c socket.c

clean:
	rm -f remcp_server remcp

run-server:
	./server