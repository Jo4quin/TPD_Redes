#include "../include/common.h"
#include <poll.h>


#define MAX_CLIENTS 10


typedef struct {
    int activo;
    struct sockaddr_in addr;
    socklen_t addr_len;
    int autenticado;
    int wrq_recibido;
    char filename[256];
    FILE* file;
    uint8_t last_seq;
} ClientState;


ClientState clients[MAX_CLIENTS];


ClientState* find_or_create_client(struct sockaddr_in* addr, socklen_t addr_len) {
    int empty_slot = -1;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].activo) {
            if (clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
                clients[i].addr.sin_port == addr->sin_port) {
                return &clients[i];
            }
        } else if (empty_slot == -1) {
            empty_slot = i;
        }
    }
    
    if (empty_slot != -1) {
        memset(&clients[empty_slot], 0, sizeof(ClientState));
        clients[empty_slot].activo = 1;
        clients[empty_slot].addr = *addr;
        clients[empty_slot].addr_len = addr_len;
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        printf("[NUEVO] Cliente %s:%d en slot %d\n", 
               ip, ntohs(addr->sin_port), empty_slot);
        
        return &clients[empty_slot];
    }
    
    return NULL;
}


void release_client(ClientState* client) {
    if (client->file) {
        fclose(client->file);
        client->file = NULL;
    }
    memset(client, 0, sizeof(ClientState));
}


void send_ack(int socket, ClientState* client, uint8_t seq_num, const char* error_msg) {
    App_PDU ack;
    memset(&ack, 0, sizeof(App_PDU));
    ack.type = ACK;
    ack.seq_num = seq_num;
    
    int data_len = 0;
    if (error_msg) {
        strncpy(ack.data, error_msg, MAX_DATA_SIZE - 1);
        data_len = strlen(error_msg) + 1;
    }
    
    sendto(socket, &ack, PDU_HEADER_SIZE + data_len, 0,
           (struct sockaddr*)&client->addr, client->addr_len);
    
    printf("  -> ACK enviado (seq=%d)\n", seq_num);
}


void handle_hello(int socket, App_PDU* pdu, ClientState* client) {
    printf("  [HELLO] Credencial: %s\n", pdu->data);
    
    if (strcmp(pdu->data, "g23-889d") == 0) {
        printf("  [OK] Credencial valida\n");
        client->autenticado = 1;
        client->last_seq = 0;
        send_ack(socket, client, 0, NULL);
    } else {
        printf("  [ERROR] Credencial invalida\n");
        send_ack(socket, client, 0, "Credencial invalida");
    }
}


void handle_wrq(int socket, App_PDU* pdu, ClientState* client) {
    printf("  [WRQ] Filename: %s\n", pdu->data);
    
    if (!client->autenticado) {
        printf("  [ERROR] Cliente no autenticado - descartando\n");
        return;
    }
    
    size_t len = strlen(pdu->data);
    if (len < 4 || len > 10) {
        printf("  [ERROR] Filename debe tener 4-10 caracteres\n");
        send_ack(socket, client, 1, "Filename invalido (4-10 chars)");
        return;
    }
    
    strncpy(client->filename, pdu->data, sizeof(client->filename) - 1);
    client->file = fopen(client->filename, "wb");
    
    if (!client->file) {
        perror("  [ERROR] fopen");
        send_ack(socket, client, 1, "Error abriendo archivo");
        return;
    }
    
    printf("  [OK] Archivo abierto: %s\n", client->filename);
    client->wrq_recibido = 1;
    client->last_seq = 1;
    send_ack(socket, client, 1, NULL);
}


void handle_data(int socket, App_PDU* pdu, ClientState* client, int bytes_recv) {
    printf("  [DATA] seq=%d, bytes=%d\n", pdu->seq_num, bytes_recv - PDU_HEADER_SIZE);
    
    if (!client->wrq_recibido) {
        printf("  [ERROR] WRQ no recibido - descartando\n");
        return;
    }
    
    uint8_t expected_seq = (client->last_seq == 0) ? 1 : 0;

    sleep(1);
    
    if (pdu->seq_num != expected_seq) {
        printf("  [WARN] Seq incorrecto (esperaba %d) - reenviando ultimo ACK\n", expected_seq);
        send_ack(socket, client, client->last_seq, NULL);
        return;
    }
    
    int data_len = bytes_recv - PDU_HEADER_SIZE;
    size_t written = fwrite(pdu->data, 1, data_len, client->file);
    printf("  [OK] Escritos %zu bytes\n", written);
    
    client->last_seq = pdu->seq_num;
    send_ack(socket, client, pdu->seq_num, NULL);
}


void handle_fin(int socket, App_PDU* pdu, ClientState* client) {
    printf("  [FIN] seq=%d\n", pdu->seq_num);
    
    if (client->file) {
        fclose(client->file);
        client->file = NULL;
        printf("  [OK] Archivo cerrado: %s\n", client->filename);
    }
    
    send_ack(socket, client, pdu->seq_num, NULL);
    printf("  [OK] Sesion completada\n");
    release_client(client);
}


int main(int argc, char* argv[]) {
    printf("\n*==========================================*\n");
    printf("|  SERVIDOR STOP & WAIT                    |\n");
    printf("*==========================================*\n");
    printf("|  Puerto: %-30s |\n", SERVER_PORT);
    printf("|  Max clientes: %-24d |\n", MAX_CLIENTS);
    printf("*==========================================*\n");
    
    memset(clients, 0, sizeof(clients));
    
    int s;
    struct addrinfo hints, *servinfo;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    
    if (getaddrinfo(NULL, SERVER_PORT, &hints, &servinfo) != 0) {
        perror("getaddrinfo");
        return 1;
    }
    
    s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (s < 0) {
        perror("socket");
        freeaddrinfo(servinfo);
        return 1;
    }
    
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(s, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("bind");
        close(s);
        freeaddrinfo(servinfo);
        return 1;
    }
    
    printf("\nServidor escuchando en puerto %s\n\n", SERVER_PORT);
    freeaddrinfo(servinfo);
    
    struct pollfd fds[1];
    fds[0].fd = s;
    fds[0].events = POLLIN;
    
    App_PDU pdu;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    
    while (1) {
        int poll_result = poll(fds, 1, -1);
        
        if (poll_result < 0) {
            perror("poll");
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            memset(&pdu, 0, sizeof(App_PDU));
            addr_len = sizeof(client_addr);
            
            int received = recvfrom(s, &pdu, sizeof(App_PDU), 0,
                                   (struct sockaddr*)&client_addr, &addr_len);
            
            if (received < PDU_HEADER_SIZE) {
                continue;
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            printf("\n[RECV] %s:%d - Type=%s, Seq=%d\n",
                   client_ip, ntohs(client_addr.sin_port),
                   type_to_string(pdu.type), pdu.seq_num);
            
            ClientState* client = find_or_create_client(&client_addr, addr_len);
            if (!client) {
                printf("  [ERROR] Servidor lleno\n");
                continue;
            }
            
            switch (pdu.type) {
                case HELLO:
                    handle_hello(s, &pdu, client);
                    break;
                case WRQ:
                    handle_wrq(s, &pdu, client);
                    break;
                case DATA:
                    handle_data(s, &pdu, client, received);
                    break;
                case FIN:
                    handle_fin(s, &pdu, client);
                    break;
                default:
                    printf("  [ERROR] Tipo desconocido: %d\n", pdu.type);
                    break;
            }
        }
    }
    
    close(s);
    return 0;
}
