#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <protocol.h>

#define DIR_CHECK_INTERVAL 5 // Intervalo em segundos para checar o diretório sync
#define CURRENT_PEERS 3

// Thread Servidor: Escuta por mensagens
void* server_thread_func(int* _socket, struct sockaddr_in* my_address) {
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

// Thread Cliente: Verifica o diretório por mudanças ---
void* client_thread_func(void* arg) {
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
        char* ip = strtok(line, ":");
        char* port = strtok(NULL, "\n");
    
        if(ip) {
            peer_address_list[num_peers].sin_family = AF_INET;
            peer_address_list[num_peers].sin_port = htons(atoi(port));
            inet_pton(AF_INET, ip, &peer_address_list[num_peers].sin_addr);
    
            // Verifica se esse peer é o peer local
            if (strcmp(local_ip_port, line) == 0) {
                my_address = peer_address_list[num_peers];
            }
    
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
    pthread_create(&server_tid, NULL, server_thread_func(&_socket, &my_address), NULL);
    pthread_create(&client_tid, NULL, client_thread_func, NULL);

    // Espera as threads terminarem (quando "exit" for enviado)
    pthread_join(server_tid, NULL);
    pthread_join(client_tid, NULL);

    close(_socket);

    return 0;
}
