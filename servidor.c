#include "common.h"

// ============================================
// ESTADO DEL CLIENTE
// ============================================
typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_len;
    int autenticado;
    int wrq_recibido;
    char filename[256];
    FILE* file;
    uint8_t last_seq;
} ClientState;

// ============================================
// ENVIAR ACK
// ============================================
void send_ack(int socket, struct sockaddr_in* client_addr, socklen_t addr_len, 
              uint8_t seq_num,const char* mensaje_error) {
    App_PDU ack;
    memset(&ack, 0, sizeof(App_PDU));
    ack.type = ACK;
    ack.seq_num = seq_num;
    if (mensaje_error) {
        strncpy(ack.data, mensaje_error, MAX_DATA_SIZE - 1);
    }
    
    sendto(socket, &ack, sizeof(App_PDU), 0, 
           (struct sockaddr*)client_addr, addr_len);
    
    printf("ACK enviado (seq=%d)\n", seq_num);
}

// ============================================
// MANEJAR HELLO
// ============================================
int handle_hello(int socket, App_PDU* pdu, ClientState* client) {
    printf("│ HELLO recibido              │\n");
    
    printf("Credencial: %s\n", pdu->data);
    
    // Validar credencial (simplificado)
    if (strcmp(pdu->data, "g23-889d") == 0) {
        printf("Credencial válida\n");
        client->autenticado = 1;
        client->last_seq = 0;
        send_ack(socket, &client->addr, client->addr_len, 0,NULL);
        return 0;
    } else {
        printf("Credencial inválida\n");
        send_ack(socket, &client->addr, client->addr_len, 0, "Credencial inválida"); // se le avisa al cliente
        return -1;
    }
}

// ============================================
// MANEJAR WRQ (Write Request)
// ============================================
int handle_wrq(int socket, App_PDU* pdu, ClientState* client) {
    printf("│ WRQ recibido                │\n");
    
    if (!client->autenticado) {
        printf("Cliente no autenticado, descartando WRQ silenciosamente\n"); // silenciosamente = sin avisarle al cliente
        return -1;
    }
    
    printf("Filename: %s\n", pdu->data);
    
    // Validar filename (4-10 caracteres)
    size_t len = strlen(pdu->data);
    if (len < 4 || len > 10) {
        printf("Filename inválido (debe tener 4-10 caracteres)\n");  // por enunciado
        return -1;
    }
    
    // Abrir archivo para escritura
    strncpy(client->filename, pdu->data, sizeof(client->filename) - 1);
    client->file = fopen(client->filename, "wb");
    
    if (!client->file) {
        perror("Error abriendo archivo");
        return -1;
    }
    
    printf("Archivo abierto: %s\n", client->filename);
    client->wrq_recibido = 1;
    client->last_seq = 1;
    send_ack(socket, &client->addr, client->addr_len, 1,NULL);
    
    return 0;
}

// ============================================
// MANEJAR DATA
// ============================================
int handle_data(int socket, App_PDU* pdu, ClientState* client,int bytes_recibidos) {
    printf("│ DATA recibido (seq=%d)      │\n", pdu->seq_num);
    
    if (!client->wrq_recibido) {
        printf("WRQ no recibido, descartando DATA silenciosamente\n");  // silenciosamente = sin avisarle al cliente
        return -1;
    }
    
    // Verificar número de secuencia
    uint8_t expected_seq = (client->last_seq == 0) ? 1 : 0;
    
    if (pdu->seq_num != expected_seq) {
        printf("Seq incorrecto (esperaba %d, recibí %d)\n", 
               expected_seq, pdu->seq_num);
        // Reenviar último ACK (por si se perdió)
        send_ack(socket, &client->addr, client->addr_len, client->last_seq,NULL);
        return 0;
    }
    
    // Escribir datos en archivo
    int data_len = bytes_recibidos - PDU_HEADER_SIZE;
    size_t written = fwrite(pdu->data, 1, data_len, client->file);
    
    printf("Escritos %zu bytes en archivo\n", written);
    
    // Actualizar estado y enviar ACK
    client->last_seq = pdu->seq_num;
    send_ack(socket, &client->addr, client->addr_len, pdu->seq_num,NULL);
    
    return 0;
}

// ============================================
// MANEJAR FIN
// ============================================
int handle_fin(int socket, App_PDU* pdu, ClientState* client) {
    printf("│ FIN recibido                │\n");
    
    if (client->file) {
        fclose(client->file);
        printf("Archivo cerrado: %s\n", client->filename);
        client->file = NULL;
    }
    
    send_ack(socket, &client->addr, client->addr_len, pdu->seq_num,NULL);
    
    // Reiniciar estado del cliente
    client->autenticado = 0;
    client->wrq_recibido = 0;
    
    printf("\nSesión completada\n");
    
    return 0;
}

// ============================================
// MAIN
// ============================================
int main(int argc, char* argv[]) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  SERVIDOR STOP & WAIT                  ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║  Puerto: %-29s ║\n", SERVER_PORT);
    printf("║  Presiona Ctrl+C para salir           ║\n");
    printf("╚════════════════════════════════════════╝\n");
    
    // ========================================
    // CREAR Y CONFIGURAR SOCKET
    // ========================================
    int s;
    struct addrinfo hints, *servinfo;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    
    int status = getaddrinfo(NULL, SERVER_PORT, &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "Error en getaddrinfo(): %s\n", gai_strerror(status));
        return 1;
    }
    
    s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (s < 0) {
        perror("Error en socket()");
        freeaddrinfo(servinfo);
        return 1;
    }
    
    // Permitir reusar el puerto
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind
    if (bind(s, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error en bind()");
        close(s);
        freeaddrinfo(servinfo);
        return 1;
    }
    
    printf("\nServidor escuchando en puerto %s\n", SERVER_PORT);
    freeaddrinfo(servinfo);
    
    // ========================================
    // LOOP PRINCIPAL
    // ========================================
    ClientState client;
    memset(&client, 0, sizeof(ClientState));
    client.addr_len = sizeof(client.addr);
    
    App_PDU pdu;
    
    // Configurar timeout para no bloquear indefinidamente
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (1) {
        memset(&pdu, 0, sizeof(App_PDU));
        
        // Recibir App_PDU
        int received = recvfrom(s, &pdu, sizeof(App_PDU), 0,
                               (struct sockaddr*)&client.addr, 
                               &client.addr_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout normal, continuar
                continue;
            } else {
                perror("Error en recvfrom()");
                continue;
            }
        }
        
        if (received == 0) {
            continue;
        }
        
        // Mostrar cliente
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.addr.sin_addr, client_ip, sizeof(client_ip));
        printf("\nApp_PDU recibido de %s:%d\n", 
               client_ip, ntohs(client.addr.sin_port));
        print_pdu("   ", &pdu);
        
        // Manejar según tipo
        switch (pdu.type) {
            case HELLO:
                handle_hello(s, &pdu, &client);
                break;
                
            case WRQ:
                handle_wrq(s, &pdu, &client);
                break;
                
            case DATA:
                handle_data(s, &pdu, &client,received);
                break;
                
            case FIN:
                handle_fin(s, &pdu, &client);
                break;
                
            default:
                printf("Type desconocido: %d\n", pdu.type);
                break;
        }
    }
    
    // ========================================
    // CLEANUP
    // ========================================
    if (client.file) {
        fclose(client.file);
    }
    
    close(s);
    printf("\nSocket y archivo destino cerrados\n");
    
    return 0;
}