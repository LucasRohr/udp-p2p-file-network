#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/stat.h>

#include "protocol.h"

const char* SYNC_DIR = "Documents/sync";

ActiveTransfer transfers[MAX_TRANSFERS];

// Inicializa array de transfers
void initialize_transfers() {
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        transfers[i].active = 0;
    }
}

// Verifica se o arquivo existe no diretório local (funcao auxiliar)
int file_exists_locally(const char* filename) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", SYNC_DIR, filename); // Monta caminho local com diretório sync

    struct stat buffer;

    // A função stat retorna 0 se o arquivo/caminho existir.
    return (stat(file_path, &buffer) == 0);
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
            printf("Recebido LIST_RESPONSE de %s:%d. Verificando arquivos...\n",
               inet_ntoa(sender_address->sin_addr), ntohs(sender_address->sin_port));

            // strtok modifica a string, então precisa copiar o payload em nova variável
            char payload_copy[PAYLOAD_SIZE];
            strncpy(payload_copy, message->payload, PAYLOAD_SIZE);
            payload_copy[PAYLOAD_SIZE - 1] = '\0';

            // O delimitador para separar os nomes dos arquivos é vírgula
            const char* delimiter = ",";
            
            // strtok retorna o primeiro "token" (nome de arquivo) da string
            char* filename = strtok(payload_copy, delimiter);

            // Loop processa todos os tokens encontrados
            while (filename != NULL) {
                // Remove espaços em branco no início
                while (*filename == ' ') {
                    filename++;
                }

                if (strlen(filename) > 0) {
                    // Para cada arquivo na lista recebida, verifica se tem no local
                    if (!file_exists_locally(filename)) {
                        // Se não tem o arquivo, faz uma request
                        printf("Arquivo '%s' não encontrado localmente. Solicitando...\n", filename);
                        request_file(sockfd, filename, sender_address);
                    }
                }

                // A chamada seguinte a strtok com NULL continua de onde parou na string original
                filename = strtok(NULL, delimiter);
            }

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
            char file_to_remove[MAX_PATH_LEN];

            snprintf(file_to_remove, sizeof(file_to_remove), "%s/%s", SYNC_DIR, message->payload);

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
    char file_path[512];
    sprintf(file_path, "%s/%s", SYNC_DIR, filename); // Constrói caminho do arquivo

    FILE *file = fopen(file_path, "rb"); // Abre em modo de leitura binária (read binary)

    if (!file) {
        perror("Erro ao abrir arquivo para envio");
        return;
    }

    UDPMessage message;
    message.type = FILE_RESPONSE_CHUNK; // Tipo de envio em segmentos

    size_t bytes_read;
    unsigned int seq_num = 0;

    // Lê o arquivo em pedaços e envia cada um como um pacote UDP
    // Avança nos segmentos "cortando" conforme o valor de PAYLOAD_SIZE
    while ((bytes_read = fread(message.payload, 1, PAYLOAD_SIZE, file)) > 0) {
        message.sequence_number = seq_num++;
        // Tamanho da mensagem = cabeçalho + bytes lidos
        size_t message_size = sizeof(message.type) + sizeof(message.sequence_number) + bytes_read;

        sendto(sockfd, &message, message_size, 0, (struct sockaddr*)dest_address, sizeof(*dest_address));
        usleep(1000); // Pausa para evitar sobrecarga do buffer de destino (boa prática)
    }

    // Envia uma mensagem final para indicar o fim da transmissão
    message.type = FILE_RESPONSE_END; // Tipo de envio indicando fim da transmissão
    message.sequence_number = seq_num;
    message.payload[0] = '\0'; // Payload vazia
    sendto(sockfd, &message, sizeof(message), 0, (struct sockaddr*)dest_address, sizeof(*dest_address));

    fclose(file); // Fecha arquivo local
    printf("Envio do arquivo %s concluído para %s:%d\n", filename, inet_ntoa(dest_address->sin_addr), ntohs(dest_address->sin_port));
}

// Requisita um arquivo
void request_file(int sockfd, const char* filename, const struct sockaddr_in* dest_address) {
    UDPMessage message;
    message.type = FILE_REQUEST; // Tipo para requisição de arquivo

    strncpy(message.payload, filename, PAYLOAD_SIZE - 1);
    message.payload[PAYLOAD_SIZE - 1] = '\0'; // Fim de string

    sendto(sockfd, &message, sizeof(message), 0, (struct sockaddr*)dest_address, sizeof(*dest_address)); // Envia requisição para peer destino
    printf("Solicitando o arquivo %s para %s:%d\n", filename, inet_ntoa(dest_address->sin_addr), ntohs(dest_address->sin_port));
}

// Envia a lista de arquivos locais para um peer
void send_file_list(int sockfd, const struct sockaddr_in* dest_address) {
    DIR *directory;
    struct dirent *direntry;
    UDPMessage message;
    
    message.type = LIST_RESPONSE; // Tipo de envio de lista de arquivos
    message.payload[0] = '\0'; // Inicia payload como string vazia

    directory = opendir(SYNC_DIR); // Abre diretório pré definido

    if (directory) { // Se abriu com sucesso
        while ((direntry = readdir(directory)) != NULL) { // Enquanto houver entradas no diretório sync
            // Ignora os diretórios '.' e '..'
            if (strcmp(direntry->d_name, ".") == 0 || strcmp(direntry->d_name, "..") == 0) {
                continue;
            }

            // Concatena o nome do arquivo no payload UDP, separado por vírgula
            strncat(message.payload, direntry->d_name, PAYLOAD_SIZE - strlen(message.payload) - 1);
            strncat(message.payload, ",", PAYLOAD_SIZE - strlen(message.payload) - 1);
        }

        closedir(directory); // Fecha diretório sync
    } else {
        perror("Não foi possível abrir o diretório de sincronização");
        return;
    }
    
    // Envia a mensagem contendo a lista de arquivos
    sendto(sockfd, &message, sizeof(message), 0, (struct sockaddr*)dest_address, sizeof(*dest_address));

    printf("Lista de arquivos enviada para %s:%d\n", inet_ntoa(dest_address->sin_addr), ntohs(dest_address->sin_port));
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
