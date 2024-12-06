#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define PORT 8080
#define MAX_CONNECTIONS 5
#define CHUNK_SIZE 128
#define TRANSFER_RATE 256 // bytes por segundo

typedef struct {
    int client_socket;
    char *file_name;
} ClientRequest;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // Prevenir condições de corrida
int active_connections = 0;

// Função para gerenciar transferência com feedback
void send_file(int client_socket, const char *file_name) {
    FILE *file = fopen(file_name, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo");
        send(client_socket, "ERROR: File not found\n", 23, 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    long bytes_sent = 0;

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) == -1) {
            perror("Erro na transferência");
            break;
        }
        bytes_sent += bytes_read;
        int percent = (int)((bytes_sent * 100) / file_size);
        printf("Enviando '%s': %d%% concluído\n", file_name, percent);

        usleep(1000000 * CHUNK_SIZE / TRANSFER_RATE); // Throttling
    }

    fclose(file);
    close(client_socket);
    printf("Transferência de '%s' concluída.\n", file_name);
}

// Função para cada cliente
void *client_handler(void *arg) {
    ClientRequest *request = (ClientRequest *)arg;

    pthread_mutex_lock(&mutex);
    active_connections++;
    pthread_mutex_unlock(&mutex);

    send_file(request->client_socket, request->file_name);

    pthread_mutex_lock(&mutex);
    active_connections--;
    pthread_mutex_unlock(&mutex);

    free(request);
    return NULL;
}

// Servidor principal
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao vincular socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CONNECTIONS) == -1) {
        perror("Erro ao escutar");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor iniciado na porta %d\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket == -1) {
            perror("Erro ao aceitar conexão");
            continue;
        }

        pthread_mutex_lock(&mutex);
        if (active_connections >= MAX_CONNECTIONS) {
            pthread_mutex_unlock(&mutex);
            send(client_socket, "ERROR: Server busy\n", 20, 0);
            close(client_socket);
            continue;
        }
        pthread_mutex_unlock(&mutex);

        char file_name[256];
        recv(client_socket, file_name, sizeof(file_name), 0);

        ClientRequest *request = malloc(sizeof(ClientRequest));
        request->client_socket = client_socket;
        request->file_name = strdup(file_name);

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, client_handler, request);
        pthread_detach(thread_id);
    }
    close(server_socket);
}

int main() {
    start_server();
    return 0;
}
