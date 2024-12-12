#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file_controller.h"

// Obtém o caminho absoluto de um arquivo a partir do diretório atual
int get_abs_path(char *file_path, char **abs_path, int verbose)
{
    // Obtém o diretório de trabalho atual
    char *cwd = getcwd(NULL, 0); // Aloca memória para armazenar o caminho atual
    if (cwd == NULL)
    {
        perror("Erro ao obter o diretório de trabalho atual."); 
        return -1;
    }

    // Calcula o tamanho necessário para armazenar o caminho absoluto
    size_t required_size = strlen(cwd) + strlen(file_path) + 2; // +2 para '/' e '\0'

    // Aloca memória para o caminho absoluto
    *abs_path = malloc(required_size);
    if (*abs_path == NULL)
    {
        perror("Erro ao alocar memória para o caminho absoluto."); 
        free(cwd);
        return -1;
    }

    //Cria o caminho absoluto
    snprintf(*abs_path, required_size, "%s/%s", cwd, file_path);

    // Exibe o caminho absoluto se o modo verboso estiver ativo
    verbose_printf(verbose, "Caminho absoluto: %s\n", *abs_path);

    free(cwd); // Libera a memória alocada para o caminho atual
    return 0;
}

// Cria o caminho de arquivo temporário com extensão ".part"
int get_part_file_path(char *file_path, char **file_path_with_part)
{
    // Calcula o tamanho necessário para armazenar o caminho com ".part"
    size_t required_size = strlen(file_path) + strlen(".part") + 1;

    // Aloca memória para o novo caminho
    *file_path_with_part = malloc(required_size);
    if (*file_path_with_part == NULL)
    {
        perror("Erro ao alocar memória para o caminho parcial."); 
        free(file_path_with_part);
        return -1;
    }

    // Constrói o caminho parcial
    snprintf(*file_path_with_part, required_size, "%s.part", file_path);
    return 0;
}

// Escreve os dados recebidos no arquivo parcial
int handle_write_part_file(char *buffer, int valread, message_t *message, int verbose)
{
    // Gera o caminho do arquivo parcial
    char *file_path_with_part;
    get_part_file_path(message->file_path, &file_path_with_part);

    // Abre o arquivo em modo de escrita no final do arquivo
    FILE *file = fopen(file_path_with_part, "a");
    if (file == NULL)
    {
        perror("Caminho do arquivo inválido."); 
        return -1;
    }

    // Escreve os dados recebidos no arquivo
    fprintf(file, "%s", buffer);
    fclose(file);

    // Verifica se o último caractere do buffer é o marcador de EOF
    if (buffer[valread - 1] == EOF_MARKER)
    {
        // Remove o marcador de EOF do arquivo parcial
        FILE *file = fopen(file_path_with_part, "r+");
        fseek(file, -1, SEEK_END);
        ftruncate(fileno(file), ftell(file)); // Trunca o arquivo para remover o EOF
        fclose(file);

        // Renomeia o arquivo parcial para o nome final
        verbose_printf(verbose, "Renomeando %s para %s...\n", file_path_with_part, message->file_path);
        rename(file_path_with_part, message->file_path);
        printf("Arquivo %s recebido com sucesso.\n", message->file_path); 
        free(file_path_with_part);
        return 1;
    }

    // Libera memória do caminho parcial
    free(file_path_with_part);
    return 0;
}

// Verifica se o arquivo existe
int file_exists(char *file_path)
{
    // Tenta abrir o arquivo para leitura
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0; // Retorna 0 se o arquivo não existe
    }
    fclose(file);
    return 1; // Retorna 1 se o arquivo existe
}

// Obtém o tamanho do arquivo em bytes
long get_size_file(char *file_path, int verbose)
{
    // Abre o arquivo para leitura
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        return 0; // Retorna 0 se o arquivo não existe
    }

    // Posiciona o ponteiro no final do arquivo para medir o tamanho
    fseek(file, 0, SEEK_END);
    long size = ftell(file); // Obtém o tamanho em bytes
    fclose(file);

    // Exibe o tamanho do arquivo no modo verboso
    verbose_printf(verbose, "Arquivo %s: %ld bytes\n", file_path, size);
    return size;
}