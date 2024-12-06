#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define CHUNK_SIZE 128
#define RETRY_LIMIT 5 // numero de tentativas pare reconexão
#define RETRY_DELAY 2

void upload_file(int socket, const char *file_path, const char *remote_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo para envio");
        send(socket, "ERROR: File not found", 21, 0);
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

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0) {
        send(socket, buffer, bytes_read, 0);
        bytes_sent += bytes_read;

        printf("Enviando '%s': %ld bytes enviados de %ld\n", file_path, bytes_sent, file_size);

        int percent = (int)((bytes_sent * 100) / file_size);
        printf("Progresso: %d%% concluído\n", percent);
    }

    fclose(file);
    printf("Envio de '%s' concluído.\n", file_path);
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
        fprintf(stderr, "Uso: %s <ARQUIVO_ORIGEM> <DESTINO_REMOTO>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *file_path = argv[1];
    char *remote_path = argv[2];

    char *colon = strchr(remote_path, ':');
    if (!colon) {
        fprintf(stderr, "Erro: Formato do destino inválido. Use <IP>:<CAMINHO>\n");
        exit(EXIT_FAILURE);
    }

    *colon = '\0'; // separa o ip do caminho remoto
    char *ip = remote_path;
    char *path = colon + 1;

    int attempt = 0;
    while (attempt < RETRY_LIMIT) {
        int client_socket = connect_to_server(ip);
        if (client_socket != -1) {
            upload_file(client_socket, file_path, path);
            close(client_socket);
            break;
        }

        attempt++;
        if (attempt < RETRY_LIMIT) {
            printf("Tentativa de reconexão (%d/%d)...\n", attempt, RETRY_LIMIT);
            sleep(RETRY_DELAY);
        } else {
            fprintf(stderr, "Falha ao conectar ao servidor após %d tentativas.\n", RETRY_LIMIT);
        }
    }

    return 0;
}