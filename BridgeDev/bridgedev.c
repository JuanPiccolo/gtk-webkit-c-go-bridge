#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdbool.h>
#include <string.h>
#include "libengine.h"
#include <gio/gio.h>

#define release_go_string(p) free(p)

GtkApplication *global_app = NULL;
static char *FirstWindowName = NULL;

// Forward declarations
static void on_script_message_received(WebKitUserContentManager *m, WebKitJavascriptResult *r, gpointer window_ptr);
static void on_window_destroy(GtkWidget* widget, gpointer user_data);

// --- GO-TO-C COMMANDS ---

void RunJavaScriptInWindow(void* web_view_ptr, char* js_code) {
    if (!web_view_ptr || !WEBKIT_IS_WEB_VIEW((WebKitWebView*)web_view_ptr)) return;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    webkit_web_view_evaluate_javascript(web_view, js_code, -1, NULL, NULL, NULL, NULL, NULL);
}

bool RunJavaScriptByWindowName(char* name, char* js_code) {
    if (!global_app) return false;
    GList *windows = gtk_application_get_windows(global_app);
    for (GList *l = windows; l != NULL; l = l->next) {
        GtkWindow *win = GTK_WINDOW(l->data);
        if (g_strcmp0(gtk_window_get_title(win), name) == 0) {
            GtkWidget *web_view = gtk_bin_get_child(GTK_BIN(win));
            if (web_view && WEBKIT_IS_WEB_VIEW(web_view)) {
                webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(web_view), js_code, -1, NULL, NULL, NULL, NULL, NULL);
                return true;
            }
        }
    }
    return false; 
}

static void kill_app_on_main_window_close(GtkWidget* widget, gpointer user_data) {
    // Keeping your "Nuclear Option" — it's deterministic.
    exit(0); 
}

void* OpenPhysicalWindow(char* name, char* type, char* source, int w, int h, char* fileloc) {
    if (!global_app) return NULL;

    GtkWidget *window = gtk_application_window_new(global_app);
    gtk_window_set_title(GTK_WINDOW(window), name);
    gtk_window_set_default_size(GTK_WINDOW(window), w, h);

    WebKitUserContentManager *manager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(manager, "c_bridge");
    
    GtkWidget *web_view = webkit_web_view_new_with_user_content_manager(manager);

    // GROK WIN: Release our reference to the manager now that the web_view owns it.
    g_object_unref(manager);

    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(web_view));
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);

    gtk_container_add(GTK_CONTAINER(window), web_view);

    if (FirstWindowName == NULL) {
        FirstWindowName = g_strdup(name); 
    }

    g_signal_connect(manager, "script-message-received::c_bridge", 
                     G_CALLBACK(on_script_message_received), web_view);
    
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), g_strdup(name));

    if (g_strcmp0(name, FirstWindowName) == 0) {
        g_signal_connect(window, "destroy", G_CALLBACK(kill_app_on_main_window_close), NULL);
    }

    if (type != NULL && strcmp(type, "HTMLAddress") == 0) {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), source);
    } else if (source != NULL) {
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), source, fileloc);
    }

    gtk_widget_show_all(window);
    return (void*)web_view; 
}

// --- THE AIRLOCK (C-TO-GO) ---

static void on_script_message_received(WebKitUserContentManager *m, WebKitJavascriptResult *r, gpointer window_ptr) {
    if (!window_ptr || !WEBKIT_IS_WEB_VIEW((WebKitWebView*)window_ptr)) {
        webkit_javascript_result_unref(r);
        return;
    }
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(window_ptr);

    char *json_str = NULL;
    char *go_b64_response = NULL;
    char *js_callback = NULL;

    JSCValue *value = webkit_javascript_result_get_js_value(r);
    if (!jsc_value_is_string(value)) goto cleanup;

    json_str = jsc_value_to_string(value);
    if (!json_str) goto cleanup;

    go_b64_response = GoTrafficCop(json_str);
    if (!go_b64_response) goto cleanup;

    js_callback = g_strdup_printf("ReceiveFromGo('%s');", go_b64_response);
    webkit_web_view_evaluate_javascript(web_view, js_callback, -1, NULL, NULL, NULL, NULL, NULL);

cleanup:
    if (js_callback) g_free(js_callback);
    if (go_b64_response) release_go_string(go_b64_response);
    if (json_str) g_free(json_str);
    webkit_javascript_result_unref(r);
}

static void on_window_destroy(GtkWidget* widget, gpointer user_data) {
    char* name = (char*)user_data;
    g_free(name);
}

// --- NATIVE UI MODALS ---

typedef struct {
    WebKitWebView *web_view;
    char *title;
    char *message;
} AlertData;

static gboolean show_native_alert_async(gpointer user_data) {
    AlertData *data = (AlertData *)user_data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(data->web_view));
    GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", data->message);
    gtk_window_set_title(GTK_WINDOW(dialog), data->title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(data->title); g_free(data->message); g_free(data);
    return FALSE;
}

void ShowNativeAlert(void* web_view_ptr, char* title, char* message) {
    if (!web_view_ptr) return;
    AlertData *data = g_new0(AlertData, 1);
    data->web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    data->title = g_strdup(title);
    data->message = g_strdup(message);
    g_idle_add(show_native_alert_async, data);
}

// --- FILE PICKERS (WITH HEARTBEATS) ---

static gboolean open_picker_async(gpointer user_data) {
    printf("[C-Bridge] Thread triggered! Finding parent...\n");
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(user_data);
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(web_view));
    GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select a File", parent, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            printf("[C-Bridge] File picked: %s\n", filename);
            char *js = g_strdup_printf("OnFilePicked('%s');", filename);
            webkit_web_view_evaluate_javascript(web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
            g_free(js); g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
    return FALSE;
}

void OpenNativeFilePicker(void* web_view_ptr) {
    if (web_view_ptr) g_idle_add(open_picker_async, web_view_ptr);
}

// --- APP ENTRY ---

extern void GoAppActivate();

static void activate(GtkApplication *app, gpointer user_data) {
    global_app = app;
    GoAppActivate();
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.your.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
