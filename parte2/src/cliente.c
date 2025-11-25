#include <getopt.h>
#include "../include/common.h"


void print_usage(const char* prog) {
    fprintf(stderr, "Uso: %s -a <IP_SERVIDOR> -d <intervalo_ms> -N <duracion_seg>\n", prog);
    fprintf(stderr, "  -a: IP del servidor\n");
    fprintf(stderr, "  -d: intervalo entre envíos en milisegundos\n");
    fprintf(stderr, "  -N: duración total de la prueba en segundos\n");
    fprintf(stderr, "Ejemplo: %s -h 192.168.1.100 -d 50 -N 10\n", prog);
}


int main(int argc, char* argv[]) {
    char* server_ip = NULL;
    int interval_ms = 0;
    int duration_sec = 0;
    int opt;

    while ((opt = getopt(argc, argv, "a:d:N:s:")) != -1) {
        switch (opt) {
            case 'a': server_ip = optarg; break;
            case 'd': interval_ms = atoi(optarg); break;
            case 'N': duration_sec = atoi(optarg); break;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (!server_ip || interval_ms <= 0 || duration_sec <= 0) {
        print_usage(argv[0]);
        return 1;
    }

    printf("\n*========================================*\n");
    printf("|  CLIENTE TCP - ONE WAY DELAY           |\n");
    printf("*========================================*\n");
    printf("|  Servidor: %-27s |\n", server_ip);
    printf("|  Intervalo: %-4d ms                    |\n", interval_ms);
    printf("|  Duración: %-4d seg                    |\n", duration_sec);
    printf("*========================================*\n");

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

    uint8_t* pdu = malloc(MAX_PDU_SIZE);
    if (!pdu) {
        perror("Error en malloc()");
        close(s);
        return 1;
    }

    
    int pdu_count=0;
    int duration_us=duration_sec*1000;
    int current_timeout_ms=duration_us;
    struct timeval start_time, current_time,elapsed_time;
    gettimeofday(&start_time, NULL);

    while (current_timeout_ms>0) {
        int payload_size= rand() % 501 + 500;  // generar num entre 0 y 500 y sumarle 500 -> conseguir numero entre 500 y 1000
        size_t pdu_size = sizeof(uint64_t) + payload_size + 1;
        memset(pdu + sizeof(uint64_t), FILLER_BYTE, payload_size); // Llenar payload con 0x20 (filler)
        pdu[pdu_size - 1] = DELIMITER; // Poner delimitador al final

        uint64_t timestamp = get_timestamp_us();
        memcpy(pdu, &timestamp, sizeof(uint64_t));

        ssize_t sent = send(s, pdu, pdu_size, 0);
        if (sent < 0) {
            perror("Error en send()");
            break;
        }

        pdu_count++;
        printf("\rPDUs enviados: %d", pdu_count);
        fflush(stdout);

        usleep(interval_ms * 1000);

        gettimeofday(&current_time, NULL);
        
        elapsed_time.tv_sec = current_time.tv_sec - start_time.tv_sec;
        elapsed_time.tv_usec = current_time.tv_usec - start_time.tv_usec;
        long elapsed_ms = (elapsed_time.tv_sec * 1000) + (elapsed_time.tv_usec / 1000);
        
        current_timeout_ms = duration_us - (int)elapsed_ms;
    }

    printf("\n\nEnvío completado: %d PDUs enviados\n", pdu_count);

    free(pdu);
    close(s);
    printf("Conexión cerrada\n");

    return 0;
}
