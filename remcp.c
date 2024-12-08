#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define CHUNK_SIZE 128
#define TRANSFER_RATE 256 // depois ajustar para pegar do server
#define RETRY_LIMIT 5
#define RETRY_DELAY 2

void delete_file(const char *file_path) {
    if (remove(file_path) == 0) {
        printf("Arquivo '%s' removido com sucesso.\n");
    } else {
        perror("Erro ao remover arquivo");
    }
}

long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void extract_filename(const char *path, char *filename) {
    char *base = strrchr(path, '/');
    if (base) {
        strcpy(filename, base + 1);
    } else {
        strcpy(filename, path);
    }
}

void download_file(int socket, const char *local_path) {
    char filename[256];
    struct stat st;

    char resolved_path[512];
    if (stat(local_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        recv(socket, filename, sizeof(filename), 0);
        snprintf(resolved_path, sizeof(resolved_path), "%s/%s", local_path, filename);
        local_path = resolved_path;
    } else {
        extract_filename(local_path, filename);
    }

    char part_file_name[512];
    snprintf(part_file_name, sizeof(part_file_name), "%s.part", local_path);

    FILE *file = fopen(part_file_name, "ab+");
    if (!file) {
        perror("Erro ao criar/abrir arquivo .part para download");
        send(socket, "ERROR: Cannot create .part file", strlen("ERROR: Cannot create .part file") + 1, 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    long local_offset = ftell(file);
    send(socket, &local_offset, sizeof(local_offset), 0);

    long file_size = 0;
    if (recv(socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("Erro ao receber tamanho do arquivo");
        fclose(file);
        return;
    }

    printf("Recebendo '%s': retomando de %ld bytes de %ld\n", filename, local_offset, file_size);

    char buffer[TRANSFER_RATE];
    size_t bytes_received;
    long total_received = local_offset;
    long last_time = current_time_ms();

    while ((bytes_received = recv(socket, buffer, TRANSFER_RATE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_received / TRANSFER_RATE) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        printf("Recebendo '%s': %ld bytes recebidos de %ld\n", filename, total_received, file_size);

        if (total_received >= file_size) break;
    }

    fclose(file);

    if (total_received == file_size) {
        rename(part_file_name, local_path);
        printf("Recebimento de '%s' concluído.\n", filename);
        send(socket, "SUCCESS", strlen("SUCCESS") + 1, 0);
    } else {
        printf("Recebimento de '%s' interrompido.\n", filename);
        send(socket, "ERROR: Transfer incomplete", strlen("ERROR: Transfer incomplete") + 1, 0);
    }
}

void upload_file(int socket, const char *file_path, const char *remote_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo para envio");
        send(socket, "ERROR: File not found", strlen("ERROR: File not found") + 1, 0);
        return;
    }

    // Extract filename from the source file
    char filename[256];
    extract_filename(file_path, filename);

    // Determine if remote_path is a directory
    struct stat st;
    char resolved_remote_path[512];
    if (stat(remote_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        // If remote_path is a directory, append the filename
        snprintf(resolved_remote_path, sizeof(resolved_remote_path), "%s/%s", remote_path, filename);
    } else {
        // Use remote_path as-is
        strncpy(resolved_remote_path, remote_path, sizeof(resolved_remote_path) - 1);
        resolved_remote_path[sizeof(resolved_remote_path) - 1] = '\0';
    }

    send(socket, resolved_remote_path, strlen(resolved_remote_path) + 1, 0);

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Send file size
    send(socket, &file_size, sizeof(file_size), 0);

    // Receive offset for resuming transfer
    long total_sent = 0;
    if (recv(socket, &total_sent, sizeof(total_sent), 0) <= 0) {
        perror("Erro ao receber offset do servidor");
        fclose(file);
        return;
    }

    printf("Enviando '%s': retomando a partir de %ld bytes\n", file_path, total_sent);

    fseek(file, total_sent, SEEK_SET);

    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    long last_time = current_time_ms();

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        if (send(socket, buffer, bytes_read, 0) == -1) {
            perror("Erro ao enviar dados");
            fclose(file);
            return;
        }

        total_sent += bytes_read;

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_read / TRANSFER_RATE) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        printf("Enviando '%s': %ld bytes enviados de %ld\n", file_path, total_sent, file_size);

        if (total_sent >= file_size) break;
    }

    fclose(file);

    char confirmation[256];
    if (recv(socket, confirmation, sizeof(confirmation), 0) > 0) {
        if (strcmp(confirmation, "SUCCESS") == 0) {
            printf("Envio de '%s' concluído.\n", file_path);
        } else {
            printf("Erro do servidor: %s\n", confirmation);
        }
    } else {
        perror("Erro ao receber confirmação do servidor");
    }
}


int connect_to_server(const char *ip) {
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Erro ao criar socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Endereço IP inválido");
        return -1;
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao conectar ao servidor");
        close(client_socket);
        return -1;
    }

    return client_socket;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <ORIGEM> <DESTINO>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *source = argv[1];
    char *dest = argv[2];

    char *colon = strchr(source, ':');
    if (colon) {
        // Download
        *colon = '\0';
        char *ip = source;
        char *remote_path = colon + 1;

        int client_socket = connect_to_server(ip);
        if (client_socket != -1) {
            send(client_socket, remote_path, strlen(remote_path) + 1, 0);
            download_file(client_socket, dest);
            close(client_socket);
        }
    } else {
        // Upload
        colon = strchr(dest, ':');
        if (!colon) {
            fprintf(stderr, "Erro: Formato do destino inválido. Use <IP>:<CAMINHO>\n");
            exit(EXIT_FAILURE);
        }

        *colon = '\0';
        char *ip = dest;
        char *remote_path = colon + 1;

        int client_socket = connect_to_server(ip);
        if (client_socket != -1) {
            upload_file(client_socket, source, remote_path);
            close(client_socket);
        }
    }

    return 0;
}