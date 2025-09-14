#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "peer.h"

// Envia um arquivo segmentado em partes
void send_file(int sockfd, const char* filename, const struct sockaddr_in* dest_address);

// Recebe segmentos e remonta o arquivo
void receive_file(const char* filename, const UDPMessage* message);

// Envia a lista de arquivos locais para um peer
void send_file_list(int sockfd, const struct sockaddr_in* dest_address);

// Processa uma mensagem recebida
void handle_message(int sockfd, const UDPMessage* message, const struct sockaddr_in* sender_address);

#endif // PROTOCOL_H
