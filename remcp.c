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
        //separar host e caminho, se necessário
        *host = strndup(arg, colon - arg);
        *file_path = strdup(colon + 1);
    }
    else
    {
        //Localhost por padrão se não for passado um host
        *host = "127.0.0.1";
        *file_path = strdup(arg);
    }
}

int receive_file(int socket_fd, message_t *message)
{
    printf("Recebendo arquivo...\n");
    while (1)
    {
        int valread = handle_receive_message(socket_fd, message->buffer);

        //arquivo .part
        int result = handle_write_part_file(message->buffer, valread, message);

        //Finalizar recebimento
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
        printf("Uso: ./client [host:]caminho_origem [host:]caminho_destino\n");
        return 1;
    }

    //informações dos caminhos e servidores
    char *host_origin = NULL;
    char *file_path_origin = NULL;
    char *host_destination = NULL;
    char *file_path_destination = NULL;
    char *host_server = NULL;
    int upload = 0;

    parse_arguments(argv[1], &host_origin, &file_path_origin);
    parse_arguments(argv[2], &host_destination, &file_path_destination);

    //Determinar se a operação é um upload
    upload = strcmp(host_origin, "127.0.0.1") == 0;

    host_server = upload ? host_destination : host_origin;

    int socket_fd;
    struct sockaddr_in address;

    //Criar e configurar o socket
    create_socket(&socket_fd, &address, host_server);

    //Conexão
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Falha ao conectar ao servidor.");
        exit(EXIT_FAILURE);
    }

    printf("Conexão estabelecida com o servidor...\n");

    //Aloca memória para a estrutura de mensagem
    message_t *message = (message_t *)malloc(sizeof(message_t));
    message->buffer = (char *)malloc(BUFFER_SIZE);
    message->upload = upload;

    //Gerencia o envio ou recebimento de arquivos
    if (upload)
    {
        //Envio de arquivo
        printf("Iniciando upload...\n");
        send_upload(socket_fd, message);
        send_file_path(socket_fd, message, file_path_destination);
        send_file(socket_fd, message, file_path_origin);
    }
    else
    {
        //Recebimento de arquivo
        printf("Iniciando download...\n");
        send_upload(socket_fd, message);
        send_file_path(socket_fd, message, file_path_origin);
        message->file_path = file_path_destination;
        send_offset_size(socket_fd, message, file_path_destination);
        receive_file(socket_fd, message);
    }

    //Liberação de recursos
    close(socket_fd);
    free(message->buffer);
    free(message);

    printf("Operação concluída.\n");

    return 0;
}