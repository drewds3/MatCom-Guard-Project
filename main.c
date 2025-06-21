#include "usb_scanner.h"
#include "local_port_scanner.h"
#include "processes_monitoring.h"
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib.h>

//------------------------------------------USB--------------------------------------------------
// Variable externa volatil
extern volatile gboolean escaneo_activo;

// Hilo para el escaneo de usb
GThread *hilo_usb = NULL;

// Output para mostrar resultados del escaneo USB
GtkWidget *textview_resultados = NULL;

// Funcion para detener el escaneo
void detener_escaneo_usb(GtkButton *btn, gpointer use_data)
{
    if(hilo_usb)
    {
        escaneo_activo = FALSE;
        g_thread_join(hilo_usb);
        hilo_usb = NULL;
    }
}


// Funcion para el escaneo de usb
static void iniciar_escaneo_usb(GtkWidget *widget, gpointer data)
{
    if(!hilo_usb)
    {
        hilo_usb = g_thread_new("escaneo_usb", thread_scanner_usb, NULL);
    }
}

//---------------------------------Puertos Locales-----------------------------------------------

// Hilo para el escaneo de puertos locales
GThread *hilo_lp = NULL;

// Output para mostrar resultados del escaneo de puertos locales
GtkWidget *textview_log = NULL;

// Funcion para el escaneo de puertos locales
static void iniciar_escaneo_puertos(GtkWidget *widget, gpointer data)
{
    if(!hilo_lp)
    {
        hilo_lp = g_thread_new("escaneo_lp", scan_and_report, NULL);
        g_thread_join(hilo_lp);
        hilo_lp = NULL;
    }
}

//------------------------------------Procesos-----------------------------------------------

// Hilo para el escaneo de puertos locales
GThread *hilo_pr = NULL;

// Output para mostrar resultados del escaneo de puertos locales
GtkWidget *textview_process = NULL;

// Funcion para el escaneo de puertos locales
static void iniciar_escaneo_procesos(GtkWidget *widget, gpointer data)
{
    if(!hilo_pr)
    {
        hilo_pr = g_thread_new("escaneo_pr", monitor_processes, NULL); // Falta cambiar el 1er NULL por el metodo
        g_thread_join(hilo_pr);
        hilo_pr = NULL;
    }
}

// Funcion para detener el escaneo de procesos
void detener_escaneo_procesos(GtkButton *btn, gpointer use_data)
{
    if(hilo_pr)
    {
        stop_monitoring_processes();
        g_thread_join(hilo_pr);
        hilo_pr = NULL;
    }
}

//------------------------------------Interfaz----------------------------------------------------
int main(int argc, char *argv[])
{
    //-----------------------------Iniciar y crear ventana--------------------------------------
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "MatCom Guard");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

    // Box principal
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_add(GTK_CONTAINER(window), box);

    // Boxes secundario
    GtkWidget *box_usb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *box_lp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *box_pr = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    gtk_box_pack_start(GTK_BOX(box), box_usb, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), box_lp, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), box_pr, TRUE, TRUE, 0);

    //------------------------------------USB--------------------------------------------------

    // Scanner USB button
    GtkWidget *button_usb = gtk_button_new_with_label("Iniciar escaneo USB");
    gtk_widget_set_size_request(button_usb, 100, 40);
    gtk_widget_set_halign(button_usb, GTK_ALIGN_CENTER);
    g_signal_connect(button_usb, "clicked", G_CALLBACK(iniciar_escaneo_usb), NULL);
    gtk_box_pack_start(GTK_BOX(box_usb), button_usb, FALSE, FALSE, 0);

    // Output scanner usb
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_halign(scrolled_window, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(scrolled_window, 400, 700);
    gtk_box_pack_start(GTK_BOX(box_usb), scrolled_window, FALSE, FALSE, 0);
    textview_resultados = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview_resultados), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), textview_resultados);

    //Scanner USB button stop
    GtkWidget *button_detener = gtk_button_new_with_label("Detener escaneo USB");
    gtk_widget_set_halign(button_detener, GTK_ALIGN_CENTER);
    g_signal_connect(button_detener, "clicked", G_CALLBACK(detener_escaneo_usb), NULL);
    gtk_widget_set_size_request(button_detener, 100, 40);
    gtk_box_pack_start(GTK_BOX(box_usb), button_detener, FALSE, FALSE, 0);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_resultados));
    gtk_text_buffer_set_text(buffer, "Informes de escaneo USB:\n", -1);

    //--------------------------------------Puertos-------------------------------------------

    // Scanner Local Ports button
    GtkWidget *button_lp = gtk_button_new_with_label("Iniciar escaneo de puertos locales");
    gtk_widget_set_size_request(button_lp, 100, 40);
    gtk_widget_set_halign(button_lp, GTK_ALIGN_CENTER);
    g_signal_connect(button_lp, "clicked", G_CALLBACK(iniciar_escaneo_puertos), NULL);
    gtk_box_pack_start(GTK_BOX(box_lp), button_lp, FALSE, FALSE, 0);

    // Output scanner puertos locales
    GtkWidget *scrolled_lp = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_halign(scrolled_lp, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(scrolled_lp, 400, 700);
    gtk_box_pack_start(GTK_BOX(box_lp), scrolled_lp, FALSE, FALSE, 0);
    textview_log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview_log), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_lp), textview_log);

    GtkTextBuffer *buffer2 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_log));
    gtk_text_buffer_set_text(buffer2, "Informes de escaneo de Puertos Locales:\n", -1);

    //-------------------------------Procesos----------------------------------------------------

    // Scanner process button
    GtkWidget *button_pr = gtk_button_new_with_label("Iniciar escaneo de procesos");
    gtk_widget_set_size_request(button_pr, 100, 40);
    gtk_widget_set_halign(button_pr, GTK_ALIGN_CENTER);
    g_signal_connect(button_pr, "clicked", G_CALLBACK(iniciar_escaneo_procesos), NULL);
    gtk_box_pack_start(GTK_BOX(box_pr), button_pr, FALSE, FALSE, 0);

    // Output scanner procesos
    GtkWidget *scrolled_pr = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_halign(scrolled_pr, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(scrolled_pr, 400, 700);
    gtk_box_pack_start(GTK_BOX(box_pr), scrolled_pr, FALSE, FALSE, 0);
    textview_process = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview_process), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_pr), textview_process);

    //Scanner process button stop
    GtkWidget *button_detener2 = gtk_button_new_with_label("Detener escaneo Procesos");
    gtk_widget_set_halign(button_detener2, GTK_ALIGN_CENTER);
    g_signal_connect(button_detener2, "clicked", G_CALLBACK(detener_escaneo_procesos), NULL);
    gtk_widget_set_size_request(button_detener2, 100, 40);
    gtk_box_pack_start(GTK_BOX(box_pr), button_detener2, FALSE, FALSE, 0);

    GtkTextBuffer *buffer3 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_process));
    gtk_text_buffer_set_text(buffer3, "Informes de escaneo de Procesos:\n", -1);

    //-------------------------------Fin------------------------------------------------------------

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
} 

//gcc main.c usb_scanner.c local_port_scanner.c $(pkg-config --cflags --libs gtk+-3.0) -lcjson
//./a.out