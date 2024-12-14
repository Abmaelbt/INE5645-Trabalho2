#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "header.h"

// Obtém o caminho absoluto de um arquivo a partir do diretório atual
int get_abs_path(char *file_path, char **abs_path, int verbose)
{
    // Obtém o diretório de trabalho atual
    char *cwd = getcwd(NULL, 0); // Aloca memória para armazenar o caminho atual
    if (cwd == NULL)
    {
        perror("Erro ao obter o diretório de trabalho atual."); 
        return -1;
    }

    // Calcula o tamanho necessário para armazenar o caminho absoluto
    size_t required_size = strlen(cwd) + strlen(file_path) + 2; // +2 para '/' e '\0'

    // Aloca memória para o caminho absoluto
    *abs_path = malloc(required_size);
    if (*abs_path == NULL)
    {
        perror("Erro ao alocar memória para o caminho absoluto."); 
        free(cwd);
        return -1;
    }

    //Cria o caminho absoluto
    snprintf(*abs_path, required_size, "%s/%s", cwd, file_path);

    // Exibe o caminho absoluto se o modo verboso estiver ativo
    verbose_printf(verbose, "Caminho absoluto: %s\n", *abs_path);

    free(cwd); // Libera a memória alocada para o caminho atual
    return 0;
}

// Cria o caminho de arquivo temporário com extensão ".part"
int get_part_file_path(char *file_path, char **file_path_with_part)
{
    // Calcula o tamanho necessário para armazenar o caminho com ".part"
    size_t required_size = strlen(file_path) + strlen(".part") + 1;

    // Aloca memória para o novo caminho
    *file_path_with_part = malloc(required_size);
    if (*file_path_with_part == NULL)
    {
        perror("Erro ao alocar memória para o caminho parcial."); 
        free(file_path_with_part);
        return -1;
    }

    // Constrói o caminho parcial
    snprintf(*file_path_with_part, required_size, "%s.part", file_path);
    return 0;
}

// Escreve os dados recebidos no arquivo parcial
int handle_write_part_file(char *buffer, int valread, message_t *message, int verbose)
{
    // Gera o caminho do arquivo parcial
    char *file_path_with_part;
    get_part_file_path(message->file_path, &file_path_with_part);

    // Abre o arquivo em modo de escrita no final do arquivo
    FILE *file = fopen(file_path_with_part, "a");
    if (file == NULL)
    {
        perror("Caminho do arquivo inválido."); 
        return -1;
    }

    // Escreve os dados recebidos no arquivo
    fprintf(file, "%s", buffer);
    fclose(file);

    // Verifica se o último caractere do buffer é o marcador de EOF
    if (buffer[valread - 1] == EOF_MARKER)
    {
        // Remove o marcador de EOF do arquivo parcial
        FILE *file = fopen(file_path_with_part, "r+");
        fseek(file, -1, SEEK_END);
        ftruncate(fileno(file), ftell(file)); // Trunca o arquivo para remover o EOF
        fclose(file);

        // Renomeia o arquivo parcial para o nome final
        verbose_printf(verbose, "Renomeando %s para %s...\n", file_path_with_part, message->file_path);
        rename(file_path_with_part, message->file_path);
        printf("Arquivo %s recebido com sucesso.\n", message->file_path); 
        free(file_path_with_part);
        return 1;
    }

    // Libera memória do caminho parcial
    free(file_path_with_part);
    return 0;
}

// Verifica se o arquivo existe
int file_exists(char *file_path)
{
    // Tenta abrir o arquivo para leitura
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0; // Retorna 0 se o arquivo não existe
    }
    fclose(file);
    return 1; // Retorna 1 se o arquivo existe
}

// Obtém o tamanho do arquivo em bytes
long get_size_file(char *file_path, int verbose)
{
    // Abre o arquivo para leitura
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0; // Retorna 0 se o arquivo não existe
    }

    // Posiciona o ponteiro no final do arquivo para medir o tamanho
    fseek(file, 0, SEEK_END);
    long size = ftell(file); // Obtém o tamanho em bytes
    fclose(file);

    // Exibe o tamanho do arquivo no modo verboso
    verbose_printf(verbose, "Arquivo %s: %ld bytes\n", file_path, size);
    return size;
}


