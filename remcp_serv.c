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
#define TRANSFER_RATE 256 // bytes por segundo

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

void delete_file(const char *file_path) {
    if (remove(file_path) == 0) {
        printf("Arquivo '%s' removido com sucesso.\n", file_path);
    } else {
        perror("Erro ao remover arquivo");
    }
}

void extract_filename(const char *path, char *filename) {
    char *base = strrchr(path, '/');
    if (base) {
        strcpy(filename, base + 1);
    } else {
        strcpy(filename, path);
    }
}

void send_file_to_client(int client_socket, const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo para envio");
        send(client_socket, "ERROR: File not found", strlen("ERROR: File not found") + 1, 0);
        return;
    }

    char filename[256];
    extract_filename(file_path, filename);
    send(client_socket, filename, strlen(filename) + 1, 0);

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    send(client_socket, &file_size, sizeof(file_size), 0);

    char buffer[TRANSFER_RATE];
    size_t bytes_read;
    long total_sent = 0;
    long last_time = current_time_ms();

    while ((bytes_read = fread(buffer, 1, TRANSFER_RATE, file)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) == -1) {
            perror("Erro ao enviar dados");
            break;
        }

        total_sent += bytes_read;

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_read / TRANSFER_RATE) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        if (total_sent >= file_size) break;
    }

    fclose(file);

    if (total_sent == file_size) {
        printf("Envio de '%s' concluído.\n", file_path);
        send(client_socket, "SUCCESS", strlen("SUCCESS") + 1, 0);
    } else {
        printf("Erro: envio de '%s' incompleto.\n", file_path);
        send(client_socket, "ERROR: Transfer incomplete", strlen("ERROR: Transfer incomplete") + 1, 0);
    }
}

void receive_file_from_client(int client_socket, const char *file_path, int client_transfer_rate) {
    char buffer[CHUNK_SIZE];
    size_t bytes_received;
    long total_received = 0;

    char part_file_name[256];
    snprintf(part_file_name, sizeof(part_file_name), "%s.part", file_path);

    FILE *part_file = fopen(part_file_name, "wb");
    if (!part_file) {
        perror("Erro ao criar arquivo .part");
        send(client_socket, "ERROR: Cannot create .part file", strlen("ERROR: Cannot create .part file") + 1, 0);
        close(client_socket);
        return;
    }

    long file_size;
    if (recv(client_socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("Erro ao receber tamanho do arquivo");
        fclose(part_file);
        send(client_socket, "ERROR: Failed to receive file size", strlen("ERROR: Failed to receive file size") + 1, 0);
        close(client_socket);
        return;
    }

    if (file_size <= 0) {
        fprintf(stderr, "Tamanho do arquivo inválido: %ld\n", file_size);
        fclose(part_file);
        send(client_socket, "ERROR: Invalid file size", strlen("ERROR: Invalid file size") + 1, 0);
        close(client_socket);
        return;
    }

    long last_time = current_time_ms();

    while ((bytes_received = recv(client_socket, buffer, CHUNK_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, part_file);
        total_received += bytes_received;

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_received / client_transfer_rate) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        printf("Recebendo arquivo: %ld bytes recebidos de %ld\n", total_received, file_size);

        if (total_received >= file_size) break;
    }

    fclose(part_file);

    if (total_received == file_size) {
        if (rename(part_file_name, file_path) == 0) {
            printf("Recebimento de '%s' concluído.\n", file_path);
            send(client_socket, "SUCCESS", strlen("SUCCESS") + 1, 0);
        } else {
            perror("Erro ao renomear arquivo");
            send(client_socket, "ERROR: Cannot rename file", strlen("ERROR: Cannot rename file") + 1, 0);
        }
    } else {
        printf("Recebimento de '%s' interrompido.\n", file_path);
        delete_file(part_file_name);
        send(client_socket, "ERROR: Transfer incomplete", strlen("ERROR: Transfer incomplete") + 1, 0);
    }
}

void *client_handler(void *arg) {
    ClientRequest *request = (ClientRequest *)arg;

    pthread_mutex_lock(&mutex);
    active_connections++;
    int client_transfer_rate = TRANSFER_RATE / active_connections;
    pthread_mutex_unlock(&mutex);

    char request_path[256];
    if (recv(request->client_socket, request_path, sizeof(request_path), 0) > 0) {
        if (access(request_path, F_OK) == 0) {
            // Arquivo existe: enviar para o cliente
            send_file_to_client(request->client_socket, request_path);
        } else {
            // Arquivo não existe: receber do cliente
            receive_file_from_client(request->client_socket, request_path, client_transfer_rate);
        }
    }

    pthread_mutex_lock(&mutex);
    active_connections--;
    pthread_mutex_unlock(&mutex);

    close(request->client_socket);
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