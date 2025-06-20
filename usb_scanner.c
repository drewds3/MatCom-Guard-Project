#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <cjson/cJSON.h>
#include <stdbool.h>
#include <glib.h>
#include <gtk/gtk.h>

// Variable volatil para detener el escaneo
volatile gboolean escaneo_activo = TRUE; 

// Variable del main.c
extern GtkWidget *textview_resultados;

// Funcion para actualizar textview desde el hilo
static gboolean append_textview_from_thread(gpointer data)
{
    const char *mensaje = (const char *)data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_resultados));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, mensaje, -1);
    
    free(data);
    return FALSE;
}

int contar_dispositivos()
{
    int count = 0;
    DIR *dir = opendir("/media/");
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        // Se omiten el directorio actual y su padre
        if (strcmp(entry->d_name, ".") != 0 
        && strcmp(entry->d_name, "..") != 0)
        {
            count++;
        }
    }

    return count;
}

// Metodo para escanear dispositivos conectados
char ** detectar_dispositivos_usb(cantidad)
{
    const char *usuario = getenv("USER");

    if(usuario == NULL)
    {
        perror("No se pudo obtener el nombre de usuario");
        return 0;
    }

    //Se reserva la memoria a usar en la lista
   // Variables globales
   char **lista_dispositivos = malloc(sizeof(char *) * cantidad);
    
    if(lista_dispositivos == NULL) return 0;

    char ruta[256];
    snprintf(ruta, sizeof(ruta), "/media/%s", usuario);
    
    DIR *dir;
    struct dirent *entry;
    
    // Se abre el directorio de montaje
    dir = opendir(ruta);
    if (!dir)
    {
        perror("No se pudo abrir el directorio de montaje");
        return 0;
    }

    int i = 0; 

    printf("Dispositivos conectados:\n");
    while ((entry = readdir(dir)) != NULL)
    {
        // Se omiten el directorio actual y su padre
        if (strcmp(entry->d_name, ".") != 0 
        && strcmp(entry->d_name, "..") != 0)
        {
            printf("- %s\n", entry->d_name);

            // Agregar el dispositivo a la lista
            lista_dispositivos[i] = strdup(entry->d_name);

            printf("%s\n", lista_dispositivos[i]);

            char mensaje[512];
            snprintf(mensaje, sizeof(mensaje), "USB Detectado: %s\n", lista_dispositivos[i]);
            g_idle_add(append_textview_from_thread, g_strdup(mensaje));

            if(lista_dispositivos[i] == NULL)
            {
                perror("Error al asignar memoria");
                closedir(dir);
            }

            i++;
        }
    }

    closedir(dir);
    return lista_dispositivos;
}

void escanear_recursivo(const char *ruta, FILE *json)
{
    // Se accede al dispositivo
    DIR *dir;
    dir = opendir(ruta);
    if (!dir)
    {
        perror("No se pudo abrir el directorio");
        return;
    }
    
    // Se accede a todos los archivos y carpetas
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        // Se omiten el actual y padre para evitar bucles
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

        // Construir ruta completa
        char ruta_completa[1024];
        
        // Si hay una ruta demasiado larga se omite
        if (snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta, entry->d_name) >= sizeof(ruta_completa))
        {
            fprintf(stderr, "Ruta demasiado larga, se omite: %s/%s\n", ruta, entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(ruta_completa, &st) != 0)
        {
            perror("Error al acceder con stat");
            continue;
        }

        // Se imprimen recursivamente todos los elementos en el dispositivo
        if (S_ISDIR(st.st_mode))
        {
            printf("Carpeta: %s\n", ruta_completa);
            guardar_archivo_json(json, ruta_completa, &st, "Carpeta");
            escanear_recursivo(ruta_completa, json);
        }
        else if (S_ISREG(st.st_mode))
        {
            printf("Archivo: %s\n", ruta_completa);
            guardar_archivo_json(json, ruta_completa, &st, "Archivo");
        }
        else
        {
            printf("Otro tipo: %s\n", ruta_completa);
            guardar_archivo_json(json, ruta_completa, &st, "Otro");
        }
    }

    closedir(dir);
}

void guardar_archivo_json(FILE *json, const char *ruta, struct stat *info, const char *tipo)
{
    fprintf(json,
        "  {\n"
        "  \"ruta\": \"%s\",\n"
        "  \"tipo\": \"%s\",\n"
        "  \"tamano\": %ld, \n"
        "  \"modificado\": %ld, \n"
        "  \"permisos\": %o, \n" 
        "  \"uid\": %d \n"
        "},\n",
        ruta,
        tipo,
        info->st_size,
        info->st_mtime,
        info->st_uid);
}

