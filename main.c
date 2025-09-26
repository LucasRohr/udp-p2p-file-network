#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>

#include "protocol.h"

#define DIR_CHECK_INTERVAL 5 // Intervalo em segundos para checar o diretório sync
#define CURRENT_PEERS 3

const char* SYNC_DIRC = "Documents/sync";

// Struct para os args da thread do servidor
typedef struct {
    int* socket_fd;
    struct sockaddr_in* my_address;
} ServerThreadArgs;

// Struct para os args da thread do cliente
typedef struct {
    int* socket_fd;
    struct sockaddr_in* peer_list;
    int num_peers;
} ClientThreadArgs;

// Thread Servidor: Escuta por mensagens
void* server_thread_func(void* args) {
    // Extrai parametros dos argumentos via struct
    ServerThreadArgs* server_args = (ServerThreadArgs*)args;

    int* _socket = server_args->socket_fd;
    struct sockaddr_in* my_address = server_args->my_address;

    // Variaveis locais da thread
    UDPMessage received_message;
    struct sockaddr_in sender_address;
    socklen_t sender_address_len = sizeof(sender_address);
    int running = 0;

    printf("Thread do servidor iniciada. Escutando por mensagens...\n");

    while (running == 0) {
        ssize_t bytes_received = recvfrom(*_socket, &received_message, sizeof(received_message), 0,
                                         (struct sockaddr *)&sender_address, &sender_address_len);

        // Se recebeu exit, encerra thread servidora
        if (strncmp(received_message.payload, "exit", 4) == 0) {
            running = 0;
            break;
        }

        if (bytes_received > 0) {
            // Compara para não processar as próprias mensagens de broadcast
            if (sender_address.sin_addr.s_addr == my_address->sin_addr.s_addr && sender_address.sin_port == my_address->sin_port) {
                continue;
            }

            handle_message(*_socket, &received_message, bytes_received, &sender_address); // Chama auxiliar
        }
    }

    return NULL;
}

