#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define CHUNK_SIZE 128

void download_file(int socket, const char *file_name) {
    FILE *file = fopen(file_name, "wb");
    if (!file) {
        perror("Erro ao criar arquivo");
        return;
    }

    char buffer[CHUNK_SIZE];
    ssize_t bytes_received;
    long bytes_written = 0;

    // Receber o tamanho total do arquivo
    long file_size;
    recv(socket, &file_size, sizeof(file_size), 0);

    while ((bytes_received = recv(socket, buffer, CHUNK_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes_received, file);
        bytes_written += bytes_received;
        int percent = (int)((bytes_written * 100) / file_size);
        printf("Baixando '%s': %d%% concluído\n", file_name, percent);
    }

    fclose(file);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <IP> <arquivo_origem> <arquivo_destino>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    const char *source_file = argv[2];
    const char *dest_file = argv[3];

    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Endereço inválido");
        exit(EXIT_FAILURE);
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erro ao conectar ao servidor");
        exit(EXIT_FAILURE);
    }

    send(client_socket, source_file, strlen(source_file) + 1, 0);
    download_file(client_socket, dest_file);

    close(client_socket);
    return 0;
}