void guardar_baseline(const char *ruta, const char *Json)
{
    FILE *json = fopen(Json, "w");
    if(!json)
    {
        perror("No se pudo abrir el archivo json");
        return;
    }

    fprintf(json, "[\n");
    escanear_recursivo(ruta, json);

    fseek(json, -2, SEEK_END);
    fprintf(json, "\n]\n");
    fclose(json);
}

void resetear_baseline(const char *ruta, const char *Json)
{
    FILE *json = fopen(Json, "w");
    if(!json)
    {
        perror("No se pudo resetear la baseline");
        return;
    }

    fclose(json);
}

//--------------------Comparacion-------------------------------------------
// Estructura para manejar facilmente la informacion de los elementos en el dispositivo
typedef struct
{
    char ruta[1024];
    char tipo[16];
    long tamano;
    long modificado;
    int permisos;
    int uid;
} InfoArchivo;

// Para convertir un archivo .json en una lista de estructuras
InfoArchivo *leer_json(const char *json, int *cantidad)
{
    // Se lee el archivo .json.
    FILE *archivoJSON = fopen(json, "r");
    if(!archivoJSON)
    {
        perror("No se pudo abrir el archivo .json");
        return NULL;
    }

    // Se lee el contenido del archivo
    fseek(archivoJSON, 0, SEEK_END);
    long len = ftell(archivoJSON);
    fseek(archivoJSON, 0, SEEK_SET);
    
    // Se guarda el contenido en un "string"
    char *contenido = malloc(len + 1);
    fread(contenido, 1, len, archivoJSON);
    contenido[len] = '\0'; // Necesario para que sea una cadena valida
    fclose(archivoJSON);

    // Parseamos el contenido como un objeto json
    cJSON *jsonParseado = cJSON_Parse(contenido);
    if(!jsonParseado)
    {
        fprintf(stderr, "Error al parsear el .json\n");
        free(contenido);
        return NULL;
    }

    // Reservamos memoria para para el total de estructuras
    int n = cJSON_GetArraySize(jsonParseado);
    InfoArchivo *lista = malloc(sizeof(InfoArchivo) * n);

    // Se extrae cada campo y se guarda en la estructura
    for (int i = 0; i < n; i++)
    {
        cJSON *item = cJSON_GetArrayItem(jsonParseado, i);
        if(!item)
        {
            fprintf(stderr, "Error: elemento #%d del .json es NULL\n", i);
            continue;
        }

        strcpy(lista[i].ruta, cJSON_GetObjectItem(item, "ruta")->valuestring);
        strcpy(lista[i].tipo, cJSON_GetObjectItem(item, "tipo")->valuestring);
        lista[i].tamano = cJSON_GetObjectItem(item, "tamano")->valuedouble;
        lista[i].modificado = cJSON_GetObjectItem(item, "modificado")->valuedouble;
        lista[i].permisos= cJSON_GetObjectItem(item, "permisos")->valueint;
        lista[i].uid = cJSON_GetObjectItem(item, "uid")->valueint;
    }
    
    // Liberamos memoria del contenido del json
    cJSON_Delete(jsonParseado);
    free(contenido);

    // Se guarda la la cantidad de estructuras en un puntero para usarse mas adelante
    *cantidad = n;  
    return lista;
}