// Thread Cliente: Verifica o diretório por mudanças
void* client_thread_func(void* args) {
    // Extrai parametros dos argumentos via struct
    ClientThreadArgs* client_args = (ClientThreadArgs*)args;

    int* _socket = client_args->socket_fd;
    struct sockaddr_in* peer_address_list = client_args->peer_list;
    int num_peers = client_args->num_peers;

    // Variaveis locais da thread
    char known_files[MAX_FILES][MAX_FILENAME_LEN];
    int num_known_files = 0;

    // Preenche o estado inicial dos arquivos
    DIR *directory = opendir(SYNC_DIRC); // Abre dir

    struct dirent *direntry;

    if (directory) {
        while ((direntry = readdir(directory)) != NULL) {
            if (strcmp(direntry->d_name, ".") != 0 && strcmp(direntry->d_name, "..") != 0) { // Ignora os diretórios '.' e '..'
                strncpy(known_files[num_known_files++], direntry->d_name, MAX_FILENAME_LEN - 1); // Incrementa known files e copia nome do arquivo
            }
        }

        closedir(directory); // Fecha dir
    }

    printf("Thread cliente iniciada. Estado inicial com %d arquivos.\n", num_known_files);

    while (1) {
        sleep(DIR_CHECK_INTERVAL); // Aguarda tempo de chacagem

        char current_files[MAX_FILES][MAX_FILENAME_LEN];
        int num_current_files = 0;
        
        directory = opendir(SYNC_DIRC);

        if (!directory) {
            perror("Cliente: Não foi possível abrir o diretório de sincronização");
            continue;
        }

        while ((direntry = readdir(directory)) != NULL) {
             if (strcmp(direntry->d_name, ".") != 0 && strcmp(direntry->d_name, "..") != 0) { // Ignora os diretórios '.' e '..'
                strncpy(current_files[num_current_files++], direntry->d_name, MAX_FILENAME_LEN - 1); // Atualiza arquivos atuais na checagem
            }
        }

        closedir(directory);

        // Verifica por arquivos ADICIONADOS
        for (int i = 0; i < num_current_files; i++) {
            int found = 0;

            // Verifica se tem algum arquivo nos currents que nao eh conhecido, quer dizer que encontrou um novo
            for (int j = 0; j < num_known_files; j++) {
                if (strcmp(current_files[i], known_files[j]) == 0) {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                printf("Cliente - Novo arquivo detectado: %s. Notificando peers...\n", current_files[i]);
                broadcast_update(*_socket, current_files[i], UPDATE_ADD, peer_address_list, num_peers);
            }
        }

        // Verifica por arquivos REMOVIDOS
        for (int i = 0; i < num_known_files; i++) {
            int found = 0;

            // Verifica se tem um arquivo conhecido que nao estah mais nos current, quer dizer que foi removido
            for (int j = 0; j < num_current_files; j++) {
                if (strcmp(known_files[i], current_files[j]) == 0) {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                printf("Cliente - Arquivo removido detectado: %s. Notificando peers...\n", known_files[i]);
                broadcast_update(*_socket, known_files[i], UPDATE_REMOVE, peer_address_list, num_peers);
            }
        }

        // Atualiza a lista de arquivos conhecidos com a lista de arquivos atuais, para a próxima verificação
        num_known_files = num_current_files;

        for(int i = 0; i < num_current_files; i++) {
            strcpy(known_files[i], current_files[i]);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Execucao de peer incorreta. Uso correto: %s <meu_ip:porta> <arquivo_peers.conf>\n", argv[0]);
        return 1;
    }

    int _socket;
    struct sockaddr_in peer_address_list[MAX_PEERS];
    int num_peers = 0;
    struct sockaddr_in my_address;

    char* local_ip_port = argv[1];
    char* peers_file = argv[2];

    FILE* fp = fopen(peers_file, "r");

    if (!fp) {
        perror("Não foi possível abrir o arquivo de peers");
        return 1;
    }

    // Itera o arquivo de peers estaticos para inicializar a lista de peers com os endereços IP e portas dos peers
    char line[100];
    while (fgets(line, sizeof(line), fp) && num_peers < MAX_PEERS) {
        // Remove o \n do final da linha lida para uma comparação limpa
        line[strcspn(line, "\n")] = 0;

        // Compara com a linha completa antes de usar strtok (funcao altera string da linha)
        if (strcmp(local_ip_port, line) == 0) {
            // Se achou, preenche os dados do my_address
            char* ip_copy = strtok(strdup(line), ":"); // Usa uma cópia para strtok
            char* port_copy = strtok(NULL, "\n");
            
            my_address.sin_family = AF_INET;
            my_address.sin_port = htons(atoi(port_copy));
            inet_pton(AF_INET, ip_copy, &my_address.sin_addr);
            free(ip_copy); // Libera a memória da cópia
        }

        char* ip = strtok(line, ":");
        char* port = strtok(NULL, "\n");
    
        if(ip && port) {
            peer_address_list[num_peers].sin_family = AF_INET;
            peer_address_list[num_peers].sin_port = htons(atoi(port));
            inet_pton(AF_INET, ip, &peer_address_list[num_peers].sin_addr);
    
            num_peers++;
        }
    }

    fclose(fp); // Fecha arquivo de peers

    // Configuracao do socket UDP
    _socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (_socket < 0) {
        printf("Erro na criacao do Socket\n");
    }

    if (bind(_socket, (struct sockaddr*)&my_address, sizeof(my_address)) < 0) {
        perror("Erro no bind");
        close(_socket);

        return 1;
    }

    printf("Socket UDP criado e vinculado ao endereco/porta: %s.\n", local_ip_port);

    // Inicializa o gerenciador de transferências para segmentos
    initialize_transfers();

    // Cria as threads
    pthread_t server_tid, client_tid;

    // Declara e inicializa args para a thread do servidor
    ServerThreadArgs server_args;
    server_args.socket_fd = &_socket;
    server_args.my_address = &my_address;

    // Declara e inicializa args para a thread do client
    ClientThreadArgs client_args;
    client_args.socket_fd = &_socket;
    client_args.peer_list = peer_address_list;
    client_args.num_peers = num_peers;


    pthread_create(&server_tid, NULL, server_thread_func, &server_args); // Cria thread do servidor passando args
    pthread_create(&client_tid, NULL, client_thread_func, &client_args); // Cria thread do cliente passando args

    // Espera as threads terminarem (quando "exit" for enviado)
    pthread_join(server_tid, NULL);
    pthread_join(client_tid, NULL);

    close(_socket);

    return 0;
}
