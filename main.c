#include "usb_scanner.h"
#include <stdlib.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib.h>

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

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "MatCom Guard");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), box);

    // Scanner USB button
    GtkWidget *button_usb = gtk_button_new_with_label("Iniciar escaneo USB");
    gtk_widget_set_size_request(button_usb, 100, 40);
    gtk_widget_set_halign(button_usb, GTK_ALIGN_CENTER);
    g_signal_connect(button_usb, "clicked", G_CALLBACK(iniciar_escaneo_usb), NULL);
    gtk_box_pack_start(GTK_BOX(box), button_usb, FALSE, FALSE, 0);

    // Output scanner usb
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_halign(scrolled_window, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(scrolled_window, 600, 400);
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, FALSE, FALSE, 0);
    textview_resultados = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview_resultados), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), textview_resultados);

    //Scanner USB button stop
    GtkWidget *button_detener = gtk_button_new_with_label("Detener escaneo USB");
    gtk_widget_set_halign(button_detener, GTK_ALIGN_CENTER);
    g_signal_connect(button_detener, "clicked", G_CALLBACK(detener_escaneo_usb), NULL);
    gtk_widget_set_size_request(button_detener, 100, 40);
    gtk_box_pack_start(GTK_BOX(box), button_detener, FALSE, FALSE, 0);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_resultados));
    gtk_text_buffer_set_text(buffer, "Informes de escaneo USB:\n", -1);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
} 

//gcc main.c usb_scanner.c $(pkg-config --cflags --libs gtk+-3.0) -lcjson
//./a.out