void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination)
{
    struct timeval timeout;

    // Criação do socket com protocolo TCP
    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Falha ao criar o socket.");
        exit(EXIT_FAILURE);
    }

    // Configura o endereço e a porta
    address->sin_family = AF_INET;
    address->sin_port = htons(PORT);
    address->sin_addr.s_addr = host_destination != NULL ? inet_addr(host_destination) : INADDR_ANY;

    // Define timeout para receber dados
    timeout.tv_sec = 5;  // 5 segundos
    timeout.tv_usec = 0; // 0 microsegundos

    // Configura timeout para operações de recebimento
    if (setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de recebimento.");
        close(*socket_fd);
    }

    // Configura timeout para operações de envio
    if (setsockopt(*socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de envio.");
        close(*socket_fd);
    }
}

// Envia uma mensagem pelo socket
void send_message(int socket_fd, message_t *message)
{
    // Envia o conteúdo do buffer
    if (send(socket_fd, message->buffer, strlen(message->buffer), 0) == -1)
    {
        perror("Falha ao enviar mensagem."); 
    }
    // Aguarda e processa a resposta do servidor
    handle_receive_message(socket_fd, message->buffer);
}

// Envia um sinal indicando se é upload (1) ou download (0)
void send_upload(int socket_fd, message_t *message, int verbose)
{
    verbose_printf(verbose, "Enviando sinal de upload...\n");
    char upload_char = message->upload ? '1' : '0'; // Define '1' para upload e '0' para download
    strncpy(message->buffer, &upload_char, 1); // Copia o sinal para o buffer
    message->buffer[1] = '\0'; // Finaliza o buffer com '\0'
    send_message(socket_fd, message); // Envia o sinal
}

// Envia o caminho do arquivo
void send_file_path(int socket_fd, message_t *message, char *file_path, int verbose)
{
    verbose_printf(verbose, "Enviando caminho do arquivo...\n");
    strncpy(message->buffer, file_path, BUFFER_SIZE - 1); // Copia o caminho para o buffer
    message->buffer[BUFFER_SIZE - 1] = '\0'; // Garante a terminação do buffer
    send_message(socket_fd, message); // Envia o buffer
}

// Envia o tamanho já transferido (offset) de um arquivo
void send_offset_size(int socket_fd, message_t *message, char *file_path, int verbose)
{
    verbose_printf(verbose, "Enviando tamanho de offset...\n");
    char *file_path_with_part;

    // Gera o caminho do arquivo parcial
    get_part_file_path(file_path, &file_path_with_part);

    // Obtém o tamanho do arquivo parcial
    long size = get_size_file(file_path_with_part, verbose);

    // Converte o tamanho em string
    char size_str[8];
    sprintf(size_str, "%ld", size);

    // Copia o tamanho para o buffer
    strncpy(message->buffer, size_str, sizeof(size_str));
    message->buffer[sizeof(size_str)] = '\0';

    free(file_path_with_part); // Libera memória do caminho parcial
    send(socket_fd, message->buffer, strlen(message->buffer), 0); // Envia o buffer
}

// Envia um arquivo para o servidor
int send_file(int socket_fd, message_t *message, char *file_path_origin, int verbose)
{
    verbose_printf(verbose, "Enviando arquivo...\n");
    char *abs_path;

    // Obtém o caminho absoluto do arquivo
    if (get_abs_path(file_path_origin, &abs_path, verbose) == -1)
    {
        perror("Caminho do arquivo inválido.");
        return -1;
    }

    FILE *file = fopen(abs_path, "r");
    fseek(file, 0, SEEK_END);
    long size = ftell(file); // Obtém o tamanho do arquivo
    fseek(file, 0, SEEK_SET);

    if (file == NULL)
    {
        perror("Arquivo não encontrado.");
        return -1;
    }

    // Retoma o envio a partir do offset, se necessário
    if (strcmp(message->buffer, file_path_origin) != 0)
    {
        verbose_printf(verbose, "Retomando do offset de %s bytes\n", message->buffer);
        fseek(file, atoi(message->buffer), SEEK_SET);
    }

    int eof = 0;
    while (fgets(message->buffer, BUFFER_SIZE, file) != NULL)
    {
        size_t len = strlen(message->buffer);

        // Adiciona o marcador de EOF se necessário
        if (len < BUFFER_SIZE - 1)
        {
            message->buffer[len] = EOF_MARKER;
            message->buffer[len + 1] = '\0';
            eof = 1;
        }

        // Envia o conteúdo do buffer
        send_message(socket_fd, message);

        // Exibe progresso, se verboso
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

    // Envia EOF explicitamente se não enviado
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

    fclose(file); // Fecha o arquivo
    free(abs_path); // Libera memória do caminho absoluto
    return 0;
}

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

// Exibe mensagens se o modo verboso estiver ativado
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