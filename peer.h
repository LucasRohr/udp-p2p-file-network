#ifndef PEER_H
#define PEER_H

#define MAX_PEERS 10
#define MAX_FILES 100
#define MAX_FILENAME_LEN 256
#define BUFFER_SIZE 1024 // Tamanho do buffer em bytes (pacote UDP)

// Struct para representar um peer na rede
typedef struct {
    char ip_address[16];
    int port;
} Peer;

// Struct para armazenar informações de um arquivo
typedef struct {
    char filename[MAX_FILENAME_LEN];
} FileInfo;

// Enum para tipos de comunicação na rede (remover arquivo, adicionar arquivo, obter arquivos, etc...)
typedef enum {
    LIST_REQUEST,
    LIST_RESPONSE,
    FILE_REQUEST,
    FILE_RESPONSE_CHUNK,
    FILE_RESPONSE_END,
    UPDATE_ADD,
    UPDATE_REMOVE
} MessageType;

// Struct para representar uma mensagem no protocolo UDP, com tipo da mensagem e o dado em si
typedef struct {
    MessageType type;
    unsigned int sequence_number; // para segmentos (chunks) caso tenha fragmentação
    char payload[BUFFER_SIZE - sizeof(MessageType)];
} UDPMessage;

#endif // PEER_H