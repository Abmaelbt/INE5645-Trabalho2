#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "header.h"


void parse_arguments(const char *arg, char **host, char **file_path)
{
    char *colon = strchr(arg, ':');
    if (colon)
    {
        // separar host e caminho, se necessario
        *host = strndup(arg, colon - arg);
        *file_path = strdup(colon + 1);
    }
    else
    {
        // localhost por padrao se nao for passado um host
        *host = "127.0.0.1";
        *file_path = strdup(arg);
    }
}

int download_file(int socket_fd, message_t *message)
{
    printf("recebendo arquivo...\n");
    while (1)
    {
        int valread = handle_receive_message(socket_fd, message->buffer);

        // arquivo .part
        int result = handle_write_part_file(message->buffer, valread, message);

        // finalizar recebimento
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
        printf("uso: ./client [host:]caminho_origem [host:]caminho_destino\n");
        return 1;
    }

    // informacoes dos caminhos e servidores
    char *host_origin = NULL;
    char *file_path_origin = NULL;
    char *host_destination = NULL;
    char *file_path_destination = NULL;
    char *host_server = NULL;
    int upload = 0;

    parse_arguments(argv[1], &host_origin, &file_path_origin);
    parse_arguments(argv[2], &host_destination, &file_path_destination);

    // determinar se a operacao e um upload
    upload = strcmp(host_origin, "127.0.0.1") == 0;

    host_server = upload ? host_destination : host_origin;

    int socket_fd;
    struct sockaddr_in address;

    // criar e configurar o socket
    define_socket(&socket_fd, &address, host_server);

    // conexao
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("falha ao conectar ao servidor.");
        exit(EXIT_FAILURE);
    }

    printf("Conexão estabelecida com o servidor...\n");

    // aloca memoria para a estrutura de mensagem
    message_t *message = (message_t *)malloc(sizeof(message_t));
    message->buffer = (char *)malloc(BUFFER_SIZE);
    message->upload = upload;

    // gerencia o envio ou recebimento de arquivos
    if (upload)
    {
        // envio de arquivo
        printf("iniciando upload...\n");
        send_upload(socket_fd, message);
        send_file_path(socket_fd, message, file_path_destination);
        send_file(socket_fd, message, file_path_origin);
    }
    else
    {
        // recebimento de arquivo
        printf("iniciando download...\n");
        send_upload(socket_fd, message);
        send_file_path(socket_fd, message, file_path_origin);
        message->file_path = file_path_destination;
        send_offset_size(socket_fd, message, file_path_destination);
        download_file(socket_fd, message);
    }

    // liberacao de recursos
    close(socket_fd);
    free(message->buffer);
    free(message);

    printf("operação concluida.\n");

    return 0;
}
