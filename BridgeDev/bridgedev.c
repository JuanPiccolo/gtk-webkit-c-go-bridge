#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdbool.h>
#include <string.h>
#include "libengine.h"
#include <gio/gio.h> // Required for streams

#define release_go_string(p) free(p)

// Global reference so Go-spawned windows know which App they belong to
GtkApplication *global_app = NULL;

// Forward declarations
static void on_script_message_received(WebKitUserContentManager *m, WebKitJavascriptResult *r, gpointer window_ptr);
static void on_window_destroy(GtkWidget* widget, gpointer user_data);

static char *FirstWindowName = NULL;


// --- GO-TO-C COMMANDS ---

// 2. Updated to non-deprecated JS call
void RunJavaScriptInWindow(void* web_view_ptr, char* js_code) {
    if (!web_view_ptr || !WEBKIT_IS_WEB_VIEW((WebKitWebView*)web_view_ptr)) return;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    // Modern 4.1+ replacement for run_javascript
    webkit_web_view_evaluate_javascript(web_view, js_code, -1, NULL, NULL, NULL, NULL, NULL);
}

// Add this one right below it!
bool RunJavaScriptByWindowName(char* name, char* js_code) {
    GList *windows = gtk_application_get_windows(global_app);
    GList *l;
    for (l = windows; l != NULL; l = l->next) {
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
    // This is the "Nuclear Option." It shuts down the process immediately.
    // It's cleaner than a crash and ensures Go stops too.
    exit(0); 
}


// Updated Signature with char* fileloc
void* OpenPhysicalWindow(char* name, char* type, char* source, int w, int h, char* fileloc) {
    if (!global_app) return NULL;

    GtkWidget *window = gtk_application_window_new(global_app);
    gtk_window_set_title(GTK_WINDOW(window), name);
    gtk_window_set_default_size(GTK_WINDOW(window), w, h);

    WebKitUserContentManager *manager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(manager, "c_bridge");
    
    GtkWidget *web_view = webkit_web_view_new_with_user_content_manager(manager);
    
    // FIX: Unref the manager here. The web_view now owns its own reference to it.
    g_object_unref(manager);

    // Settings
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(web_view));
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);

    gtk_container_add(GTK_CONTAINER(window), web_view);

    // --- CAPTURE THE FIRST WINDOW NAME ---
    if (FirstWindowName == NULL) {
        FirstWindowName = g_strdup(name); 
    }

    // Signals (CONNECTED ONCE)
    // We can still use 'manager' here if needed before the function scope ends, 
    // but webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(web_view)) is the "official" way.
    g_signal_connect(webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(web_view)), 
                     "script-message-received::c_bridge", 
                     G_CALLBACK(on_script_message_received), web_view);
    
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), g_strdup(name));

    // Kill-switch: If this is the master window, attach the exit trap
    if (g_strcmp0(name, FirstWindowName) == 0) {
        g_signal_connect(window, "destroy", G_CALLBACK(kill_app_on_main_window_close), NULL);
    }

    // Loading Logic
    if (type != NULL && strcmp(type, "HTMLAddress") == 0) {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), source);
    } else if (source != NULL) {
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), source, fileloc);
    }

    gtk_widget_show_all(window);
    return (void*)web_view; 
}


bool ClosePhysicalWindow(char* name) {
    GList *windows = gtk_application_get_windows(global_app);
    GList *l;
    for (l = windows; l != NULL; l = l->next) {
        GtkWindow *win = GTK_WINDOW(l->data);
        if (g_strcmp0(gtk_window_get_title(win), name) == 0) {
            gtk_window_close(win);
            return true;
        }
    }
    return false;
}

// --- THE AIRLOCK (C-TO-GO) ---

static void on_script_message_received(WebKitUserContentManager *m, 
                                      WebKitJavascriptResult *r, 
                                      gpointer window_ptr) {
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

    // Call Go Traffic Cop (Exports to libengine.h)
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

// Notify Go that a window has been closed so it can update state
static void on_window_destroy(GtkWidget* widget, gpointer user_data) {
    char* name = (char*)user_data;
    // You will need to create this function in Go to handle the cleanup
    // GoWindowClosedNotify(name); 
    g_free(name);
}

// --- APP LIFECYCLE ---

// Add this near your other externs/forward declarations
extern void GoAppActivate();

static void activate(GtkApplication *app, gpointer user_data) {
    global_app = app;
    
    // Call Go to handle the first window spawn
    GoAppActivate();
}

// Struct to pass title and message safely to the idle thread
typedef struct {
    WebKitWebView *web_view;
    char *title;
    char *message;
} AlertData;

static gboolean show_native_confirm_async(gpointer user_data) {
    AlertData *data = (AlertData *)user_data;
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(data->web_view));
    GtkWindow *parent_window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    // Create a GTK message dialog with Yes/No buttons
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", data->message
    );
    gtk_window_set_title(GTK_WINDOW(dialog), data->title);

    // Run the dialog and capture the response
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    char *js_callback = NULL;
    if (response == GTK_RESPONSE_YES) {
        js_callback = g_strdup_printf("OnConfirmResult(true);");
    } else {
        js_callback = g_strdup_printf("OnConfirmResult(false);");
    }

    // Pass the result back to JavaScript
    webkit_web_view_evaluate_javascript(data->web_view, js_callback, -1, NULL, NULL, NULL, NULL, NULL);

    // Cleanup
    g_free(js_callback);
    gtk_widget_destroy(dialog);
    g_free(data->title);
    g_free(data->message);
    g_free(data);
    
    return FALSE;
}

