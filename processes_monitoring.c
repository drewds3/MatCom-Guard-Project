#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>

#include <glib.h>
#include <gtk/gtk.h>

// Variable del main.c
extern GtkWidget* textview_process;

// Funcion para actualizar textview desde el hilo
static gboolean append_textview_from_thread(gpointer data) {
     const char* mensaje = (const char*)data;
     GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_process));
     GtkTextIter end;
     gtk_text_buffer_get_end_iter(buffer, &end);
     gtk_text_buffer_insert(buffer, &end, mensaje, -1);

     free(data);
     return FALSE;
}

#define PRINT_ALERT(fmt, ...) char _print_buffer[256]; snprintf(_print_buffer, sizeof(_print_buffer), fmt, __VA_ARGS__); g_idle_add(append_textview_from_thread, g_strdup(_print_buffer));

#define CPU_THRESHOLD 80.0
#define MEM_THRESHOLD 80.0
#define MAX_PROCESSES 512
#define BUFFER_SIZE 1024
#define SLEEP_TIME 5

typedef struct {
     int pid;
     char name[256];
     unsigned long utime;
     unsigned long stime;
     unsigned long long total_time;
     double cpu_usage;
     double mem_usage;
} ProcessInfo;

// fopen(const char* filename, const char* mode) -> FILE* o NULL  // Abre un archivo y devuelve el puntero al archivo o NULL si falla
// fgets(char* str, int n, FILE* stream) -> str o NULL  // Lee una línea desde un archivo y la guarda en el buffer
// sscanf(const char* str, const char* format, ...) -> número de asignaciones  // Extrae datos de una cadena según formato
// fclose(FILE* stream) -> 0 éxito, EOF error  // Cierra un archivo abierto y devuelve estado
// snprintf(char* str, size_t size, const char* format, ...) -> número de caracteres escritos (sin nulo)  // Escribe formato en buffer seguro contra desbordes
// strcmp(const char* s1, const char* s2, size_t n) -> 0 igual, <0 s1<s2, >0 s1>s2  // Compara dos cadenas hasta n caracteres

// strtok(char* str, const char* delim) -> puntero al token o NULL  // Divide cadena en tokens usando delimitadores
// strncpy(char* dest, const char* src, size_t n) -> puntero a dest  // Copia hasta n caracteres de una cadena a otra
// strlen(const char* str) -> longitud de str sin nulo  // Calcula la longitud de una cadena
// strtoul(const char* nptr, char* *endptr, int base) -> unsigned long convertido  // Convierte cadena a número unsigned long según base

// opendir(const char* name) -> DIR* o NULL  // Abre un directorio para lectura
// closedir(DIR* dirp) -> 0 éxito, -1 error  // Cierra un directorio abierto
// perror(const char* s) -> void, imprime error en stderr  // Imprime mensaje de error con descripción del sistema
// readdir(DIR* dirp) -> puntero a struct dirent o NULL  // Lee la siguiente entrada de un directorio
// isdigit(int c) -> distinto de 0 si c es dígito, 0 si no  // Verifica si un carácter es un dígito decimal
// atoi(const char* str) -> int convertido  // Convierte cadena a entero (int)
// memcpy(void* dest, const void* src, size_t n) -> puntero a dest  // Copia n bytes de memoria de src a dest

unsigned long long get_total_cpu_time() { // Devuelve el tiempo total de la cpu dado en ciclos de reloj
     // Abre el archivo /proc/stat que contiene varias estadisticas del sistema (incluyendo el tiempo de uso de la CPU)
     FILE* file = fopen("/proc/stat", "r");
     if (!file) return 0;
     // Lee la primera linea, esta linea siempre contiene la palabra cpu y luego 4 numeros que representan distintos tiempos
     char line[BUFFER_SIZE];
     fgets(line, sizeof(line), file);
     // Obtiene el tiempo en modo usuario, modo usuario con baja prioridad, modo kernel y idle
     unsigned long long user, nice, system, idle;
     sscanf(line, "cpu %llu %llu %llu %llu", &user, &nice, &system, &idle);
     fclose(file);
     // Devuelve cuanto tiempo ha pasado usandose la CPU (la suma de los 4)
     return user + nice + system + idle;
}

