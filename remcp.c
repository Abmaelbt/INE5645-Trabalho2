#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "file_controller.h"
#include "socket.h"

int verbose = 0;

// separa host e caminho do arquivo do argumento fornecido
void parse_arguments(const char *arg, char **host, char **file_path)
{
    char *colon = strchr(arg, ':');
    if (colon)
    {
        *host = strndup(arg, colon - arg);
        *file_path = strdup(colon + 1);
    }
    else
    {
        *host = "127.0.0.1";
        *file_path = strdup(arg);
    }
}

// gerencia o recebimento de arquivos do servidor (download)
int receive_file(int socket_fd, message_t *message, int verbose)
{
    verbose_printf(verbose, "recebendo arquivo\n");
    while (1)
    {
        int valread = handle_receive_message(socket_fd, message->buffer);
        int result = handle_write_part_file(message->buffer, valread, message, verbose);

        if (result != 0)
        {
            return result;
        }
        send(socket_fd, message->buffer, strlen(message->buffer), 0);
    }
}

int main(int argc, char const *argv[])
{
    if (argc < 3 || argc > 4)
    {
        printf("uso: ./client [host:]caminho_origem [host:]caminho_destino [-v]\n");
        return 1;
    }

    char *host_origin = NULL;
    char *file_path_origin = NULL;
    char *host_destination = NULL;
    char *file_path_destination = NULL;
    char *host_server = NULL;
    int upload = 0;

    parse_arguments(argv[1], &host_origin, &file_path_origin);
    parse_arguments(argv[2], &host_destination, &file_path_destination);

    verbose = argc == 4 && strcmp(argv[3], "-v") == 0;

    upload = strcmp(host_origin, "127.0.0.1") == 0;
    host_server = upload ? host_destination : host_origin;

    int socket_fd;
    struct sockaddr_in address;

    // cria e configura o socket
    create_socket(&socket_fd, &address, host_server);

    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("falha ao conectar ao servidor");
        exit(EXIT_FAILURE);
    }

    verbose_printf(verbose, "conexao estabelecida com o servidor\n");

    message_t *message = (message_t *)malloc(sizeof(message_t));
    message->buffer = (char *)malloc(BUFFER_SIZE);
    message->upload = upload;

    // gerencia upload ou download de arquivo
    if (upload)
    {
        send_upload(socket_fd, message, verbose);
        send_file_path(socket_fd, message, file_path_destination, verbose);
        send_file(socket_fd, message, file_path_origin, verbose);
    }
    else
    {
        send_upload(socket_fd, message, verbose);
        send_file_path(socket_fd, message, file_path_origin, verbose);
        message->file_path = file_path_destination;
        send_offset_size(socket_fd, message, file_path_destination, verbose);
        receive_file(socket_fd, message, verbose);
    }

    // libera recursos e encerra a conexao
    close(socket_fd);
    free(message);

    return 0;
}