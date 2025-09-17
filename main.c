#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <protocol.h>

#define DIR_CHECK_INTERVAL 5 // Intervalo em segundos para checar o diretório sync
#define CURRENT_PEERS 3

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Execucao de peer incorreta. Uso correto: %s <meu_ip:porta> <arquivo_peers.conf>\n", argv[0]);
        return 1;
    }

    int _socket;
    struct sockaddr_in peer_address_list[MAX_PEERS];
    int num_peers = 0;
    struct sockaddr_in my_address;

    char* static_peers[MAX_PEERS] = {'192.168.1.100:8000', '192.168.1.101:8000', '192.168.1.102:8000'};

    char* local_ip_port = argv[1];

    // Itera a lista de peers estaticos para inicializar a lista de peers com os endereços IP e portas dos peers
    for (int i = 0; i < CURRENT_PEERS; i++) {
        char* ip = strtok(static_peers[i], ":");
        char* port = strtok(NULL, "\n");

        if(ip) {
            peer_address_list[num_peers].sin_family = AF_INET;
            peer_address_list[num_peers].sin_port = htons(atoi(port));
            inet_pton(AF_INET, ip, &peer_address_list[num_peers].sin_addr);

            // Verifica se esse peer é o peer local
            if (strcmp(local_ip_port, static_peers[i]) == 0) {
                 my_address = peer_address_list[num_peers];
            }

            num_peers++;
        }
    }

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

    return 0;
}
