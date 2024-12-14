import os
import random
import string

def generate_random_text_file(filename, size_in_kb):
    # Define o tamanho desejado em bytes
    size_in_bytes = size_in_kb * 1024

    # Gera conteúdo aleatório com letras
    with open(filename, 'w') as file:
        while file.tell() < size_in_bytes:
            # Escreve um bloco de texto aleatório no arquivo
            random_text = ''.join(random.choices(string.ascii_letters, k=1024))
            file.write(random_text[:size_in_bytes - file.tell()])

    print(f"Arquivo '{filename}' criado com sucesso com {os.path.getsize(filename)} bytes.")

# Nome do arquivo e tamanho desejado
output_file = "1MB.txt"
size_in_kb = 1

generate_random_text_file(output_file, size_in_kb)

