#include "common.h"
#include <getopt.h>

void print_usage(const char* prog) {
    fprintf(stderr, "Uso: %s -h <IP_SERVIDOR> -d <intervalo_ms> -N <duracion_seg> [-s <payload_size>]\n", prog);
    fprintf(stderr, "  -h: IP del servidor\n");
    fprintf(stderr, "  -d: intervalo entre envíos en milisegundos\n");
    fprintf(stderr, "  -N: duración total de la prueba en segundos\n");
    fprintf(stderr, "  -s: tamaño del payload (500-1000, default: 500)\n");
    fprintf(stderr, "Ejemplo: %s -h 192.168.1.100 -d 50 -N 10\n", prog);
}

int main(int argc, char* argv[]) {
    char* server_ip = NULL;
    int interval_ms = 0;
    int duration_sec = 0;
    int payload_size = MIN_PAYLOAD;
    int opt;

    while ((opt = getopt(argc, argv, "h:d:N:s:")) != -1) {
        switch (opt) {
            case 'h': server_ip = optarg; break;
            case 'd': interval_ms = atoi(optarg); break;
            case 'N': duration_sec = atoi(optarg); break;
            case 's': payload_size = atoi(optarg); break;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (!server_ip || interval_ms <= 0 || duration_sec <= 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (payload_size < MIN_PAYLOAD || payload_size > MAX_PAYLOAD) {
        fprintf(stderr, "Error: payload debe estar entre %d y %d\n", MIN_PAYLOAD, MAX_PAYLOAD);
        return 1;
    }

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  CLIENTE TCP - ONE WAY DELAY           ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║  Servidor: %-27s ║\n", server_ip);
    printf("║  Intervalo: %-4d ms                    ║\n", interval_ms);
    printf("║  Duración: %-4d seg                    ║\n", duration_sec);
    printf("║  Payload: %-5d bytes                  ║\n", payload_size);
    printf("╚════════════════════════════════════════╝\n");

    // Crear socket TCP
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(server_ip, SERVER_PORT, &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "Error en getaddrinfo(): %s\n", gai_strerror(status));
        return 1;
    }

    int s = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (s < 0) {
        perror("Error en socket()");
        freeaddrinfo(servinfo);
        return 1;
    }

    printf("\nConectando al servidor...\n");
    if (connect(s, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error en connect()");
        close(s);
        freeaddrinfo(servinfo);
        return 1;
    }
    printf("Conectado!\n\n");
    freeaddrinfo(servinfo);

    // Preparar PDU
    App_PDU pdu;
    memset(&pdu, 0, sizeof(App_PDU));
    
    // Llenar payload con 0x20 (filler)
    memset(pdu.payload, FILLER_BYTE, payload_size);
    // Poner delimitador después del payload
    // El delimitador va en la posición payload_size (justo después del payload usado)
    pdu.payload[payload_size] = DELIMITER;
    
    // Tamaño real a enviar: timestamp + payload_size + delimiter
    size_t pdu_size = sizeof(uint64_t) + payload_size + 1;

    // Calcular cantidad de PDUs a enviar
    int total_pdus = (duration_sec * 1000) / interval_ms;
    printf("Enviando %d PDUs de %zu bytes cada %d ms...\n\n", total_pdus, pdu_size, interval_ms);

    struct timeval start, now;
    gettimeofday(&start, NULL);
    int pdu_count = 0;

    while (pdu_count < total_pdus) {
        // Obtener timestamp justo antes de enviar
        pdu.origin_timestamp = get_timestamp_us();

        // Enviar PDU (solo los bytes necesarios)
        ssize_t sent = send(s, &pdu, pdu_size, 0);
        if (sent < 0) {
            perror("Error en send()");
            break;
        }

        pdu_count++;
        printf("\rPDUs enviados: %d/%d", pdu_count, total_pdus);
        fflush(stdout);

        // Esperar el intervalo
        usleep(interval_ms * 1000);
    }

    printf("\n\nEnvío completado: %d PDUs enviados\n", pdu_count);

    close(s);
    printf("Conexión cerrada\n");

    return 0;
}
