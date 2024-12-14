import os
import random
import string

def generate_random_text_file(filename, size_in_kb):
    size_in_bytes = size_in_kb * 1024

    with open(filename, 'w') as file:
        while file.tell() < size_in_bytes:
            random_text = ''.join(random.choices(string.ascii_letters, k=1024))
            file.write(random_text[:size_in_bytes - file.tell()])

    print(f"Arquivo '{filename}' criado com sucesso com {os.path.getsize(filename)} bytes.")

output_file = "1MB.txt"
size_in_kb = 1

generate_random_text_file(output_file, size_in_kb)