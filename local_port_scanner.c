#include "local_port_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <glib.h>
#include <gtk/gtk.h>


#define MAX_PORTS 1024

#define NUM_HILOS 20

typedef struct {
    int puerto;
    int abierto;
    const char* servicio;
    int sospechoso;
} ResultadoPuerto;

ResultadoPuerto resultados[MAX_PORTS];
int total_resultados = 0;
pthread_mutex_t mutex_resultados = PTHREAD_MUTEX_INITIALIZER;


// Esta variable debe definirse externamente, desde un main.c
extern GtkTextView *textview_log;

static gboolean append_textview_from_thread(gpointer data)
{
    const char *mensaje = (const char *)data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview_log);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, mensaje, -1);
    free(data);
    return FALSE; // Para que se ejecute solo una vez
}

//Lista de servicios comunes
const char* obtain_common_service(int puerto)
{
    switch (puerto)
    {
        case 21: return "FTP";
        case 22: return "SSH";
        case 23: return "Telnet";
        case 25: return "SMTP";
        case 53: return "DNS";
        case 80: return "HTTP";
        case 110: return "POP3";
        case 143: return "IMAP";
        case 443: return "HTTPS";
        case 3306: return "MySQL";
        case 8080: return "HTPP-alt";
        default: return NULL;
    }
}

//Estructura para pasar rango a hilo
typedef struct
{
    int inicio;
    int fin;
} RangoPuertos;

void* escanear_rango(void* arg)
{
    RangoPuertos* rango = (RangoPuertos*)arg;
    struct sockaddr_in addr;

    for (int puerto = rango->inicio; puerto<=rango->fin; puerto++)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        addr.sin_family = AF_INET;
        addr.sin_port = htons(puerto);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        int estado = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        ResultadoPuerto r;
        r.puerto = puerto;
        r.abierto = (estado == 0);
        r.servicio = obtain_common_service(puerto);
        r.sospechoso = (r.abierto && r.servicio == NULL);

        close(sock);

        if (r.abierto)
        {
            pthread_mutex_lock(&mutex_resultados);
            resultados[total_resultados++] = r;
            pthread_mutex_unlock(&mutex_resultados);
        }
    }
    return NULL;
}

void scan_local_ports(int inicio, int fin)
{
    pthread_t hilos[NUM_HILOS];
    RangoPuertos rangos[NUM_HILOS];
    int total = fin - inicio + 1;
    int bloque = total / NUM_HILOS;

    for (int i = 0; i < NUM_HILOS; i++)
    {
        rangos[i].inicio = inicio + i * bloque;
        rangos[i].fin = (i == NUM_HILOS - 1) ? fin : (rangos[i].inicio + bloque -1 );

        pthread_create(&hilos[i], NULL, escanear_rango, &rangos[i]);
    }

    for (int i = 0; i < NUM_HILOS; i++)
    {
        pthread_join(hilos[i], NULL);
    }
}


void print_report() {
    // Se limpia el buffer
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview_log);
    gtk_text_buffer_set_text(buffer, "", -1);

    // Línea 1: título del reporte
    g_idle_add(append_textview_from_thread, g_strdup("---- Reporte de Puertos Escaneados ----\n"));

    // Líneas 2 y 3: detalle de puertos abiertos
    for (int i = 0; i < total_resultados; i++) {
        char linea[200];
        if (resultados[i].sospechoso) {
            snprintf(linea, sizeof(linea), "[ALERTA] Puerto %d abierto (posible backdoor o servicio desconocido).\n", resultados[i].puerto);
        } else {
            snprintf(linea, sizeof(linea), "[OK] Puerto %d (%s) abierto.\n", resultados[i].puerto, resultados[i].servicio);
        }
        g_idle_add(append_textview_from_thread, g_strdup(linea));
    }
}

void *scan_and_report(void *arg)
{
    total_resultados = 0;  // se reinicia el conteo para evitar duplicados
    scan_local_ports(1, 1024);
    print_report();

    return NULL;
}