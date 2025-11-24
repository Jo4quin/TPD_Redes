#include "common.h"
#include <poll.h>      // Necesario para poll()
#include <sys/time.h>  // necesario para gettimeofday()
#define MAX_RETRIES 3
#define TIMEOUT_MSEC 3000 // Timeout total en milisegundos

#define MAX_RETRIES 3

// ============================================
// ENVIAR PDU Y ESPERAR ACK (con poll)
// Ignora ACKs incorrectos sin reiniciar el timer
// ============================================
int send_and_wait_ack(int socket, App_PDU* pdu, uint8_t expected_seq, int data_size) {
    App_PDU ack;
    int attempts = 0;
    
    // Aquí no se usa setsockopt(SO_RCVTIMEO)
    
    while (attempts < MAX_RETRIES) {
        // 1. ENVIAR App_PDU (retransmisión si es necesario)
        print_pdu("Enviando pdu", pdu);
        
        int sent = send(socket, pdu, PDU_HEADER_SIZE + data_size, 0);
        if (sent < 0) {
            perror("Error en send()");
            return -1;
        }

        // 2. INICIALIZAR TIMER y ESPERAR ACK
        
        // El tiempo de timeout se manejará dinámicamente.
        // Inicialmente, es el tiempo total (3000ms).
        int current_timeout_ms = TIMEOUT_MSEC; 
        
        // Necesitamos registrar el tiempo de inicio para calcular el tiempo restante
        struct timeval start_time, current_time, elapsed_time;
        gettimeofday(&start_time, NULL);
        
        printf("Esperando ACK (Max %d ms)...\n", TIMEOUT_MSEC);

        struct pollfd pfd;   // solo queremos monitorear 1 fd, el de nuestro unico socket
        pfd.fd = socket;
        pfd.events = POLLIN;     // el poll despierta con el evento "IN" (llegaron datos para leer)

        // Bucle para esperar y procesar paquetes dentro del timer total
        while (current_timeout_ms > 0) {
            
            // poll() bloquea hasta que hay datos o expira el timer restante
            int poll_res = poll(&pfd, 1, current_timeout_ms);   // 1 = cantidad de fd a monitorear

            if (poll_res < 0) {
                // Error de poll
                perror("Error en poll()");
                return -1;

            } else if (poll_res == 0) {
                printf("poll=0");
                // TIMEOUT: No se recibieron datos en el tiempo restante
                break;

            } else {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {     // -> poll despertó debido a eventos de error
                    if (pfd.revents & POLLERR)
                        printf("POLLERR: hubo un problema con el socket del servidor\n");
                    if (pfd.revents & POLLHUP)
                        printf("POLLHUP: se detectó socket cerrado del servidor\n");
                    if (pfd.revents & POLLNVAL)
                        printf("POLLNVAL\n");
                    return -1;
                }
                if (pfd.revents & POLLIN) {                   // POLLIN -> poll despertó debido a evento IN
                    memset(&ack, 0, sizeof(App_PDU));  // Limpiar antes de recibir
                    int received = recv(socket, &ack, sizeof(App_PDU), 0);

                    if (received < 0) {
                        perror("Error en recv() después de poll");
                        return -1;
                    }
                    
                    // 3. VERIFICAR ACK
                    printf("Se esperaba recibir seq=%d. Recibido type=%d, seq=%d\n", 
                            expected_seq, ack.type, ack.seq_num);

                    if (ack.type == ACK && ack.seq_num == expected_seq) {
                        // ✅ ACK CORRECTO: Salir con éxito
                        size_t data_len = strnlen(ack.data, MAX_DATA_SIZE);
                        if (data_len > 0) {
                            printf("Servidor dice: %s\n", ack.data);
                            return -2;
                        } else {
                            printf("ACK correcto (seq=%d) recibido.\n\n", ack.seq_num);
                            return 0; // Éxito
                        }

                    } else {
                        // ⚠️ ACK INCORRECTO: Se descarta y se recalcula el tiempo restante.
                        printf("ACK incorrecto. Se ignora y se recalcula el tiempo restante...\n");
                        
                        // Recalcular el tiempo restante
                        gettimeofday(&current_time, NULL);
                        
                        // Calcular tiempo transcurrido en milisegundos
                        elapsed_time.tv_sec = current_time.tv_sec - start_time.tv_sec;
                        elapsed_time.tv_usec = current_time.tv_usec - start_time.tv_usec;
                        long elapsed_ms = (elapsed_time.tv_sec * 1000) + (elapsed_time.tv_usec / 1000);
                        
                        current_timeout_ms = TIMEOUT_MSEC - (int)elapsed_ms;
                        printf("current timeout ms: %i",current_timeout_ms);
                        
                        // Volver al inicio del bucle while para un nuevo poll con el tiempo restante.
                    }
                }
            }
        }
        // Si el bucle while(current_timeout_ms > 0) termina porque el tiempo se acabó
        attempts++;
        printf("TIMEOUT (calculado) - Reintento %d/%d\n", attempts, MAX_RETRIES);
    }
    
    // Si llegamos aquí, fallaron todos los intentos
    printf("FALLO después de %d intentos\n", MAX_RETRIES);
    return -1;
}


// ============================================
// FASE 1: HELLO (Autenticación)
// ============================================
int fase_hello(int socket, const char* credencial) {
    printf("\n===== FASE 1: HELLO (Autenticación) =====\n");
    
    App_PDU pdu;
    memset(&pdu, 0, sizeof(App_PDU));
    pdu.type = HELLO;
    pdu.seq_num = 0;
    strncpy(pdu.data, credencial, MAX_DATA_SIZE - 1);

    size_t data_size = strlen(credencial) + 1;  // +1 para null terminator
    
    return send_and_wait_ack(socket, &pdu, 0,data_size);
}

