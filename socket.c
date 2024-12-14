#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include "socket.h"
#include "file_controller.h"

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

