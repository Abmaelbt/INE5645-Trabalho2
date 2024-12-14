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

int MAX_CLIENTS = 2;   // Número máximo de clientes simultâneos
int MAX_THROTTLE = 300; // Taxa máxima de requisições permitidas
int THROTTLING_TIME = 100000; // Tempo de espera em caso de limitação (em microssegundos)

// upload ou download
int handle_buffer(char *buffer, int valread, int socket_fd, message_t *message)
{
    buffer[valread] = '\0';

    // Determina se é uma operação de upload ou download
    if (message->upload == -1)
    {
        message->upload = atoi(buffer);
        send(socket_fd, buffer, strlen(buffer), 0); //Confirmar para o cliente
    }
    else if (message->file_path == NULL)
    {
        message->file_path = strdup(buffer); //Caminho do arquivo enviado pelo cliente

        if (message->upload)
        {
            send_offset_size(socket_fd, message, message->file_path); //Total do arquivo
        }
        else
        {
            send(socket_fd, buffer, strlen(buffer), 0); //Conformar recebimento do tamanho do arquivo
        }
    }
    else if (message->upload)
    {
        if (handle_write_part_file(buffer, valread, message) == -1)
        {
            perror("Caminho do arquivo inválido."); 
            return 0;
        }
        send(socket_fd, buffer, strlen(buffer), 0); //Confirmação para o cliente
    }
    else
    {
        //Envia o arquivo solicitado pelo cliente
        if (send_file(socket_fd, message, message->file_path) == -1)
        {
            perror("Arquivo não encontrado."); 
            return 0;
        }
    }

    return 0;
}

//Função de controle de mensagens para clientes conectados
int handle_message_activity(message_t *message, struct pollfd *poolfd, int *client_count, int *request_count)
{
    int *socket_fd = &poolfd->fd;
    char *buffer = message->buffer;

    if (*socket_fd != -1 && (poolfd->revents & POLLIN))
    {
        int valread = read(*socket_fd, buffer, BUFFER_SIZE);
        if (valread == 0)
        {
            printf("Cliente no socket %d desconectado.\n", *socket_fd); 
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
            (*request_count)++; // Contador global
            message->request_count++; // Contador do cliente
        }

        return handle_buffer(buffer, valread, *socket_fd, message);
    }
    return 0;
}

//Reseta o contador de requisições a cada segundo
void reset_request_count(int *request_count, struct pollfd *poolfd, message_t *messages, int max_clients)
{
    static time_t last_time = 0;
    time_t current_time = time(NULL);

    if (current_time - last_time >= 1)
    {
        #pragma omp critical
        {
            printf("Taxa total de requisições por segundo: %d\n", *request_count); //Taxa de requisições total 

            //Taxa de requisições para cada socket
            for (int i = 1; i <= max_clients; i++)
            {
                if (poolfd[i].fd != -1) //Verifica se o socket está ativo
                {
                    printf("Socket %d: enviou %d requisições neste intervalo.\n",
                           poolfd[i].fd, messages[i].request_count);
                    messages[i].request_count = 0; //Reseta o contador por cliente
                }
            }

            *request_count = 0; //Reseta o contador global de requisições
        }
        last_time = current_time;
    }
}


void parse_arguments(int argc, char const *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--max-clients=", 14) == 0)
        {
            MAX_CLIENTS = atoi(argv[i] + 14); //Definir o máximo de clientes
        }
        else if (strncmp(argv[i], "--max-throttle=", 15) == 0)
        {
            MAX_THROTTLE = atoi(argv[i] + 15); //Definir o máximo de req
        }
        else if (strncmp(argv[i], "--throttling-time=", 18) == 0)
        {
            THROTTLING_TIME = atoi(argv[i] + 18); //Define o tempo de espera para limitação
        }
        else
        {
            fprintf(stderr, "Argumento desconhecido: %s\n", argv[i]); 
        }
    }
}

int main(int argc, char const *argv[])
{
    omp_set_nested(1); // Ativa suporte a threads aninhadas no OpenMP

    int socket_fd, new_socket;
    struct sockaddr_in address;
    struct pollfd poolfd[MAX_CLIENTS + 1];
    message_t messages[MAX_CLIENTS + 1];
    int addrlen = sizeof(address);
    int activity;
    int client_count = 0;
    int request_count = 0;

    printf("Uso: ./remcp_server [--max-clients=x] [--max-throttle=x] [--throttling-time=x]\n"); 
    parse_arguments(argc, argv); 

    create_socket(&socket_fd, &address, NULL); //Cria o main socket

    struct linger so_linger = {1, 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

    //Associar o socket à porta e endereço especificados
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Falha ao associar o socket."); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }


    if (listen(socket_fd, 3) < 0)
    {
        perror("Falha ao escutar no socket."); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    //Inicializar os descritores e buffers de clientes
    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        poolfd[i].fd = -1;
        messages[i].upload = -1;
        messages[i].file_path = NULL;
        messages[i].buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    }

    poolfd[0].fd = socket_fd; // Adiciona o socket principal ao pool
    poolfd[0].events = POLLIN;

    printf("Aguardando conexões...\n"); 

    while (1)
    {
        activity = poll(poolfd, MAX_CLIENTS + 1, -1); // Monitora atividade nos sockets

        if (activity < 0)
        {
            perror("Erro ao monitorar os sockets."); 
            close(socket_fd);
            exit(EXIT_FAILURE);
        }

        //Nova conexão
        if (poolfd[0].revents & POLLIN)
        {
            new_socket = accept(socket_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            printf("Clientes conectados: %d\n", client_count); 
            if (new_socket < 0)
            {
                perror("Falha ao aceitar conexão."); 
                continue;
            }

            //Verificar se o limite de clientes foi atingido
            if (client_count >= MAX_CLIENTS)
            {
                printf("Conexão rejeitada: Limite de clientes atingido.\n"); 
                close(new_socket);
            }
            else
            {
                client_count++;
                printf("Nova conexão aceita, socket fd: %d\n", new_socket); 
            }

            //Adicionar o novo socket ao pool
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

        //Processar a atividade de cada cliente
        #pragma omp parallel for schedule(static, 1)
        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            int break_flag = 0;
            #pragma omp critical
            if (request_count >= MAX_THROTTLE)
            {
                printf("Aplicando limitação...\n"); 
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
