# Serviço de transferência de arquivos em rede

Este projeto implementa um serviço de transferência de arquivos em rede, permitindo enviar e baixar arquivos entre servidor e cliente.

## Compilar
```bash
make
```

## Executar
### Servidor
```bash
make run-server
```

### Cliente
#### Enviar arquivo do cliente para o servidor
```bash
./remcp (nome do arquivo a ser enviado) (id do servidor/maquina):(repositório onde o arquivo ficará salvo)
```
exemplo:
```bash
./remcp client_files/test_1024.txt 192.168.0.10:/home/bdsabmael/Desktop/Abmael/UFSC/Programacao-Paralela-e-Distribuída-INE5645/trabalho-2/INE5645-Trabalho2/server_files/test_1024.txt
```

#### Baixar arquivo do servidor
```bash
./remcp (id do servidor/maquina):(repositório do servidor onde está o arquivo) (repositório do cliente onde ficará salvo)
```
exemplo:
```bash
./remcp 192.168.0.10:/home/bdsabmael/Desktop/Abmael/UFSC/Programacao-Paralela-e-Distribuída-INE5645/trabalho-2/INE5645-Trabalho2/server_files/test_1024.txt client_files/test_1024.txt
```
