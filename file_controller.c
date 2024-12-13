#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file_controller.h"

// obtem o caminho absoluto a partir do diretorio atual
int get_abs_path(char *file_path, char **abs_path, int verbose)
{
    char *cwd = getcwd(NULL, 0); // obtem o diretorio de trabalho atual
    if (cwd == NULL)
    {
        perror("Erro ao obter o diretório de trabalho atual."); 
        return -1;
    }

    size_t required_size = strlen(cwd) + strlen(file_path) + 2; // calcula o tamanho do caminho absoluto
    *abs_path = malloc(required_size); // aloca memoria para o caminho absoluto
    if (*abs_path == NULL)
    {
        perror("Erro ao alocar memória para o caminho absoluto."); 
        free(cwd);
        return -1;
    }

    snprintf(*abs_path, required_size, "%s/%s", cwd, file_path); // cria o caminho absoluto
    verbose_printf(verbose, "Caminho absoluto: %s\n", *abs_path); // exibe o caminho absoluto no modo verboso
    free(cwd); // libera a memoria do diretório de trabalho
    return 0;
}

// cria o caminho do arquivo temporário com extensão ".part"
int get_part_file_path(char *file_path, char **file_path_with_part)
{
    size_t required_size = strlen(file_path) + strlen(".part") + 1; // calcula o tamanho para o arquivo com .part
    *file_path_with_part = malloc(required_size); // aloca memoria para o caminho com .part
    if (*file_path_with_part == NULL)
    {
        perror("Erro ao alocar memória para o caminho parcial."); 
        free(file_path_with_part);
        return -1;
    }

    snprintf(*file_path_with_part, required_size, "%s.part", file_path); // cria o caminho parcial
    return 0;
}

// escreve os dados no arquivo temporário
int handle_write_part_file(char *buffer, int valread, message_t *message, int verbose)
{
    char *file_path_with_part;
    get_part_file_path(message->file_path, &file_path_with_part); // gera o caminho do arquivo parcial

    FILE *file = fopen(file_path_with_part, "a"); // abre o arquivo em modo de escrita
    if (file == NULL)
    {
        perror("Caminho do arquivo inválido."); 
        return -1;
    }

    fprintf(file, "%s", buffer); // escreve os dados no arquivo
    fclose(file);

    if (buffer[valread - 1] == EOF_MARKER) // verifica se o último caractere é o marcador de EOF
    {
        FILE *file = fopen(file_path_with_part, "r+");
        fseek(file, -1, SEEK_END); // posiciona o ponteiro no final do arquivo
        ftruncate(fileno(file), ftell(file)); // trunca o arquivo para remover o EOF
        fclose(file);

        verbose_printf(verbose, "Renomeando %s para %s...\n", file_path_with_part, message->file_path); 
        rename(file_path_with_part, message->file_path); // renomeia o arquivo para o nome final
        printf("Arquivo %s recebido com sucesso.\n", message->file_path); 
        free(file_path_with_part);
        return 1;
    }

    free(file_path_with_part); // libera a memoria do caminho do arquivo
    return 0;
}

// verifica se o arquivo existe
int file_exists(char *file_path)
{
    FILE *file = fopen(file_path, "r"); // tenta abrir o arquivo para leitura
    if (file == NULL)
    {
        return 0; // arquivo nao existe
    }
    fclose(file);
    return 1; // arquivo existe
}

// obtem o tamanho do arquivo
long get_size_file(char *file_path, int verbose)
{
    FILE *file = fopen(file_path, "r"); // abre o arquivo para leitura
    if (file == NULL)
    {
        return 0; // arquivo nao existe
    }

    fseek(file, 0, SEEK_END); // posiciona o ponteiro no final do arquivo
    long size = ftell(file); // obtem o tamanho do arquivo
    fclose(file);

    verbose_printf(verbose, "Arquivo %s: %ld bytes\n", file_path, size); // exibe o tamanho do arquivo no modo verboso
    return size;
}