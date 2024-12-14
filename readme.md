## **Compilar**
### servidor
gcc -o remcp_server remcp_server.c -lpthread -fopenmp file_controller.c socket.c

### cliente
gcc -o remcp_client remcp_client.c file_controller.c socket.c

