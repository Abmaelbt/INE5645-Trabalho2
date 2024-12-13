#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include "socket.h"
#include "file_controller.h"

// cria e configura o socket tcp com timeout para envio e recebimento
void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination)
{
    struct timeval timeout;

    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Falha ao criar o socket.");
        exit(EXIT_FAILURE);
    }

    address->sin_family = AF_INET;
    address->sin_port = htons(PORT);
    address->sin_addr.s_addr = host_destination != NULL ? inet_addr(host_destination) : INADDR_ANY;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de recebimento.");
        close(*socket_fd);
    }

    if (setsockopt(*socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de envio.");
        close(*socket_fd);
    }
}

// envia o conteúdo do buffer e aguarda resposta do servidor
void send_message(int socket_fd, message_t *message)
{
    if (send(socket_fd, message->buffer, strlen(message->buffer), 0) == -1)
    {
        perror("Falha ao enviar mensagem.");
    }
    handle_receive_message(socket_fd, message->buffer);
}

// envia sinal indicando upload ou download
void send_upload(int socket_fd, message_t *message, int verbose)
{
    verbose_printf(verbose, "Enviando sinal de upload...\n");
    char upload_char = message->upload ? '1' : '0';
    strncpy(message->buffer, &upload_char, 1);
    message->buffer[1] = '\0';
    send_message(socket_fd, message);
}

// envia o caminho do arquivo
void send_file_path(int socket_fd, message_t *message, char *file_path, int verbose)
{
    verbose_printf(verbose, "Enviando caminho do arquivo...\n");
    strncpy(message->buffer, file_path, BUFFER_SIZE - 1);
    message->buffer[BUFFER_SIZE - 1] = '\0';
    send_message(socket_fd, message);
}

// envia o tamanho já transferido do arquivo
void send_offset_size(int socket_fd, message_t *message, char *file_path, int verbose)
{
    verbose_printf(verbose, "Enviando tamanho de offset...\n");
    char *file_path_with_part;

    get_part_file_path(file_path, &file_path_with_part);

    long size = get_size_file(file_path_with_part, verbose);

    char size_str[8];
    sprintf(size_str, "%ld", size);

    strncpy(message->buffer, size_str, sizeof(size_str));
    message->buffer[sizeof(size_str)] = '\0';

    free(file_path_with_part);
    send(socket_fd, message->buffer, strlen(message->buffer), 0);
}

// envia arquivo para o servidor
int send_file(int socket_fd, message_t *message, char *file_path_origin, int verbose)
{
    verbose_printf(verbose, "Enviando arquivo...\n");
    char *abs_path;

    if (get_abs_path(file_path_origin, &abs_path, verbose) == -1)
    {
        perror("Caminho do arquivo inválido.");
        return -1;
    }

    FILE *file = fopen(abs_path, "r");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file == NULL)
    {
        perror("Arquivo não encontrado.");
        return -1;
    }

    if (strcmp(message->buffer, file_path_origin) != 0)
    {
        verbose_printf(verbose, "Retomando do offset de %s bytes\n", message->buffer);
        fseek(file, atoi(message->buffer), SEEK_SET);
    }

    int eof = 0;
    while (fgets(message->buffer, BUFFER_SIZE, file) != NULL)
    {
        size_t len = strlen(message->buffer);

        if (len < BUFFER_SIZE - 1)
        {
            message->buffer[len] = EOF_MARKER;
            message->buffer[len + 1] = '\0';
            eof = 1;
        }

        send_message(socket_fd, message);

        if (verbose)
        {
            static time_t last_time = 0;
            time_t current_time = time(NULL);
            if (current_time != last_time)
            {
                printf("%.2f%%\n", (ftell(file) / (double)size) * 100);
                last_time = current_time;
                fflush(stdout);
            }
        }
    }

    if (!eof)
    {
        char eof_marker = EOF;
        if (send(socket_fd, &eof_marker, 1, 0) == -1)
        {
            perror("Falha ao enviar mensagem.");
        }
        verbose_printf(verbose, "Enviando EOF\n");
        handle_receive_message(socket_fd, message->buffer);
    }

    verbose_printf(verbose, "100.00%%\n");

    fclose(file);
    free(abs_path);
    return 0;
}

// processa mensagem recebida do servidor e verifica erros comuns
int handle_receive_message(int socket_fd, char *buffer)
{
    int valread = recv(socket_fd, buffer, BUFFER_SIZE, 0);
    if (valread == 0)
    {
        perror("Conexão com o servidor encerrada.");
        return -1;
    }
    else if (valread < 0)
    {
        perror("Falha ao receber mensagem.");
        return -1;
    }

    buffer[valread] = '\0';
    if (strcmp(buffer, FILE_EXISTS_EXCEPTION) == 1)
    {
        perror("Arquivo já existe.");
        return -1;
    }

    if (strcmp(buffer, FILE_NOT_FOUND_EXCEPTION) == 1)
    {
        perror("Arquivo não encontrado.");
        return -1;
    }
    return valread;
}

// exibe mensagens se o modo verboso estiver ativado
void verbose_printf(int verbose, const char *format, ...)
{
    if (verbose)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}