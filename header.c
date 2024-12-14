#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "header.h"

// Obtém o caminho absoluto de um arquivo a partir do diretório atual
void create_socket(int *socket_fd, struct sockaddr_in *address, char *host_destination)
{
    struct timeval timeout;

    //Socket TCP
    if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Falha ao criar o socket.");
        exit(EXIT_FAILURE);
    }

    //Configura o endereço e a porta
    address->sin_family = AF_INET;
    address->sin_port = htons(PORT);
    address->sin_addr.s_addr = host_destination != NULL ? inet_addr(host_destination) : INADDR_ANY;

    timeout.tv_sec = 5;  // 5 segundos
    timeout.tv_usec = 0; // 0 microsegundos

    //Configura timeout para operações de recebimento
    if (setsockopt(*socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de recebimento.");
        close(*socket_fd);
    }

    //Configura timeout para operações de envio
    if (setsockopt(*socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Erro ao configurar o timeout de envio.");
        close(*socket_fd);
    }
}

void send_message(int socket_fd, message_t *message)
{
    // Envia o conteúdo do buffer
    if (send(socket_fd, message->buffer, strlen(message->buffer), 0) == -1)
    {
        perror("Falha ao enviar mensagem."); 
    }
    handle_receive_message(socket_fd, message->buffer);
}

void send_upload(int socket_fd, message_t *message)
{
    printf("Enviando sinal de upload...\n");
    char upload_char = message->upload ? '1' : '0'; // Define '1' para upload e '0' para download
    strncpy(message->buffer, &upload_char, 1); // Copia o sinal para o buffer
    message->buffer[1] = '\0'; // Finaliza o buffer com '\0'
    send_message(socket_fd, message); // Envia o sinal
}

void send_file_path(int socket_fd, message_t *message, char *file_path)
{
    printf("Enviando caminho do arquivo...\n");
    strncpy(message->buffer, file_path, BUFFER_SIZE - 1); // Copia o caminho para o buffer
    message->buffer[BUFFER_SIZE - 1] = '\0'; // Garante a terminação do buffer
    send_message(socket_fd, message); // Envia o buffer
}

void send_offset_size(int socket_fd, message_t *message, char *file_path)
{
    printf("Enviando tamanho de offset...\n");
    char *file_path_with_part;

    get_part_file_path(file_path, &file_path_with_part);

    long size = get_size_file(file_path_with_part);

    char size_str[8];
    sprintf(size_str, "%ld", size);

    strncpy(message->buffer, size_str, sizeof(size_str));
    message->buffer[sizeof(size_str)] = '\0';

    free(file_path_with_part); // Libera memória do caminho parcial
    send(socket_fd, message->buffer, strlen(message->buffer), 0); // Envia o buffer
}

// Envia um arquivo para o servidor
int send_file(int socket_fd, message_t *message, char *file_path_origin)
{
    printf("Enviando arquivo...\n");
    char *abs_path;

    // Obtém o caminho absoluto do arquivo
    if (get_abs_path(file_path_origin, &abs_path) == -1)
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

    fseek(file, 0, SEEK_END);
    long total_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    long offset = 0;
    if (message->buffer[0] != '\0') //Se o buffer contiver dados
    {
        offset = atol(message->buffer); //Converte o valor do buffer para o offset
        if (offset < 0 || offset > total_size)
        {
            perror("Offset inválido recebido do cliente.");
            fclose(file);
            free(abs_path);
            return -1;
        }
        fseek(file, offset, SEEK_SET);
        printf("Retomando envio a partir do offset: %ld bytes\n", offset);
    }

    //Variáveis para medir progresso e taxa de transmissão
    long bytes_sent = offset; 
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);

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

        send_message(socket_fd, message);
        bytes_sent += len;

        //Taxa de transm. e progresso
        gettimeofday(&current_time, NULL);
        double elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                              (current_time.tv_usec - start_time.tv_usec) / 1000000.0;

        if (elapsed_time > 0)
        {
            double rate = bytes_sent / elapsed_time; // Taxa de bytes por segundo
            double progress = (bytes_sent / (double)total_size) * 100; // Progresso em %
            printf("\rTaxa de transmissão: %.2f bytes/seg | Progresso: %.2f%%", rate, progress);
            fflush(stdout);
        }
    }

    if (!eof)
    {
        char eof_marker = EOF;
        if (send(socket_fd, &eof_marker, 1, 0) == -1)
        {
            perror("Falha ao enviar EOF.");
        }
        printf("\nEnviando EOF\n");
        handle_receive_message(socket_fd, message->buffer);
    }

    printf("\nEnvio concluído. Total de bytes enviados: %ld\n", bytes_sent);

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

// file_controller.c

int get_abs_path(char *file_path, char **abs_path)
{
    //Obtém o diretório de trabalho atual
    char *cwd = getcwd(NULL, 0); //Aloca memória para armazenar o caminho atual
    if (cwd == NULL)
    {
        perror("Erro ao obter o diretório de trabalho atual."); 
        return -1;
    }

    size_t required_size = strlen(cwd) + strlen(file_path) + 2; // +2 para '/' e '\0'

    //Aloca memória para o caminho absoluto
    *abs_path = malloc(required_size);
    if (*abs_path == NULL)
    {
        perror("Erro ao alocar memória para o caminho absoluto."); 
        free(cwd);
        return -1;
    }

    snprintf(*abs_path, required_size, "%s/%s", cwd, file_path);

    printf("Caminho absoluto: %s\n", *abs_path);

    free(cwd); // Libera a memória alocada para o caminho atual
    return 0;
}

// Cria o caminho de arquivo temporário com extensão ".part"
int get_part_file_path(char *file_path, char **file_path_with_part)
{
    //Calcula o tamanho necessário para armazenar o caminho com ".part"
    size_t required_size = strlen(file_path) + strlen(".part") + 1;

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

int handle_write_part_file(char *buffer, int valread, message_t *message)
{
    char *file_path_with_part;
    get_part_file_path(message->file_path, &file_path_with_part);

    //Abre o arquivo em modo de escrita no final do arquivo
    FILE *file = fopen(file_path_with_part, "a");
    if (file == NULL)
    {
        perror("Caminho do arquivo inválido."); 
        return -1;
    }

    fprintf(file, "%s", buffer);
    fclose(file);

    //Verifica se o último caractere do buffer é o marcador de EOF
    if (buffer[valread - 1] == EOF_MARKER)
    {
        FILE *file = fopen(file_path_with_part, "r+");
        fseek(file, -1, SEEK_END);
        ftruncate(fileno(file), ftell(file));
        fclose(file);

        //Renomeia o arquivo parcial para o nome final
        printf("Renomeando %s para %s...\n", file_path_with_part, message->file_path);
        rename(file_path_with_part, message->file_path);
        printf("Arquivo %s recebido com sucesso.\n", message->file_path); 
        free(file_path_with_part);
        return 1;
    }

    free(file_path_with_part);
    return 0;
}

int file_exists(char *file_path)
{
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0;
    }
    fclose(file);
    return 1; 
}

long get_size_file(char *file_path)
{
    // Abre o arquivo para leitura
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0;
    }
    
    // Posiciona o ponteiro no final do arquivo para medir o tamanho
    fseek(file, 0, SEEK_END);
    long size = ftell(file); // Obtém o tamanho em bytes
    fclose(file);

    printf("Arquivo %s: %ld bytes\n", file_path, size);
    return size;
}