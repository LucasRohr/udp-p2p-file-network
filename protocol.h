#ifndef PROTOCOL_H
#define PROTOCOL_H


#define MAX_TRANSFERS 5 // O peer pode baixar até 5 arquivos simultaneamente

#include "peer.h"

// Struct para gerenciar o estado de um download ativo para casos de segmentação
typedef struct {
    int active; // 0 = inativo, 1 = ativo
    struct sockaddr_in peer_address;
    char filename[MAX_FILENAME_LEN];
    char temp_filename[MAX_FILENAME_LEN + 5];
    FILE* file_ptr; // Ponteiro para o arquivo temp
    unsigned int last_seq_num;
} ActiveTransfer;

// Inicializa array de transfers
void initialize_transfers();

// Envia um arquivo segmentado em partes
void send_file_chunks(int sockfd, const char* filename, const struct sockaddr_in* dest_address);

// Requisita um arquivo
void request_file(int sockfd, const char* filename, const struct sockaddr_in* dest_address);

// Envia a lista de arquivos locais para um peer
void send_file_list(int sockfd, const struct sockaddr_in* dest_address);

// Processa uma mensagem recebida
void handle_message(int sockfd, const UDPMessage* message, ssize_t bytes_received, const struct sockaddr_in* sender_address);

// Atualiza os peers com updates no diretório de arquivos
void broadcast_update(int sockfd, const char* filename, MessageType type, const struct sockaddr_in peers[], int num_peers);

#endif // PROTOCOL_H
