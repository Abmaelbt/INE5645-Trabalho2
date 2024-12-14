#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "file_controller.h"
#include "socket.h"

// Analisa os argumentos de entrada para separar host e caminho do arquivo
void parse_arguments(const char *arg, char **host, char **file_path)
{
    char *colon = strchr(arg, ':');
    if (colon)
    {
        // Caso o argumento tenha host especificado, separa host e caminho
        *host = strndup(arg, colon - arg);
        *file_path = strdup(colon + 1);
    }
    else
    {
        // Caso não tenha host, usa o localhost por padrão
        *host = "127.0.0.1";
        *file_path = strdup(arg);
    }
}

// Gerencia o recebimento de arquivos do servidor
int receive_file(int socket_fd, message_t *message)
{
    printf("Recebendo arquivo...\n");
    while (1)
    {
        // Lê dados recebidos do servidor
        int valread = handle_receive_message(socket_fd, message->buffer);

        // Escreve a parte recebida no arquivo temporário
        int result = handle_write_part_file(message->buffer, valread, message);

        // Finaliza o recebimento se o arquivo estiver completo
        if (result != 0)
        {
            return result;
        }
        // Envia uma confirmação para o servidor
        send(socket_fd, message->buffer, strlen(message->buffer), 0);
    }
}

int main(int argc, char const *argv[])
{
    // Verifica o número de argumentos passados
    if (argc < 3 || argc > 4)
    {
        printf("Uso: ./client [host:]caminho_origem [host:]caminho_destino\n");
        return 1;
    }

    // Variáveis para armazenar informações dos caminhos e servidores
    char *host_origin = NULL;
    char *file_path_origin = NULL;
    char *host_destination = NULL;
    char *file_path_destination = NULL;
    char *host_server = NULL;
    int upload = 0;

    // Analisa os argumentos fornecidos
    parse_arguments(argv[1], &host_origin, &file_path_origin);
    parse_arguments(argv[2], &host_destination, &file_path_destination);

    // Determina se a operação é um upload
    upload = strcmp(host_origin, "127.0.0.1") == 0;

    // Define o servidor de destino
    host_server = upload ? host_destination : host_origin;

    int socket_fd;
    struct sockaddr_in address;

    // Cria e configura o socket
    create_socket(&socket_fd, &address, host_server);

    // Estabelece conexão com o servidor
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Falha ao conectar ao servidor.");
        exit(EXIT_FAILURE);
    }

    printf("Conexão estabelecida com o servidor...\n");

    // Aloca memória para a estrutura de mensagem
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

    // Fecha o socket e libera memória alocada
    close(socket_fd);
    free(message->buffer);
    free(message);

    printf("Operação concluída.\n");

    return 0;
}
