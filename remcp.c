#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "header.h"


#define MAX_RETRIES 7
#define RETRY_WAIT_TIME 5

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

int retry_connect(int *socket_fd, struct sockaddr_in *address, const char *host_server)
{
    int attempts = 0;
    while (attempts < MAX_RETRIES)
    {
        printf("Tentativa de conexão (%d/%d)...\n", attempts + 1, MAX_RETRIES);
        define_socket(socket_fd, address, (char *)host_server);

        if (connect(*socket_fd, (struct sockaddr *)address, sizeof(*address)) == 0)
        {
            printf("Conexão estabelecida. Aguardando resposta do servidor...\n");
            char buffer[256] = {0};
            recv(*socket_fd, buffer, sizeof(buffer), 0);

            if (strncmp(buffer, "SERVER_BUSY", 11) == 0)
            {
                printf("Servidor ocupado. Tentando novamente...\n");
                close(*socket_fd);
                sleep(RETRY_WAIT_TIME);
                attempts++;
                continue;
            }
            return 0; // Conexão bem-sucedida
        }

        perror("Falha ao conectar ao servidor. Tentando novamente...");
        close(*socket_fd);
        sleep(RETRY_WAIT_TIME);
        attempts++;
    }

    fprintf(stderr, "Falha após %d tentativas. Não foi possível conectar ao servidor.\n", MAX_RETRIES);
    return -1; // Falha após todas as tentativas
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

    

    // Tentativa de conexão com retry
    if (retry_connect(&socket_fd, &address, host_server) != 0)
    {
        exit(EXIT_FAILURE); // Finaliza se não conseguir conectar
    }

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