double get_process_mem_usage(int pid) {
     // Obtiene la direccion del archivo que contiene informacion del proceso y lo abre
     char path[256];
     snprintf(path, sizeof(path), "/proc/%d/status", pid);
     FILE* file = fopen(path, "r");
     if (!file) return 0.0;

     char line[BUFFER_SIZE]; // Buffer para almacenar las lineas
     long vm_rss = 0; // Almacena la cantidad de memoria en kb usada por el proceso
     while (fgets(line, sizeof(line), file)) {
          if (strncmp(line, "VmRSS:", 6) == 0) {
               sscanf(line + 6, "%ld", &vm_rss);
               break;
          }
     }
     fclose(file);

     // Convertir a MB y porcentaje (asumiendo 8GB = 8192MB)
     return (vm_rss / 1024.0) / 8192.0 * 100;
}
int read_process_info(int pid, ProcessInfo* pinfo) {
     // Obtiene información del proceso desde /proc/[pid]/stat
     char path[256];
     snprintf(path, sizeof(path), "/proc/%d/stat", pid);

     FILE* file = fopen(path, "r");
     if (!file) return 0;

     char line[BUFFER_SIZE];
     if (!fgets(line, sizeof(line), file)) { fclose(file); return 0; }
     fclose(file);

     // Extraer campos relevantes del formato de /proc/[pid]/stat
     char* token = strtok(line, " ");
     for (int i = 1; token != NULL; i++, token = strtok(NULL, " ")) {
          switch (i) {
          case 2:  // Nombre del proceso
               strncpy(pinfo->name, token, sizeof(pinfo->name));
               if (pinfo->name[strlen(pinfo->name) - 1] == ')')
                    pinfo->name[strlen(pinfo->name) - 1] = '\0';
               break;
          case 14: // utime
               pinfo->utime = strtoul(token, NULL, 10);
               break;
          case 15: // stime
               pinfo->stime = strtoul(token, NULL, 10);
               break;
          }
     }

     pinfo->pid = pid;
     pinfo->total_time = pinfo->utime + pinfo->stime;
     pinfo->mem_usage = get_process_mem_usage(pid);

     return 1;
}
void print_processes(const ProcessInfo* processes, int count, const char* target) { // Todos los nombres de procesos se guardan iniciando con un '('
     if (strcmp(target, "") == 0) { printf("Procesos detectados: %d\n", count); }
     for (int i = 0; i < count; i++) {
          if (strcmp(target, "") == 0) { printf("PID: %d, Nombre: %s\n, Uso de memoria: %f, Uso de CPU: %f", processes[i].pid, processes[i].name, processes[i].mem_usage, processes[i].cpu_usage); }
          if (strcmp(processes[i].name, target) == 0) { printf("%s detected\n", target); return; }
     }
}
// Monitorea los procesos y detecta anomalías
int read_actual_processes(ProcessInfo* processes) {
     DIR* proc_dir = opendir("/proc");
     if (!proc_dir) { return -1; }
     int proc_count = 0;
     struct dirent* entry; // Apunta a cada entrada del directorio "/proc"
     while ((entry = readdir(proc_dir)) && (proc_count < MAX_PROCESSES)) {
          if (isdigit(entry->d_name[0])) { // Solo considerar directorios con nombres numericos (PIDs)
               int pid = atoi(entry->d_name);
               if (read_process_info(pid, &processes[proc_count])) { proc_count++; } // Si se leyo correctamente incrementar
          }
     }
     closedir(proc_dir);
     return proc_count;
}
int stop = 0;
void *monitor_processes(void *arg) {
     printf("Iniciando sistema de monitoreo de procesos...\n");
     ProcessInfo prev_procs[MAX_PROCESSES] = { 0 };
     unsigned long long prev_total_cpu = get_total_cpu_time();
     stop = 0;
     while (!stop) {
          ProcessInfo curr_procs[MAX_PROCESSES];
          int proc_count = read_actual_processes(curr_procs);
          if (proc_count < 0) { perror("Error abriendo /proc"); exit(EXIT_FAILURE); }

          // Calcular uso de CPU y comparar con iteración anterior
          unsigned long long curr_total_cpu = get_total_cpu_time();
          unsigned long long cpu_delta = curr_total_cpu - prev_total_cpu;

          for (int i = 0; i < proc_count; i++) {
               for (int j = 0; j < MAX_PROCESSES; j++) {
                    if (prev_procs[j].pid == curr_procs[i].pid) {
                         unsigned long long time_diff = curr_procs[i].total_time - prev_procs[j].total_time;
                         curr_procs[i].cpu_usage = (cpu_delta > 0) ? (double)time_diff / cpu_delta * 100.0 : 0.0;

                          // Detectar picos de uso
                         if (curr_procs[i].cpu_usage > CPU_THRESHOLD) {
                              printf("[ALERTA CPU] Proceso %d (%s): %.2f%%\n",
                                   curr_procs[i].pid, curr_procs[i].name, curr_procs[i].cpu_usage);
                         }
                         if (curr_procs[i].mem_usage > MEM_THRESHOLD) {
                              printf("[ALERTA MEM] Proceso %d (%s): %.2f%%\n",
                                   curr_procs[i].pid, curr_procs[i].name, curr_procs[i].mem_usage);
                         }
                         break;
                    }
               }
          }
          print_processes(curr_procs, proc_count, "");

          memcpy(prev_procs, curr_procs, sizeof(ProcessInfo) * MAX_PROCESSES);
          prev_total_cpu = curr_total_cpu;

          sleep(SLEEP_TIME); // Intervalo de monitoreo
     }

     return NULL;
}
void stop_monitoring_processes() { stop = 1; }