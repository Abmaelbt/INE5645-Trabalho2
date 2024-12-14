## **Compilar**
### servidor
gcc -o remcp_server main.c -lpthread -fopenmp file_controller.c socket.c

### cliente
gcc -o remcp_client client.c file_controller.c socket.c