// ============================================
// FASE 2: WRQ (Write Request)
// ============================================
int fase_wrq(int socket, const char* filename) {
    printf("\n===== FASE 2: WRQ (Write Request) =====\n");
    
    App_PDU pdu;
    memset(&pdu, 0, sizeof(App_PDU));
    pdu.type = WRQ;
    pdu.seq_num = 1;
    strncpy(pdu.data, filename, MAX_DATA_SIZE - 1);

    size_t data_size = strlen(filename) + 1;  // +1 para null terminator
    
    return send_and_wait_ack(socket, &pdu, 1,data_size);
}

// ============================================
// FASE 3: DATA (Transferencia con Stop & Wait)
// ============================================
int fase_data(int socket, const char* filepath, uint8_t* last_seq_out) {
    printf("\n===== FASE 3: DATA (Transferencia con Stop & Wait) =====\n");
    
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        perror("Error en fopen()");
        return -1;
    }
    
    App_PDU pdu;
    int seq = 0;  // en la fase DATA seq empieza en 0
    int paquetes_enviados = 0;
    size_t bytes_leidos;
    
    char data_buffer[MAX_DATA_SIZE];

    while ((bytes_leidos = fread(data_buffer, 1, MAX_DATA_SIZE, file)) > 0) {
        memset(&pdu, 0, sizeof(App_PDU));  // es necesario resetear la pdu para que no le quede basura accidental en el campo data
        pdu.type = DATA;
        pdu.seq_num = seq;
        memcpy(pdu.data, data_buffer, bytes_leidos);  // copiar datos leídos desde data_buffer a pdu.data
        
        printf("\n--- Paquete #%d (seq=%d, %zu bytes) ---\n", 
                paquetes_enviados + 1, seq, bytes_leidos);
        
        if (send_and_wait_ack(socket, &pdu, seq,bytes_leidos) != 0) {
            fclose(file);
            return -1;
        }

        *last_seq_out = seq;
        paquetes_enviados++;
        seq = (seq == 0) ? 1 : 0; // se alterna el numero de secuencia. if seq==0 -> seq=1, else -> seq=0
    }
    
    fclose(file);
    printf("\nTransferencia completada: %d paquetes enviados\n", paquetes_enviados);
    return 0;
}

// ============================================
// FASE 4: FIN (Finalización)
// ============================================
int fase_fin(int socket, const char* filename, int last_seq) {
    printf("\n===== FASE 4: FIN (Finalización) =====\n");
    
    App_PDU pdu;
    memset(&pdu, 0, sizeof(App_PDU));
    pdu.type = FIN;
    int seq = (last_seq == 0) ? 1 : 0;       // aca se define el seq del FIN dependiendo de cómo termino el seq en fase DATA
    pdu.seq_num = seq;
    // strncpy(pdu.data, filename, MAX_DATA_SIZE - 1);     // por aviso en campus debe ser vacio el campo data en el FIN
    
    return send_and_wait_ack(socket, &pdu, seq,0);
}

// ============================================
// MAIN
// ============================================
int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <IP_SERVIDOR> <ARCHIVO_LOCAL> <ARCHIVO_REMOTO>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 127.0.0.1 test.txt a.txt\n", argv[0]);
        return 1;
    }
    
    const char* server_ip = argv[1];
    const char* local_file = argv[2];
    const char* remote_name = argv[3];
    
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  CLIENTE STOP & WAIT                   ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║  Servidor: %-27s ║\n", server_ip);
    printf("║  Archivo:  %-27s ║\n", local_file);
    printf("╚════════════════════════════════════════╝\n");
    

    // ========= CREAR SOCKET UDP ============

    int s;
    struct addrinfo hints, *servinfo;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    int status = getaddrinfo(server_ip, SERVER_PORT, &hints, &servinfo);
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
    
    //  ======= CONNECT ============
    if (connect(s, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error en connect()");
        close(s);
        freeaddrinfo(servinfo);
        return 1;
    }
    
    printf("Conectado al servidor\n");
    freeaddrinfo(servinfo);

    // ===== EJECUTAR LAS 4 FASES =======

    // FASE 1: HELLO
    if (fase_hello(s, "g23-889d") != 0) {
        fprintf(stderr, "💀 Fallo en FASE 1 (HELLO)\n");
        close(s);
        return 1;
    }
    
    // FASE 2: WRQ
    if (fase_wrq(s, remote_name) != 0) {
        fprintf(stderr, "💀 Fallo en FASE 2 (WRQ)\n");
        close(s);
        return 1;
    }
    
    // FASE 3: DATA
    uint8_t last_data_seq = 0;
    if (fase_data(s, local_file,&last_data_seq) != 0) {
        fprintf(stderr, "💀 Fallo en FASE 3 (DATA)\n");
        close(s);
        return 1;
    }
    
    // FASE 4: FIN
    if (fase_fin(s, remote_name, last_data_seq) != 0) {  // El seq depende del último DATA
        fprintf(stderr, "💀 Fallo en FASE 4 (FIN)\n");
        close(s);
        return 1;
    }
    
    // ÉXITO
    printf("TRANSFERENCIA COMPLETADA\n");
    close(s);
    printf("Socket cerrado\n");
    return 0;
}
