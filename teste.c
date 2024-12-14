int send_file(int socket_fd, message_t *message, char *file_path_origin, int verbose)
{
    verbose_printf(verbose, "Enviando arquivo...\n");
    char *abs_path;

    // Obtém o caminho absoluto do arquivo
    if (get_abs_path(file_path_origin, &abs_path, verbose) == -1)
    {
        perror("Caminho do arquivo inválido.");
        return -1;
    }

    FILE *file = fopen(abs_path, "r");
    if (file == NULL)
    {
        perror("Arquivo não encontrado.");
        free(abs_path);
        return -1;
    }

    // Obtém o tamanho total do arquivo
    fseek(file, 0, SEEK_END);
    long total_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Variável para armazenar o offset enviado pelo cliente
    long offset = 0;
    if (message->buffer[0] != '\0') // Se o buffer contiver dados
    {
        offset = atol(message->buffer); // Converte o valor do buffer para o offset
        if (offset < 0 || offset > total_size)
        {
            perror("Offset inválido recebido do cliente.");
            fclose(file);
            free(abs_path);
            return -1;
        }
        // Ajusta o ponteiro do arquivo para o offset
        fseek(file, offset, SEEK_SET);
        verbose_printf(verbose, "Retomando envio a partir do offset: %ld bytes\n", offset);
    }

    // Variáveis para medir progresso e taxa de transmissão
    long bytes_sent = offset; // Inicializa com o offset
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);

    int eof = 0;
    while (fgets(message->buffer, BUFFER_SIZE, file) != NULL)
    {
        size_t len = strlen(message->buffer);

        // Adiciona o marcador de EOF se necessário
        if (len < BUFFER_SIZE - 1)
        {
            message->buffer[len] = EOF_MARKER;
            message->buffer[len + 1] = '\0';
            eof = 1;
        }

        // Envia o conteúdo do buffer
        send_message(socket_fd, message);
        bytes_sent += len;

        // Exibe taxa de transmissão e progresso se verboso
        if (verbose)
        {
            gettimeofday(&current_time, NULL);
            double elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                                  (current_time.tv_usec - start_time.tv_usec) / 1000000.0;

            if (elapsed_time > 0) // Evita divisão por zero
            {
                double rate = bytes_sent / elapsed_time; // Taxa de bytes por segundo
                double progress = (bytes_sent / (double)total_size) * 100; // Progresso em %
                printf("\rTaxa de transmissão: %.2f bytes/seg | Progresso: %.2f%%", rate, progress);
                fflush(stdout);
            }
        }
    }

    if (!eof)
    {
        // Envia EOF explicitamente se necessário
        char eof_marker = EOF;
        if (send(socket_fd, &eof_marker, 1, 0) == -1)
        {
            perror("Falha ao enviar EOF.");
        }
        verbose_printf(verbose, "\nEnviando EOF\n");
        handle_receive_message(socket_fd, message->buffer);
    }

    verbose_printf(verbose, "\nEnvio concluído. Total de bytes enviados: %ld\n", bytes_sent);

    fclose(file); // Fecha o arquivo
    free(abs_path); // Libera memória do caminho absoluto
    return 0;
}
