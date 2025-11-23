#ifndef COMMON_TCP_H
#define COMMON_TCP_H

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
#include <sys/time.h>

#define SERVER_PORT "20252"
#define MIN_PAYLOAD 500
#define MAX_PAYLOAD 1000
#define DELIMITER '|'  // ASCII 124
#define FILLER_BYTE 0x20

// Tamaños de PDU: 8 (timestamp) + N (payload) + 1 (delimiter)
// Mínimo: 8 + 500 + 1 = 509 bytes
// Máximo: 8 + 1000 + 1 = 1009 bytes
#define MIN_PDU_SIZE (sizeof(uint64_t) + MIN_PAYLOAD + 1)
#define MAX_PDU_SIZE (sizeof(uint64_t) + MAX_PAYLOAD + 1)

// ============================================
// ESTRUCTURA PDU (Protocol Data Unit)
// ============================================
typedef struct {
    uint64_t origin_timestamp;      // 8 bytes - timestamp en microsegundos
    uint8_t payload[MAX_PAYLOAD];   // 1000 bytes máximo (filler 0x20)
    uint8_t delimiter;              // 1 byte - siempre '|' (ASCII 124)
} __attribute__((packed)) App_PDU;

// Obtener timestamp en microsegundos desde epoch
uint64_t get_timestamp_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// Convertir microsegundos a segundos con precisión
double us_to_seconds(uint64_t us) {
    return (double)us / 1000000.0;
}

#endif // COMMON_TCP_H
