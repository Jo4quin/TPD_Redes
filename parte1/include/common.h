#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>

// ============================================
// CONFIGURACIÓN DEL PROTOCOLO
// ============================================
#define SERVER_PORT "20252"
#define MAX_DATA_SIZE 1470
#define PDU_HEADER_SIZE 2

// ============================================
// TIPOS DE MENSAJES
// ============================================
#define HELLO 1
#define WRQ   2
#define DATA  3
#define ACK   4
#define FIN   5

// ============================================
// ESTRUCTURA PDU (Protocol Data Unit)
// ============================================
typedef struct {
    uint8_t type;              // Tipo de mensaje (HELLO, WRQ, DATA, ACK, FIN)
    uint8_t seq_num;           // Número de secuencia (0 o 1)
    char data[MAX_DATA_SIZE];  // por aviso campus recomiendan que tenga máximo 1470 B
} __attribute__((packed)) App_PDU;  // packed para evitar padding

// ============================================
// FUNCIONES AUXILIARES
// ============================================

// Convierte tipo de mensaje a string (para debugging)
const char* type_to_string(uint8_t type) {
    switch(type) {
        case HELLO: return "HELLO";
        case WRQ:   return "WRQ";
        case DATA:  return "DATA";
        case ACK:   return "ACK";
        case FIN:   return "FIN";
        default:    return "UNKNOWN";
    }
}

// Imprime información de un PDU (para debugging)
void print_pdu(const char* prefix, App_PDU* pdu) {
    printf("%s [Type=%s, Seq=%d]\n", 
            prefix, 
            type_to_string(pdu->type), 
            pdu->seq_num);
}

#endif // COMMON_H