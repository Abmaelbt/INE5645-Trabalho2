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
#include "file_controller.h"
#include "socket.h"


// TODO incorporar essa função dentro da main
// encerra processos que utilizam a porta especificada
void kill_process_on_port(int port)
{
    char command[128];

#ifdef __linux__
    snprintf(command, sizeof(command), "fuser -k %d/tcp", port); 
#elif __APPLE__
    snprintf(command, sizeof(command), "lsof -ti:%d | xargs kill -9", port); 
#elif _WIN32
    snprintf(command, sizeof(command), "netstat -ano | findstr :%d > pid.txt && for /f \"tokens=5\" %%a in (pid.txt) do taskkill /PID %%a /F", port); 
#else
    fprintf(stderr, "sistema operacional não suportado\n"); 
    return;
#endif

    if (system(command) == -1)
    {
        perror("falha ao finalizar processo na porta"); 
    }
    else
    {
        printf("servidor rodando na porta %d \n", port); 
    }
}

int verbose = 0;       
int MAX_CLIENTS = 5;   
int MAX_THROTTLE = 300; 
int THROTTLING_TIME = 100000; 

// processa mensagens recebidas e realiza upload ou download de arquivos
int handle_buffer(char *buffer, int valread, int socket_fd, message_t *message, int verbose)
{
    buffer[valread] = '\0'; 

    if (message->upload == -1)
    {
        message->upload = atoi(buffer); 
        send(socket_fd, buffer, strlen(buffer), 0); 
    }
    else if (message->file_path == NULL)
    {
        message->file_path = strdup(buffer); 

        if (message->upload)
        {
            send_offset_size(socket_fd, message, message->file_path, verbose); 
        }
        else
        {
            send(socket_fd, buffer, strlen(buffer), 0); 
        }
    }
    else if (message->upload)
    {
        if (handle_write_part_file(buffer, valread, message, verbose) == -1)
        {
            perror("caminho do arquivo inválido"); 
            return 0;
        }
        send(socket_fd, buffer, strlen(buffer), 0); 
    }
    else
    {
        if (send_file(socket_fd, message, message->file_path, verbose) == -1)
        {
            perror("arquivo não encontrado"); 
            return 0;
        }
    }

    return 0;
}

// gerencia a atividade de mensagens de clientes conectados
int handle_message_activity(message_t *message, struct pollfd *poolfd, int *client_count, int *request_count)
{
    int *socket_fd = &poolfd->fd;
    char *buffer = message->buffer;

    if (*socket_fd != -1 && (poolfd->revents & POLLIN))
    {
        int valread = read(*socket_fd, buffer, BUFFER_SIZE); 
        if (valread == 0)
        {
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
        (*request_count)++;
        return handle_buffer(buffer, valread, *socket_fd, message, verbose);
    }
    return 0;
}

// reseta o contador de requisições periodicamente
void reset_request_count(int *request_count)
{
    static time_t last_time = 0;
    time_t current_time = time(NULL);
    if (current_time - last_time >= 1)
    {
        #pragma omp critical
        {
            *request_count = 0; 
        }
        last_time = current_time;
    }
}

// processa os argumentos da linha de comando para configuração do servidor
void parse_arguments(int argc, char const *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0)
        {
            verbose = 1; 
        }
        else if (strncmp(argv[i], "--max-clients=", 14) == 0)
        {
            MAX_CLIENTS = atoi(argv[i] + 14); 
        }
        else if (strncmp(argv[i], "--max-throttle=", 15) == 0)
        {
            MAX_THROTTLE = atoi(argv[i] + 15); 
        }
        else if (strncmp(argv[i], "--throttling-time=", 18) == 0)
        {
            THROTTLING_TIME = atoi(argv[i] + 18); 
        }
        else
        {
            fprintf(stderr, "argumento desconhecido: %s\n", argv[i]); 
        }
    }
}

// função principal do servidor
int main(int argc, char const *argv[])
{
    kill_process_on_port(PORT); 
    omp_set_nested(1); 

    int socket_fd, new_socket;
    struct sockaddr_in address;
    struct pollfd poolfd[MAX_CLIENTS + 1];
    message_t messages[MAX_CLIENTS + 1];
    int addrlen = sizeof(address);
    int activity;
    int client_count = 0;
    int request_count = 0;

    parse_arguments(argc, argv); 
    create_socket(&socket_fd, &address, NULL); 

    struct linger so_linger = {1, 0}; 
    setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("falha ao associar o socket"); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, 3) < 0)
    {
        perror("falha ao escutar no socket"); 
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i <= MAX_CLIENTS; i++)
    {
        poolfd[i].fd = -1;
        messages[i].upload = -1;
        messages[i].file_path = NULL;
        messages[i].buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    }

    poolfd[0].fd = socket_fd; 
    poolfd[0].events = POLLIN;

    printf("Aguardando conexões...\n");

    while (1)
    {
        activity = poll(poolfd, MAX_CLIENTS + 1, -1); 

        if (activity < 0)
        {
            perror("erro ao monitorar os sockets"); 
            close(socket_fd);
            exit(EXIT_FAILURE);
        }

        if (poolfd[0].revents & POLLIN)
        {
            new_socket = accept(socket_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (new_socket < 0)
            {
                perror("falha ao aceitar conexão"); 
                continue;
            }

            if (client_count >= MAX_CLIENTS)
            {
                close(new_socket);
            }
            else
            {
                client_count++;
                verbose_printf(verbose, "Nova conexão aceita, socket fd: %d\n", new_socket);
            }

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

        reset_request_count(&request_count); 

        #pragma omp parallel for schedule(static, 1)
        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            int break_flag = 0;
            #pragma omp critical
            if (request_count >= MAX_THROTTLE) 
            {
                verbose_printf(verbose, "aplicando limitação...\n");
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