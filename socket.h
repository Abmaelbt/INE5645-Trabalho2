#ifndef SOCKET_H
#define SOCKET_H

#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 128

typedef struct message_t
{
    char *file_path; //Caminho do arquivo associado à mensagem
    char *buffer;    //Buffer de dados da mensagem
    int upload;      //Indica se é upload (1) ou download (0)
    int request_count; //Contador de requisições para o cliente
} message_t;

void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination);
void send_message(int socket_fd, message_t *message);
void send_upload(int socket_fd, message_t *message);
void send_file_path(int socket_fd, message_t *message, char *file_path);
void send_offset_size(int socket_fd, message_t *message, char *file_path);
int send_file(int socket_fd, message_t *message, char *file_path_origin);
int handle_receive_message(int socket_fd, char *buffer);
void verbose_printf(int verbose, const char *format, ...);

#endif // SOCKET_H