// Comparar dos rutas
int comparar_ruta(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

// Comparar dos listas de estructuras de info de los archivos
void comparar_baselines(const char *inicial, const char *actual)
{
    // Se leen los .json y parsean en listas de estructuras
    int n1 = 0, n2 = 0;
    InfoArchivo *b1 = leer_json(inicial, &n1);
    InfoArchivo *b2 = leer_json(actual, &n2);

    if (!b1 || !b2)
    {
        printf("Alguna de las listas esta vacia");
        return;
    } 

    // Se comparan las baselines
    printf("Comparando baselines...\n");

    // Se verifica si todos los archivos se aun se encuentran en el dispositivo
    // y se mantienen sin cambios
    for (int i= 0; i < n1; i++)
    {
        int encontrado = 0;
        for (int j = 0; j < n2; j++)
        {
            if(comparar_ruta(b1[i].ruta, b2[j].ruta))
            {
                encontrado = 1;

                if(b1[i].tamano != b2[j].tamano || b1[i].modificado != b2[j].modificado
                || b1[i].permisos != b2[j].permisos || b1[i].uid != b2[j].uid
                || strcmp(b1[i].tipo, b2[j].tipo) != 0)
                {
                    printf("Advertencia: %s modificado: %s\n", b1[i].tipo, b1[i].ruta);
                
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Advertencia: %s modificado: %s\n", b1[i].tipo, b1[i].ruta);
                    g_idle_add(append_textview_from_thread, g_strdup(msg));
                }
                
                break;
            }
        }
        if(!encontrado)
        {
            printf("Advertencia: %s eliminado: %s\n", b1[i].tipo, b1[i].ruta);

            char msg[256];
            snprintf(msg, sizeof(msg), "Advertencia: %s eliminado: %s\n", b1[i].tipo, b1[i].ruta);
            g_idle_add(append_textview_from_thread, g_strdup(msg));
        } 
    }

    // Se comprueba que no se hayan creado nuevos elementos en el dispositivo
    for(int j = 0; j < n2; j++)
    {
        int encontrado = 0;

        for (int i = 0; i < n1; i++)
        {
            if(comparar_ruta(b2[j].ruta, b1[i].ruta))
            {
                encontrado = 1;
                break;
            }
        }

        if(!encontrado)
        {
            printf("%s nuevo detectado en: %s\n", b2[j].tipo, b2[j].ruta);
            char msg[256];
            snprintf(msg, sizeof(msg), "Advertencia: %s nuevo detectado: %s\n", b2[j].tipo, b2[j].ruta);
            g_idle_add(append_textview_from_thread, g_strdup(msg));
        }
    }

    // Se libera la memoria usada
    free(b1);
    free(b2);
}

//----------------------------Hilo USB------------------------------------------
// Metodo general del hilo
void *thread_scanner_usb(void *arg)
{
    bool baselines_guardadas = false;

    while(escaneo_activo)
    {
        // Obtener lista de dispositivos
        int cantidad = contar_dispositivos();
        char **lista_dispositivos = detectar_dispositivos_usb(cantidad);
        
        // Por cada USB, manejar las baselines en formato JSON
        for(int i = 0; i < cantidad; i++)
        {   
            char archivo_base[256];
            snprintf(archivo_base, sizeof(archivo_base), "baselines_iniciales/baseline_%d.json", i);
            
            const char *usuario = getenv("USER");
            
            char ruta_dispositivo[256];
            snprintf(ruta_dispositivo, sizeof(ruta_dispositivo), "/media/%s/%s", usuario, lista_dispositivos[i]);
            printf("%s\n", ruta_dispositivo);

            if(!baselines_guardadas)
            {
                // Crear la baseline inicial
                guardar_baseline(ruta_dispositivo, archivo_base);

                char msg[256];
                snprintf(msg, sizeof(msg), "Baseline guardada en JSON: %s\n", archivo_base);
                g_idle_add(append_textview_from_thread, g_strdup(msg));
            }
            else
            {
                // Crear baseline actual
                char archivo_actual[256];
                snprintf(archivo_actual, sizeof(archivo_actual), "baselines_actuales/baseline_actual_%d.json", i);
                guardar_baseline(ruta_dispositivo, archivo_actual);

                char msg[256];
                snprintf(msg, sizeof(msg), "Comparacion iniciada entre %s y %s\n", archivo_base, archivo_actual);
                g_idle_add(append_textview_from_thread, g_strdup(msg));

                comparar_baselines(archivo_base, archivo_actual);

                snprintf(msg, sizeof(msg), "Comparacion finalizada entre %s y %s\n", archivo_base, archivo_actual);
                g_idle_add(append_textview_from_thread, g_strdup(msg));
            }
        }

        baselines_guardadas = true;

        // Liberar memoria de la lista
        for(int i = 0; i < cantidad; i++)
        free(lista_dispositivos[i]);

        free(lista_dispositivos);

        // Esperar 60 segundos
        for (int t = 0; t < 5 && escaneo_activo; t++)
        {
            sleep(1);
        }
    }

    baselines_guardadas = false;

    char mensaje[256];
    snprintf(mensaje, sizeof(mensaje), "Escaneo detenido.\n");
    g_idle_add(append_textview_from_thread, g_strdup("Escaneo detenido.\n"));
    return NULL;
}