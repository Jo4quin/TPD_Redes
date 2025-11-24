#include "common.h"
#include <getopt.h>

#define BUFFER_SIZE 4096

int main(int argc, char* argv[]) {
    char* output_file = "one_way_delay.csv";
    int opt;

    while ((opt = getopt(argc, argv, "o:")) != -1) {
        switch (opt) {
            case 'o': output_file = optarg; break;
            default:
                fprintf(stderr, "Uso: %s [-o archivo_salida.csv]\n", argv[0]);
                return 1;
        }
    }

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  SERVIDOR TCP - ONE WAY DELAY          ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║  Puerto: %-29s ║\n", SERVER_PORT);
    printf("║  Archivo: %-28s ║\n", output_file);
    printf("║  Presiona Ctrl+C para salir            ║\n");
    printf("╚════════════════════════════════════════╝\n");

    // Crear socket TCP
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, SERVER_PORT, &hints, &servinfo);
    if (status != 0) {
        fprintf(stderr, "Error en getaddrinfo(): %s\n", gai_strerror(status));
        return 1;
    }

    int listen_sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (listen_sock < 0) {
        perror("Error en socket()");
        freeaddrinfo(servinfo);
        return 1;
    }

    int opt_val = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

    if (bind(listen_sock, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error en bind()");
        close(listen_sock);
        freeaddrinfo(servinfo);
        return 1;
    }
    freeaddrinfo(servinfo);

    if (listen(listen_sock, 1) < 0) {
        perror("Error en listen()");
        close(listen_sock);
        return 1;
    }

    printf("\nServidor escuchando en puerto %s...\n", SERVER_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        printf("\nEsperando conexión...\n");
        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            if (errno == EINTR) continue;
            perror("Error en accept()");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Cliente conectado: %s:%d\n\n", client_ip, ntohs(client_addr.sin_port));

        // Abrir archivo CSV
        FILE* csv = fopen(output_file, "w");
        if (!csv) {
            perror("Error abriendo archivo CSV");
            close(client_sock);
            continue;
        }
        fprintf(csv, "Numero,One_Way_Delay_seg\n");

        // Buffer para lecturas parciales
        uint8_t buffer[BUFFER_SIZE];
        App_PDU pdu_buffer;
        size_t pdu_offset = 0;
        int pdu_count = 0;

        while (1) {
            ssize_t received = recv(client_sock, buffer, BUFFER_SIZE, 0);
            
            if (received < 0) {
                if (errno == EINTR) continue;
                perror("Error en recv()");
                break;
            }
            if (received == 0) {
                printf("\nCliente desconectado\n");
                break;
            }

            // Procesar bytes recibidos
            uint8_t* pdu_bytes = (uint8_t*)&pdu_buffer;
            for (ssize_t i = 0; i < received; i++) {
                if (pdu_offset < sizeof(App_PDU)) {
                    pdu_bytes[pdu_offset++] = buffer[i];
                }

                // Verificar si encontramos el delimitador
                if (buffer[i] == pdu_buffer.delimiter && pdu_offset >= MIN_PDU_SIZE) {
                    // PDU completa recibida - obtener timestamp de destino
                    uint64_t dest_ts = get_timestamp_us();

                    // Extraer timestamp de origen directamente del struct
                    uint64_t origin_ts = pdu_buffer.origin_timestamp;

                    // Calcular one-way delay
                    int64_t delay_us = (int64_t)dest_ts - (int64_t)origin_ts;
                    double delay_sec = (double)delay_us / 1000000.0;

                    pdu_count++;

                    // Loguear en CSV
                    fprintf(csv, "%d,%.6f\n", pdu_count, delay_sec);
                    fflush(csv);

                    printf("\rPDU #%d: OWD = %.6f seg (size=%zu bytes)", 
                           pdu_count, delay_sec, pdu_offset);
                    fflush(stdout);

                    // Reiniciar buffer de PDU
                    pdu_offset = 0;
                }
            }
        }

        printf("\n\nTotal PDUs recibidas: %d\n", pdu_count);
        printf("Resultados guardados en: %s\n", output_file);

        fclose(csv);
        close(client_sock);
    }

    close(listen_sock);
    printf("\nServidor cerrado\n");

    return 0;
}
