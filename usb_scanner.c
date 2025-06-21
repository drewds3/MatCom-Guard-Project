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

// Variables globales para controlar USBs
char **lista_global = NULL;
int tamGlobal = 0;

//-----------------Seccion para mandar actualizar la interfaz--------------------
// Funcion para actualizar textview desde el hilo
static gboolean append_textview_from_thread(gpointer data)
{
    const char *mensaje = (const char *)data;

    // Obtiene la direccion real en memoria donde se guarda el texto a mostrar
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_resultados));
    
    // Se obtiene la ultima posicion
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    // Se inserta
    gtk_text_buffer_insert(buffer, &end, mensaje, -1);
    
    free(data);
    return FALSE; // FALSE para que no se vuelva a ejecutar
}

//------------------------Escaneo de dispositivos USB--------------------------
// Cuenta los dispositivos
int contar_dispositivos()
{
    int count = 0;

    // Se abre la carpeta
    DIR *dir = opendir("/media/Andriu/");
    struct dirent *entry;

    //
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

// Escaneo de dispositivos conectados
char ** detectar_dispositivos_usb(int cantidad_esperada, int *cantidad_real)
{
    // Se extrae el nombre del usuario
    const char *usuario = getenv("USER");

    if(usuario == NULL)
    {
        perror("No se pudo obtener el nombre de usuario");
        return NULL;
    }

    //Se reserva la memoria a usar en la lista
   char **lista_dispositivos = malloc(sizeof(char *) * cantidad_esperada);
   
    if(lista_dispositivos == NULL) return NULL;

    // Se crea la direccion
    char ruta[256];
    snprintf(ruta, sizeof(ruta), "/media/%s", usuario);
    
    // Se crean las variables del directorio abierto
    // y la entrada leida dentro del directorio
    DIR *dir;
    struct dirent *entry;
    
    // Se abre el directorio de montaje
    dir = opendir(ruta);
    if (!dir)
    {
        perror("No se pudo abrir el directorio de montaje");
        return NULL;
    }

    // Se extrae el nombre de los dispositivos conectados
    printf("Dispositivos conectados:\n");
    
    int i = 0;

    while ((entry = readdir(dir)) != NULL)
    {  
        
        // Se omiten el directorio actual y su padre
        if (strcmp(entry->d_name, ".") != 0 
        && strcmp(entry->d_name, "..") != 0)
        {
            printf("- %s\n", entry->d_name);

            // Agregar el nombre del dispositivo a la lista
            lista_dispositivos[i] = strdup(entry->d_name);

            printf("- %s\n", lista_dispositivos[i]);

            if(lista_dispositivos[i] == NULL)
            {
                perror("Error al asignar nombre de dispositivo");
                closedir(dir);
            }

            i++;
        }
    }

    // Se captura la cantidad real de dispositivos conectados
    // por si desconectan una memoria a mitad de iteracion
    *cantidad_real = i; 

    closedir(dir);
    return lista_dispositivos;
}

//--------------------------Informacion de elementos----------------------------
// Para extraer la informacion de todos los elementos en el dispositivo
void escanear_recursivo(const char *ruta, FILE *json)
{
    // Se accede al dispositivo
    DIR *dir;
    struct dirent *entry;

    dir = opendir(ruta);
    if (!dir)
    {
        perror("No se pudo abrir el directorio");
        return;
    }
    
    // Se accede a todos los archivos y carpetas
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

        // Se declara la variable para obtener informacion de los archivos
        struct stat st;
        if (stat(ruta_completa, &st) != 0)
        {
            perror("Error al acceder con stat");
            continue;
        }

        // Se guardan recursivamente todos los elementos en el dispositivo y su informacion
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

// Para guardar el archivo json correctamente
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

// Para guardar la informacion de los archivos en un .json
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

// Para resetear el .json
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
InfoArchivo *leer_json(const char *json, int *cantidad_esperada)
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
        lista[i].permisos = cJSON_GetObjectItem(item, "permisos")->valueint;
        lista[i].uid = cJSON_GetObjectItem(item, "uid")->valueint;
    }
    
    // Liberamos memoria del contenido del json
    cJSON_Delete(jsonParseado);
    free(contenido);

    // Se guarda la cantidad_esperada de estructuras en un puntero para usarse mas adelante
    *cantidad_esperada = n;  
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

//--------------------------Manejo de listas de nombres de USB--------------------------------------
// Verifica si un dispositivo usb esta en otra lista
int existeEnLista(const char *str, char **lista, int tam) 
{
    for (int i = 0; i < tam; i++) 
    {
        if (strcmp(str, lista[i]) == 0) 
        return 1;
    }
    
    return 0;
}

// Libera una lista de strings (nombres de dispositivos)
void liberarLista(char **lista, int tam) 
{
    for (int i = 0; i < tam; i++) 
    free(lista[i]);
    
    free(lista);
}

// Se actualiza la lista global
char **copiarLista(char **origen, int tam) 
{
    char **nuevaLista = malloc(tam * sizeof(char*));
    
    for (int i = 0; i < tam; i++) 
    {
        nuevaLista[i] = malloc(strlen(origen[i]) + 1);
        strcpy(nuevaLista[i], origen[i]);
    }

    return nuevaLista;
}

// Compara la lista local con la global y actualiza la global
void compararListasYActualizar(char ***globalLista, int *tamGlobal, char **localLista, int tamLocal, bool *not_zero) 
{   
    if(not_zero)
    {
        // Buscar nuevos elementos (por cada local buscar en global)
        for (int i = 0; i < tamLocal; i++) 
        {
            if (!existeEnLista(localLista[i], *globalLista, *tamGlobal)) 
            {
                printf("USB Detectado: %s\n", localLista[i]);
            
                char mensaje[512];
                snprintf(mensaje, sizeof(mensaje), "USB Detectado: %s\n", localLista[i]);
                g_idle_add(append_textview_from_thread, g_strdup(mensaje));
            }
        }

        // Buscar eliminados (por cada en global buscar en local)
        for (int i = 0; i < *tamGlobal; i++) 
        {
            if (!existeEnLista((*globalLista)[i], localLista, tamLocal)) 
            {
                printf("USB Expulsado: %s\n", (*globalLista)[i]);
            
                char mensaje[512];
                snprintf(mensaje, sizeof(mensaje), "USB Expulsado: %s\n", (*globalLista)[i]);
                g_idle_add(append_textview_from_thread, g_strdup(mensaje));
            }
        }
    }
    
    // Se libera memoria
    liberarLista(*globalLista, *tamGlobal);
    *globalLista = NULL; // Para evitar doble free()

    // Hacer copia profunda en la lista global de la lista actual (local)
    if(tamLocal != 0)
    {
        *globalLista = copiarLista(localLista, tamLocal);
        *not_zero = true; 
    }     
    
    *tamGlobal = tamLocal;
}

//----------------------------Hilo USB------------------------------------------
// Metodo general del hilo
void *thread_scanner_usb(void *arg)
{
    // Booleanos necesarios
    bool baselines_guardadas = false; // Para saber si ya hay una baseline inicial
    bool not_zero = false;  // Para evitar violacion de segmento con el tamano de la lista local
    escaneo_activo = TRUE;  // Para salir del while       

    // Bucle de escaneo hasta que se presione el boton de detener
    while(escaneo_activo)
    {
        // Obtener lista de dispositivos y su tamano
        int cantidad_esperada = contar_dispositivos();
        int cantidad_real = 0;
        char **lista_dispositivos = detectar_dispositivos_usb(cantidad_esperada, &cantidad_real);
        
        // Detectar nuevos dispositivos USB conectados y desconectados
        compararListasYActualizar(&lista_global, &tamGlobal, lista_dispositivos, cantidad_esperada, &not_zero);
        
        // Por cada USB, manejar las baselines en formato JSON
        for(int i = 0; i < cantidad_esperada; i++)
        {    
            // Se obtiene la direccion de cada dispositivo
            const char *usuario = getenv("USER");
            
            char ruta_dispositivo[256];
            snprintf(ruta_dispositivo, sizeof(ruta_dispositivo), "/media/%s/%s", usuario, lista_dispositivos[i]);
            printf("%s\n", ruta_dispositivo);
            
            // Se crea la direccion del baseline inicial (o anterior)
            char archivo_base[256];
            snprintf(archivo_base, sizeof(archivo_base), "baselines_iniciales/baseline_%d.json", i);
            
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

                // Se comparan las baselines
                char msg[256];
                snprintf(msg, sizeof(msg), "Comparacion iniciada entre %s y %s\n", archivo_base, archivo_actual);
                g_idle_add(append_textview_from_thread, g_strdup(msg));

                comparar_baselines(archivo_base, archivo_actual);
                
                // Se actualiza la baseline inicial para que no vuelva a mostrar las mismas advertencias
                char comando[256];
                snprintf(comando, sizeof(comando), "cp %s %s", archivo_actual, archivo_base);
                system(comando);

                // Se notifica en la interfaz
                snprintf(msg, sizeof(msg), "Comparacion finalizada entre %s y %s\n\n", archivo_base, archivo_actual);
                g_idle_add(append_textview_from_thread, g_strdup(msg));
            }
        }

        baselines_guardadas = true; // Ya se guardo al menos una vez
        
        // Liberar memoria de la lista
        liberarLista(lista_dispositivos, cantidad_real);
        
        // Esperar 60 segundos
        for (int t = 0; t < 5 && escaneo_activo; t++)
        {
            sleep(1);
        }
    }

    // Ultimas lineas para cuando finalice el codigo
    baselines_guardadas = false;

    char mensaje[256];
    snprintf(mensaje, sizeof(mensaje), "Escaneo detenido.\n");
    g_idle_add(append_textview_from_thread, g_strdup("Escaneo detenido.\n"));
    return NULL;
}