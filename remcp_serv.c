#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>

#define PORT 8080
#define MAX_CONNECTIONS 5
#define CHUNK_SIZE 128
#define TRANSFER_RATE 256

typedef struct {
    int client_socket;
} ClientRequest;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int active_connections = 0;

long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void send_file(int client_socket, const char *remote_path, int client_transfer_rate) {
    char buffer[CHUNK_SIZE];
    size_t bytes_received;
    long total_received = 0;

    char *file_name = strrchr(remote_path, '/');
    if (!file_name) file_name = (char *)remote_path;
    else file_name++;

    char part_file_name[256];
    snprintf(part_file_name, sizeof(part_file_name), "%s.part", remote_path);

    FILE *part_file = fopen(part_file_name, "wb");
    if (!part_file) {
        perror("Erro ao criar arquivo .part");
        close(client_socket);
        return;
    }

    FILE *file = fopen(remote_path, "wb");
    if (!file) {
        perror("Erro ao criar arquivo final");
        fclose(part_file);
        close(client_socket);
        return;
    }

    long file_size;
    recv(client_socket, &file_size, sizeof(file_size), 0);
    long last_time = current_time_ms();

    while ((bytes_received = recv(client_socket, buffer, CHUNK_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, part_file);
        fwrite(buffer, 1, bytes_received, file);
        fflush(part_file);

        total_received += bytes_received;
        printf("Recebendo '%s': %ld bytes recebidos de %ld\n", file_name, total_received, file_size);

        int percent = (int)((total_received * 100) / file_size);
        printf("Progresso: %d%% concluído\n", percent);

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_received / client_transfer_rate) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        if (total_received >= file_size) break;
    }

    fclose(part_file);
    fclose(file);

    if (total_received == file_size) {
        rename(part_file_name, remote_path);
        printf("Recebimento de '%s' concluído.\n", file_name);
    } else {
        printf("Recebimento de '%s' interrompido.\n", file_name);
    }

    close(client_socket);
}

void *client_handler(void *arg) {
    ClientRequest *request = (ClientRequest *)arg;

    pthread_mutex_lock(&mutex);
    active_connections++;
    int client_transfer_rate = TRANSFER_RATE / active_connections;
    pthread_mutex_unlock(&mutex);

    char remote_path[256];
    recv(request->client_socket, remote_path, sizeof(remote_path), 0);
    send_file(request->client_socket, remote_path, client_transfer_rate);

    pthread_mutex_lock(&mutex);
    active_connections--;
    pthread_mutex_unlock(&mutex);

    free(request);
    return NULL;
}

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

        ClientRequest *request = malloc(sizeof(ClientRequest));
        request->client_socket = client_socket;

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, client_handler, request);
        pthread_detach(thread_id);
    }
    close(server_socket);
}

int main() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Erro ao criar daemon");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        printf("Servidor iniciado em background (PID: %d).\n", pid);
        exit(EXIT_SUCCESS);
    }

    setsid();
    chdir("/");
    fclose(stdout);
    fclose(stderr);
    fclose(stdin);

    start_server();
    return 0;
}
