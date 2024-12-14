#ifndef FILE_CONTROLLER_H
#define FILE_CONTROLLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>


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

void define_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination);
void send_message(int socket_fd, message_t *message);
void send_upload(int socket_fd, message_t *message);
void send_file_path(int socket_fd, message_t *message, char *file_path);
void send_offset_size(int socket_fd, message_t *message, char *file_path);
int send_file(int socket_fd, message_t *message, char *file_path_origin);
int handle_receive_message(int socket_fd, char *buffer);

int get_abs_path(char *file_path, char **abs_path);
int get_part_file_path(char *file_path, char **file_path_with_part);
int handle_write_part_file(char *buffer, int valread, message_t *client);
long get_size_file(char *file_path);
int file_exists(char *file_path);

#endif