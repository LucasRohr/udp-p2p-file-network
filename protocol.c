#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

#include "protocol.h"

const char* SYNC_DIR = "tmp/sync";

// Processa uma mensagem recebida
void handle_message(int sockfd, const UDPMessage* message, const struct sockaddr_in* sender_address) {
    printf("Mensagem recebida: Endereço/Porta %s:%d - Tipo: %d\n", 
           inet_ntoa(sender_address->sin_addr), ntohs(sender_address->sin_port), message->type);

    switch (message->type) {
        case LIST_REQUEST:
            printf("Enviando lista de arquivos...\n");
            send_file_list(sockfd, sender_address);
            break;

        case LIST_RESPONSE:
            printf("Payload: %s\n", message->payload);
            // Lógica para comparar a lista recebida com a local e pedir arquivos faltantes
            // Aqui você implementaria a comparação e chamaria request_file() se necessário
            break;

        case FILE_REQUEST:
            printf("Arquivo requisitado: %s\n", message->payload);
            send_file_chunks(sockfd, message->payload, sender_address);
            break;

        case FILE_RESPONSE_CHUNK:
        case FILE_RESPONSE_END:
            // Lógica para remontar o arquivo
            char file_path[512];
            
            // Abre o arquivo no modo de apêndice binário ('ab')
            sprintf(file_path, "%s/%s", SYNC_DIR, "arquivo_recebido_temp.tmp"); // Nome temporário
            FILE *file = fopen(file_path, "ab");
            if (file) {
                fwrite(message->payload, 1, strlen(message->payload), file);
                fclose(file);
            }
            if (message->type == FILE_RESPONSE_END) {
                printf("Transferência do arquivo finalizada.\n");
                // Renomear arquivo temporário para o nome final
            }
            
            break;

        case UPDATE_ADD:
            printf("Solicitando arquivo %s...\n", message->payload);
            request_file(sockfd, message->payload, sender_address);
            break;

        case UPDATE_REMOVE:
            printf("Removendo arquivo %s localmente...\n", message->payload);
            char file_to_remove[512];

            sprintf(file_to_remove, "%s/%s", SYNC_DIR, message->payload);

            if (remove(file_to_remove) == 0) {
                printf("Arquivo %s removido com sucesso.\n", message->payload);
            } else {
                perror("Erro ao remover arquivo.");
            }

            break;

        default:
            printf("Tipo de mensagem desconhecido: %d\n", message->type);
            break;
    }
}

// Envia um arquivo segmentado em partes
void send_file(int sockfd, const char* filename, const struct sockaddr_in* dest_address) {

}

// Recebe segmentos e remonta o arquivo
void receive_file(const char* filename, const UDPMessage* message) {

}

// Envia a lista de arquivos locais para um peer
void send_file_list(int sockfd, const struct sockaddr_in* dest_address) {

}