#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>

#include "protocol.h"

const char* SYNC_DIR = "tmp/sync";

ActiveTransfer transfers[MAX_TRANSFERS];

// Inicializa array de transfers
void initialize_transfers() {
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        transfers[i].active = 0;
    }
}

// Processa uma mensagem recebida
void handle_message(int sockfd, const UDPMessage* message, ssize_t bytes_received, const struct sockaddr_in* sender_address) {
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
            {
                // Se for o primeiro pacote, ele cria o registro da transferência
                if (message->sequence_number == 0) {
                    int transfer_index = -1;

                    // Acha um slot livre para a nova transferência
                    for (int i = 0; i < MAX_TRANSFERS; i++) {
                        if (transfers[i].active == 0) {
                            transfer_index = i;
                            break;
                        }
                    }

                    if (transfer_index == -1) {
                        fprintf(stderr, "Não há slots de transferência disponíveis.\n");
                        break; // Ignora o pacote
                    }
                    
                    // Configura o novo registro de transferência
                    ActiveTransfer* new_transfer = &transfers[transfer_index];
                    new_transfer->active = 1;
                    new_transfer->peer_address = *sender_address;
                    new_transfer->last_seq_num = 0;
                    
                    // O payload tem "nome_do_arquivo.txt\0dados_do_arquivo..."
                    // Copia o nome do arquivo
                    strncpy(new_transfer->filename, message->payload, MAX_FILENAME_LEN - 1);
                    new_transfer->filename[MAX_FILENAME_LEN - 1] = '\0';

                    printf("Iniciando recebimento do arquivo: %s\n", new_transfer->filename);

                    // Cria um nome de arquivo temporário
                    snprintf(new_transfer->temp_filename, sizeof(new_transfer->temp_filename),
                            "%s/%s.part", SYNC_DIR, new_transfer->filename);

                    // Abre o arquivo temporário para escrita binária ("wb")
                    new_transfer->file_ptr = fopen(new_transfer->temp_filename, "wb");

                    if (!new_transfer->file_ptr) {
                        perror("Falha ao criar arquivo temporário");
                        new_transfer->active = 0; // Libera o slot
                        break;
                    }

                    // Escreve os dados que vieram junto com o nome no primeiro pacote
                    size_t filename_len = strlen(new_transfer->filename) + 1; // +1 para o '\0'

                    // O tamanho total da mensagem vem do recvfrom(), passado em bytes_received
                    size_t data_size = bytes_received - offsetof(UDPMessage, payload) - filename_len;

                    fwrite(message->payload + filename_len, 1, data_size, new_transfer->file_ptr);

                } else { // Pacotes a partir do primeiro
                    int transfer_index = -1;

                    // Acha a transferência ativa do remetente
                    for (int i = 0; i < MAX_TRANSFERS; i++) {
                        if (transfers[i].active &&
                            transfers[i].peer_address.sin_addr.s_addr == sender_address->sin_addr.s_addr &&
                            transfers[i].peer_address.sin_port == sender_address->sin_port) 
                        {
                            transfer_index = i;
                            break;
                        }
                    }

                    if (transfer_index != -1) {
                        //Aceita pacotes em sequência
                        if (message->sequence_number == transfers[transfer_index].last_seq_num + 1) {
                            size_t data_size = bytes_received - offsetof(UDPMessage, payload);
                            fwrite(message->payload, 1, data_size, transfers[transfer_index].file_ptr);
                            transfers[transfer_index].last_seq_num++;
                        }
                    }
                }
            }
            break;

        case FILE_RESPONSE_END:
            // A transferência de um arquivo terminou, precisando renomear ele
            {
                int transfer_index = -1;
                // Acha a transferência ativa correspondente a este remetente
                for (int i = 0; i < MAX_TRANSFERS; i++) {
                    // Match de ativo, endereço e porta
                    if (transfers[i].active &&
                        transfers[i].peer_address.sin_addr.s_addr == sender_address->sin_addr.s_addr &&
                        transfers[i].peer_address.sin_port == sender_address->sin_port) 
                    {
                        transfer_index = i;
                        break;
                    }
                }

                if (transfer_index != -1) {
                    ActiveTransfer* transfer = &transfers[transfer_index];
                    printf("Finalizando recebimento do arquivo: %s\n", transfer->filename);

                    // Fecha o ponteiro do arquivo
                    fclose(transfer->file_ptr);

                    // Monta o caminho final do arquivo
                    char final_filepath[512];
                    snprintf(final_filepath, sizeof(final_filepath), "%s/%s", SYNC_DIR, transfer->filename);

                    // Renomeia o arquivo de ".part" para o nome final
                    if (rename(transfer->temp_filename, final_filepath) == 0) {
                        printf("Arquivo %s salvo com sucesso!\n", transfer->filename);
                    } else {
                        perror("Falha ao renomear o arquivo final");
                    }
                    
                    // Libera o slot de transferência
                    transfer->active = 0;
                }
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
void send_file_chunks(int sockfd, const char* filename, const struct sockaddr_in* dest_address) {

}

// Requisita um arquivo
void request_file(int sockfd, const char* filename, const struct sockaddr_in* dest_addr) {
    
}

// Envia a lista de arquivos locais para um peer
void send_file_list(int sockfd, const struct sockaddr_in* dest_address) {

}

// Atualiza os peers com updates no diretório de arquivos
void broadcast_update(int sockfd, const char* filename, MessageType type, const struct sockaddr_in peers[], int num_peers) {
    UDPMessage message;
    message.type = type;

    strncpy(message.payload, filename, PAYLOAD_SIZE - 1);
    message.payload[PAYLOAD_SIZE - 1] = '\0';

    printf("Transmitindo atualização (%s) para todos os peers...\n", (type == UPDATE_ADD ? "ADD" : "REMOVE"));

    for (int i = 0; i < num_peers; ++i) {
        sendto(sockfd, &message, sizeof(message), 0, (struct sockaddr*)&peers[i], sizeof(peers[i]));
    }
}
