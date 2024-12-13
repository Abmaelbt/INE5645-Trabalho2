#ifndef SOCKET_H
#define SOCKET_H

#include <arpa/inet.h>

#define FAILED_TO_SEND_MESSAGE_EXCEPTION "Falha ao enviar mensagem. [0100]\n"
#define SOCKET_CREATE_EXCEPTION "Falha ao criar socket. [0101]\n"
#define SOCKET_BIND_EXCEPTION "Falha ao associar socket. [0102]\n"
#define SOCKET_LISTEN_EXCEPTION "Falha ao escutar no socket. [0103]\n"
#define POOL_EXCEPTION "Falha ao usar poll no socket. [0104]\n"
#define ACCEPT_EXCEPTION "Falha ao aceitar conexão. [0105]\n"
#define CONNECTION_EXCEPTION "Falha ao conectar ao servidor. [0106]\n"
#define SERVER_CLOSED_EXCEPTION "Servidor encerrou a conexão. [0107]\n"
#define FAILED_TO_RECEIVE_MESSAGE_EXCEPTION "Falha ao receber mensagem. [0108]\n"

#define PORT 8080
#define BUFFER_SIZE 128

// estrutura de mensagens para comunicacao
typedef struct message_t
{
    char *file_path; // caminho da mensagem
    char *buffer;    // buffer da mensagem
    int upload;      // 1 para upload, 0 para download
} message_t;

// funcoes auxiliares para manipulacao de sockets
void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination);
void send_message(int socket_fd, message_t *message);
void send_upload(int socket_fd, message_t *message, int verbose);
void send_file_path(int socket_fd, message_t *message, char *file_path, int verbose);
void send_offset_size(int socket_fd, message_t *message, char *file_path, int verbose);
int send_file(int socket_fd, message_t *message, char *file_path_origin, int verbose);
int handle_receive_message(int socket_fd, char *buffer);
void verbose_printf(int verbose, const char *format, ...);

#endif
