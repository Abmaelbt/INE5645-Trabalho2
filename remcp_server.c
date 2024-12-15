#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <omp.h>
#include <time.h>
#include "header.h"

int MAX_CLIENTS = 1;   // numero maximo de clientes simultaneos
int MAX_THROTTLE = 300; // taxa maxima de requisicoes permitidas
int THROTTLING_TIME = 100000; // tempo de espera em caso de limitacao (em microssegundos)

int client_handler(char *buffer, int valread, int socket_fd, message_t *message)
{
    buffer[valread] = '\0';

    // determina se download ou upload
    if (message->upload == -1)
    {
        message->upload = atoi(buffer);
        send(socket_fd, buffer, strlen(buffer), 0); // confirmar para o cliente
    }
    else if (message->file_path == NULL)
    {
        message->file_path = strdup(buffer); // caminho do arquivo enviado pelo cliente

        if (message->upload)
        {
            send_offset_size(socket_fd, message, message->file_path); // total do arquivo
        }
        else
        {
            send(socket_fd, buffer, strlen(buffer), 0); // confirmar recebimento do tamanho do arquivo
        }
    }
    else if (message->upload)
    {
        if (handle_write_part_file(buffer, valread, message) == -1)
        {
            perror("caminho do arquivo invalido."); 
            return 0;
        }
        send(socket_fd, buffer, strlen(buffer), 0); // confirmacao para o cliente
    }
    else
    {
        // envia o arquivo solicitado pelo cliente
        if (send_file(socket_fd, message, message->file_path) == -1)
        {
            perror("arquivo nao encontrado."); 
            return 0;
        }
    }

    return 0;
}

// controla as mensagens para os clientes conectados
int handle_message_activity(message_t *message, struct pollfd *poolfd, int *client_count, int *request_count)
{
    int *socket_fd = &poolfd->fd;
    char *buffer = message->buffer;

    if (*socket_fd != -1 && (poolfd->revents & POLLIN))
    {
        int valread = read(*socket_fd, buffer, BUFFER_SIZE);
        if (valread == 0)
        {
            printf("cliente no socket %d desconectado.\n", *socket_fd); 
            close(*socket_fd);
            *socket_fd = -1;
            message->upload = -1;
            message->file_path = NULL;
            memset(message->buffer, 0, BUFFER_SIZE);

            #pragma omp critical
            (*client_count)--;
            return 0;
        }

        #pragma omp critical
        {
            (*request_count)++; // contador global
            message->request_count++; // contador do cliente
        }

        return client_handler(buffer, valread, *socket_fd, message);
    }
    return 0;
}

// reseta o contador de requisicoes a cada segundo
void reset_request_count(int *request_count, struct pollfd *poolfd, message_t *messages, int max_clients)
{
    static time_t last_time = 0;
    time_t current_time = time(NULL);

    if (current_time - last_time >= 1)
    {
        #pragma omp critical
        {
            printf("taxa total de requisicoes por segundo: %d\n", *request_count); // taxa de requisicoes total 

            // taxa de requisicoes para cada socket
            for (int i = 1; i <= max_clients; i++)
            {
                if (poolfd[i].fd != -1) // verifica se o socket esta ativo
                {
                    printf("socket %d: enviou %d requisicoes neste intervalo.\n",
                           poolfd[i].fd, messages[i].request_count);
                    messages[i].request_count = 0; // reseta o contador por cliente
                }
            }

            *request_count = 0; // reseta o contador global de requisicoes
        }
        last_time = current_time;
    }
}


int main(int argc, char const *argv[])
{
    omp_set_nested(1); // ativa suporte a threads aninhadas no openmp

    int socket_fd, new_socket;
    struct sockaddr_in address;
    struct pollfd poolfd[MAX_CLIENTS + 1];
    message_t messages[MAX_CLIENTS + 1];
    int addrlen = sizeof(address);
    int activity;
    int client_count = 0;
    int request_count = 0;


    define_socket(&socket_fd, &address, NULL); // cria o socket principal
    struct linger so_linger = {1, 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

    // associa o socket ao endereco ip e porta
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("falha ao associar o socket."); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, 3) < 0)
    {
        perror("falha ao escutar no socket."); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // inicializar os descritores e buffers de clientes
    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        poolfd[i].fd = -1;
        messages[i].upload = -1;
        messages[i].file_path = NULL;
        messages[i].buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    }

    poolfd[0].fd = socket_fd; // adiciona o socket principal ao pool
    poolfd[0].events = POLLIN;

    printf("aguardando conexoes...\n"); 

    while (1)
    {
        activity = poll(poolfd, MAX_CLIENTS + 1, -1); // monitora atividade nos sockets

        if (activity < 0)
        {
            perror("erro ao monitorar os sockets."); 
            close(socket_fd);
            exit(EXIT_FAILURE);
        }

        // nova conexao
        if (poolfd[0].revents & POLLIN)
        {
            new_socket = accept(socket_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            printf("clientes conectados: %d\n", client_count); 
            if (new_socket < 0)
            {
                perror("falha ao aceitar conexao."); 
                continue;
            }

            // verificar se o limite de clientes foi atingido
            if (client_count >= MAX_CLIENTS)
            {
                printf("conexao rejeitada: limite de clientes atingido.\n"); 
                close(new_socket);
            }
            else
            {
                client_count++;
                printf("nova conexao aceita, socket fd: %d\n", new_socket); 
            }

            // adicionar o novo socket ao pool
            for (int i = 1; i <= MAX_CLIENTS; i++)
            {
                if (poolfd[i].fd == -1)
                {
                    poolfd[i].fd = new_socket;
                    poolfd[i].events = POLLIN;
                    break;
                }
            }
        }

        reset_request_count(&request_count, poolfd, messages, MAX_CLIENTS);

        // processar a atividade de cada cliente
        #pragma omp parallel for schedule(static, 1)
        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            int break_flag = 0;
            #pragma omp critical
            if (request_count >= MAX_THROTTLE)
            {
                printf("aplicando limitacao...\n"); 
                usleep(THROTTLING_TIME);
                break_flag = 1;
            }
            if (!break_flag)
            {
                if (handle_message_activity(&messages[i], &poolfd[i], &client_count, &request_count) == -1)
                {
                    close(socket_fd);
                    exit(EXIT_SUCCESS);
                }
            }
        }
    }

    close(socket_fd);
    return 0;
}