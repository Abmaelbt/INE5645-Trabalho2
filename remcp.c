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
        printf("Arquivo '%s' removido com sucesso.\n", file_path);
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

    char resolved_path[512]; // Buffer para evitar ponteiro inválido

    // Determinar nome do arquivo, mesmo sem estar especificado no destino
    if (stat(local_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        recv(socket, filename, sizeof(filename), 0);
        snprintf(resolved_path, sizeof(resolved_path), "%s/%s", local_path, filename);
        local_path = resolved_path;
    } else {
        extract_filename(local_path, filename);
    }

    FILE *file = NULL;
    char part_file_name[512]; // Aumentado para evitar truncamento
    snprintf(part_file_name, sizeof(part_file_name), "%s.part", local_path);

    file = fopen(part_file_name, "wb");
    if (!file) {
        perror("Erro ao criar arquivo .part para download");
        send(socket, "ERROR: Cannot create .part file", strlen("ERROR: Cannot create .part file") + 1, 0);
        return;
    }

    char buffer[TRANSFER_RATE];
    size_t bytes_received;
    long file_size = 0;
    long total_received = 0;

    // Receber tamanho do arquivo
    if (recv(socket, &file_size, sizeof(file_size), 0) <= 0) {
        perror("Erro ao receber tamanho do arquivo");
        fclose(file);
        return;
    }

    if (file_size <= 0) {
        fprintf(stderr, "Tamanho do arquivo inválido: %ld\n", file_size);
        fclose(file);
        return;
    }

    // Receber o conteúdo do arquivo
    while ((bytes_received = recv(socket, buffer, TRANSFER_RATE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        total_received += bytes_received;

        if (total_received % CHUNK_SIZE == 0) {
            fflush(file);
        }

        printf("Recebendo '%s': %ld bytes recebidos de %ld\n", filename, total_received, file_size);

        if (total_received >= file_size) break;
    }

    fclose(file);

    if (total_received == file_size) {
        rename(part_file_name, local_path);
        printf("Recebimento de '%s' concluído.\n", filename);
    } else {
        printf("Erro: Recebimento de '%s' interrompido.\n", filename);
        delete_file(part_file_name); // Remover arquivo parcial em caso de erro
    }
}

void upload_file(int socket, const char *file_path, const char *remote_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo para envio");
        send(socket, "ERROR: File not found", strlen("ERROR: File not found") + 1, 0);
        return;
    }

    send(socket, remote_path, strlen(remote_path) + 1, 0);

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    send(socket, &file_size, sizeof(file_size), 0);

    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    long bytes_sent = 0;

    long last_time = current_time_ms();

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        if (send(socket, buffer, bytes_read, 0) == -1) {
            perror("Erro ao enviar dados");
            fclose(file);
            return;
        }

        bytes_sent += bytes_read;

        long current_time = current_time_ms();
        long elapsed_time = current_time - last_time;
        long sleep_time = (1000 * bytes_read / TRANSFER_RATE) - elapsed_time;

        if (sleep_time > 0) usleep(sleep_time * 1000);
        last_time = current_time;

        printf("Enviando '%s': %ld bytes enviados de %ld\n", file_path, bytes_sent, file_size);

        if (bytes_sent >= file_size) break;
    }

    fclose(file);

    char confirmation[256];
    if (recv(socket, confirmation, sizeof(confirmation), 0) > 0) {
        if (strcmp(confirmation, "SUCCESS") == 0) {
            printf("Envio de '%s' concluído. Excluindo arquivo local...\n", file_path);
            delete_file(file_path);
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