void ShowNativeConfirm(void* web_view_ptr, char* title, char* message) {
    if (!web_view_ptr) return;
    
    AlertData *data = g_new0(AlertData, 1);
    data->web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    data->title = g_strdup(title);
    data->message = g_strdup(message);
    
    g_idle_add(show_native_confirm_async, data);
}


static gboolean show_native_alert_async(gpointer user_data) {
    AlertData *data = (AlertData *)user_data;
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(data->web_view));
    GtkWindow *parent_window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;

    // Create a native GTK message dialog
    GtkWidget *dialog = gtk_message_dialog_new(
        parent_window,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s", data->message
    );
    gtk_window_set_title(GTK_WINDOW(dialog), data->title);

    // Run the dialog and destroy it when the user clicks OK
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    // Free the copied strings and struct
    g_free(data->title);
    g_free(data->message);
    g_free(data);
    
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


// Struct to pass data between threads
typedef struct {
    void *web_view_ptr;
    char *result;
    bool completed;
} PickerData;


static gboolean open_picker_async(gpointer user_data) {
    printf("[C-Bridge] Thread triggered! Grabbing web view...\n");
    
    void *web_view_ptr = user_data;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    
    printf("[C-Bridge] Tracing to toplevel window...\n");
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(web_view));
    GtkWindow *parent_window = NULL;
    
    if (GTK_IS_WINDOW(toplevel)) {
        printf("[C-Bridge] Success: Found parent GtkWindow!\n");
        parent_window = GTK_WINDOW(toplevel);
    } else {
        printf("[C-Bridge] WARNING: Could not find parent window. Falling back to NULL parent.\n");
    }

    printf("[C-Bridge] Creating file chooser dialog...\n");
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select a File",
        parent_window, 
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    printf("[C-Bridge] Running dialog...\n");
    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    printf("[C-Bridge] Dialog closed with response code: %d\n", res);

    if (res == GTK_RESPONSE_ACCEPT) {
        char *chosen_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (chosen_path) {
            printf("[C-Bridge] File picked: %s. Sending to JS...\n", chosen_path);
            char *js_callback = g_strdup_printf("OnFilePicked('%s');", chosen_path);
            webkit_web_view_evaluate_javascript(web_view, js_callback, -1, NULL, NULL, NULL, NULL, NULL);
            
            g_free(js_callback);
            g_free(chosen_path);
        }
    }

    gtk_widget_destroy(dialog);
    printf("[C-Bridge] Dialog memory destroyed.\n");
    return FALSE; 
}

static gboolean open_folder_picker_async(gpointer user_data) {
    void *web_view_ptr = user_data;
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(web_view_ptr);
    
    GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(web_view));
    GtkWindow *parent_window = NULL;
    
    if (GTK_IS_WINDOW(toplevel)) {
        parent_window = GTK_WINDOW(toplevel);
    }

    // Notice the ACTION is now SELECT_FOLDER
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select a Folder",
        parent_window, 
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL
    );

    gint res = gtk_dialog_run(GTK_DIALOG(dialog));

    if (res == GTK_RESPONSE_ACCEPT) {
        char *chosen_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (chosen_path) {
            // We'll trigger OnFolderPicked in your JS
            char *js_callback = g_strdup_printf("OnFolderPicked('%s');", chosen_path);
            webkit_web_view_evaluate_javascript(web_view, js_callback, -1, NULL, NULL, NULL, NULL, NULL);
            
            g_free(js_callback);
            g_free(chosen_path);
        }
    }

    gtk_widget_destroy(dialog);
    return FALSE; 
}

// The outer wrapper Go calls
void OpenNativeFolderPicker(void* web_view_ptr) {
    if (!web_view_ptr) return;
    g_idle_add(open_folder_picker_async, web_view_ptr);
}

void OpenNativeFilePicker(void* web_view_ptr) {
    if (!web_view_ptr) return;
    
    // Fire it off to the main thread and IMMEDIATELY return to Go!
    g_idle_add(open_picker_async, web_view_ptr);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.your.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}