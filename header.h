#ifndef FILE_CONTROLLER_H
#define FILE_CONTROLLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>


// mensagens de erro relacionadas aos arquivos
#define EOF_MARKER EOF
#define FILE_EXISTS_EXCEPTION "File already exists. [0000]\n"
#define FILE_NOT_FOUND_EXCEPTION "File not found. [0001]\n"
#define INVALID_FILE_PATH_EXCEPTION "Invalid file path. [0002]\n"

//mensagens de erros relacionadas aos sockets
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

// estrutura de uma msg de comunicacao
typedef struct message_t
{
    char *file_path; //Caminho do arquivo associado à mensagem
    char *buffer;    //Buffer de dados da mensagem
    int upload;      //Indica se é upload (1) ou download (0)
    int request_count; //Contador de requisições para o cliente
} message_t;

// funcoes de manipulacao dos sockets
void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination);
void send_message(int socket_fd, message_t *message);
void send_upload(int socket_fd, message_t *message);
void send_file_path(int socket_fd, message_t *message, char *file_path);
void send_offset_size(int socket_fd, message_t *message, char *file_path);
int send_file(int socket_fd, message_t *message, char *file_path_origin);
int handle_receive_message(int socket_fd, char *buffer);
void verbose_printf(int verbose, const char *format, ...);

// funcoes de manipulacao dos arquivos
int get_abs_path(char *file_path, char **abs_path);
int get_part_file_path(char *file_path, char **file_path_with_part);
int handle_write_part_file(char *buffer, int valread, message_t *client);
long get_size_file(char *file_path);
int file_exists(char *file_path);

#endif