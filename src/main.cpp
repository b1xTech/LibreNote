#include <gtk/gtk.h>
#include <cstring>

/* ================== App Data ================== */
struct AppData {
    GtkWindow   *window = nullptr;
    GtkTextView *textview = nullptr;
    GFile       *current_file = nullptr;
    gboolean     word_wrap = TRUE;
    char        *last_search = nullptr;
};

/* ================== Clipboard ================== */
static GdkClipboard* get_clipboard(AppData *d) {
    return gtk_widget_get_clipboard(GTK_WIDGET(d->textview));
}

/* ================== Helpers ================== */
static void set_wrap(AppData *d, gboolean enabled) {
    gtk_text_view_set_wrap_mode(
        d->textview,
        enabled ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE
    );
    d->word_wrap = enabled;
}

/* ================== Save Helpers ================== */
static void save_to_file(AppData *d, GFile *file) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(d->textview);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);

    char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    g_file_replace_contents(
        file,
        text,
        std::strlen(text),
        nullptr,
        FALSE,
        G_FILE_CREATE_NONE,
        nullptr,
        nullptr,
        nullptr
    );
    g_free(text);
}

/* ================== Save As ================== */
static void save_as_finish(GObject *src, GAsyncResult *res, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GError *err = nullptr;

    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
    if (!file) {
        if (err) g_error_free(err);
        return;
    }

    if (d->current_file)
        g_object_unref(d->current_file);

    d->current_file = file;
    save_to_file(d, file);
}

static void action_save_as(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Save File");
    gtk_file_dialog_save(dlg, d->window, nullptr, save_as_finish, d);
}

/* ================== Save ================== */
static void action_save(GSimpleAction *a, GVariant *p, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    if (d->current_file)
        save_to_file(d, d->current_file);
    else
        action_save_as(a, p, user_data);
}

/* ================== Open ================== */
static void open_finish(GObject *src, GAsyncResult *res, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GError *err = nullptr;

    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (!file) {
        if (err) g_error_free(err);
        return;
    }

    char *contents = nullptr;
    gsize len = 0;
    if (g_file_load_contents(file, nullptr, &contents, &len, nullptr, &err)) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(d->textview);
        gtk_text_buffer_set_text(buf, contents, len);

        if (d->current_file)
            g_object_unref(d->current_file);

        d->current_file = file;
    }

    g_free(contents);
    if (err) g_error_free(err);
}

static void action_open(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Open File");
    gtk_file_dialog_open(dlg, d->window, nullptr, open_finish, d);
}

/* ================== Edit ================== */
static void action_cut(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkTextBuffer *b = gtk_text_view_get_buffer(d->textview);
    gtk_text_buffer_cut_clipboard(b, get_clipboard(d), TRUE);
}

static void action_copy(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkTextBuffer *b = gtk_text_view_get_buffer(d->textview);
    gtk_text_buffer_copy_clipboard(b, get_clipboard(d));
}

static void action_paste(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkTextBuffer *b = gtk_text_view_get_buffer(d->textview);
    gtk_text_buffer_paste_clipboard(b, get_clipboard(d), nullptr, TRUE);
}

static void action_select_all(GSimpleAction*, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    GtkTextBuffer *b = gtk_text_view_get_buffer(d->textview);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    gtk_text_buffer_select_range(b, &s, &e);
}

/* ================== View ================== */
static void action_wrap(GSimpleAction *a, GVariant*, gpointer user_data) {
    auto *d = static_cast<AppData*>(user_data);
    gboolean enabled = !d->word_wrap;
    set_wrap(d, enabled);
    g_simple_action_set_state(a, g_variant_new_boolean(enabled));
}

/* ================== Quit ================== */
static void action_quit(GSimpleAction*, GVariant*, gpointer user_data) {
    gtk_window_destroy(static_cast<AppData*>(user_data)->window);
}

/* ================== Find (Notepad-style) ================== */
struct FindData {
    GtkWidget *window;
    GtkWidget *entry;
    AppData   *app;
    GtkTextIter last_iter;
    gboolean   initialized = FALSE;
};

static void find_next(FindData *fdata, gboolean forward) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(fdata->app->textview);
    GtkTextIter start = fdata->last_iter;
    GtkTextIter match_start, match_end;

    const char *txt = gtk_editable_get_text(GTK_EDITABLE(fdata->entry));
    if (!txt || !*txt) return;

    gboolean found = FALSE;

    if (!fdata->initialized) {
        gtk_text_buffer_get_start_iter(buf, &start);
        fdata->initialized = TRUE;
    }

    if (forward)
        found = gtk_text_iter_forward_search(
            &start, txt, GTK_TEXT_SEARCH_TEXT_ONLY,
            &match_start, &match_end, nullptr
        );
    else
        found = gtk_text_iter_backward_search(
            &start, txt, GTK_TEXT_SEARCH_TEXT_ONLY,
            &match_start, &match_end, nullptr
        );

    if (found) {
        gtk_text_buffer_select_range(buf, &match_start, &match_end);
        gtk_text_view_scroll_to_iter(
            fdata->app->textview, &match_start,
            0.1, FALSE, 0, 0
        );
        fdata->last_iter = match_end;
    } else {
        if (forward)
            gtk_text_buffer_get_start_iter(buf, &fdata->last_iter);
        else
            gtk_text_buffer_get_end_iter(buf, &fdata->last_iter);
    }
}

static void on_find_next_clicked(GtkButton*, gpointer user_data) {
    find_next(static_cast<FindData*>(user_data), TRUE);
}

static void on_find_prev_clicked(GtkButton*, gpointer user_data) {
    find_next(static_cast<FindData*>(user_data), FALSE);
}

static void action_find(GSimpleAction*, GVariant*, gpointer user_data) {
    AppData *d = static_cast<AppData*>(user_data);
    FindData *fdata = new FindData();
    fdata->app = d;

    GtkWidget *win = gtk_window_new();
    fdata->window = win;
    gtk_window_set_title(GTK_WINDOW(win), "Find");
    gtk_window_set_transient_for(GTK_WINDOW(win), d->window);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(box), entry);
    fdata->entry = entry;

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(box), hbox);

    GtkWidget *btn_next = gtk_button_new_with_label("↓");
    gtk_box_append(GTK_BOX(hbox), btn_next);
    g_signal_connect(btn_next, "clicked", G_CALLBACK(on_find_next_clicked), fdata);

    GtkWidget *btn_prev = gtk_button_new_with_label("↑");
    gtk_box_append(GTK_BOX(hbox), btn_prev);
    g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_find_prev_clicked), fdata);

    if (d->last_search) {
        GtkEntryBuffer *buffer = gtk_entry_buffer_new(d->last_search, -1);
        gtk_entry_set_buffer(GTK_ENTRY(entry), buffer);
    }

    g_signal_connect(entry, "activate", G_CALLBACK(on_find_next_clicked), fdata);
    gtk_window_present(GTK_WINDOW(win));
}

/* ================== Status Bar ================== */
static void update_status(AppData *d) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(d->textview);
    GtkTextIter iter;
    GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
    gtk_text_buffer_get_iter_at_mark(buf, &iter, mark);

    gint line = gtk_text_iter_get_line(&iter) + 1;
    gint col  = gtk_text_iter_get_line_offset(&iter) + 1;

    char text[64];
    snprintf(text, sizeof(text), "Line: %d, Column: %d", line, col);
    gtk_label_set_text(
        GTK_LABEL(g_object_get_data(G_OBJECT(d->window), "status_label_linecol")),
        text
    );
}

/* ================== Activate ================== */
static void activate(GtkApplication *app, gpointer) {
    auto *d = new AppData();

    d->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(d->window, "LibreNote");
    gtk_window_set_default_size(d->window, 800, 600);
    gtk_application_window_set_show_menubar(
        GTK_APPLICATION_WINDOW(d->window), TRUE
    );

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(d->window, vbox);

    /* TextView and Scroll */
    d->textview = GTK_TEXT_VIEW(gtk_text_view_new());
    set_wrap(d, TRUE);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(scroll),
        GTK_WIDGET(d->textview)
    );
    gtk_box_append(GTK_BOX(vbox), scroll);

    /* Status Bar */
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(status, TRUE);
    gtk_box_append(GTK_BOX(vbox), status);

    GtkWidget *label_pos = gtk_label_new("Line: 1, Column: 1");
    gtk_box_append(GTK_BOX(status), label_pos);

    GtkWidget *label_utf8 = gtk_label_new("UTF-8");
    gtk_widget_set_halign(label_utf8, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(status), label_utf8);

    g_object_set_data(
        G_OBJECT(d->window),
        "status_label_linecol",
        label_pos
    );

    GtkTextBuffer *buf = gtk_text_view_get_buffer(d->textview);

    /* ==== ONLY FIX: NULL-safe strcmp ==== */
    g_signal_connect(
        buf,
        "mark-set",
        G_CALLBACK(+[](GtkTextBuffer*, GtkTextIter*, GtkTextMark *m, gpointer user_data) {
            const char *name = gtk_text_mark_get_name(m);
            if (name && strcmp(name, "insert") == 0)
                update_status(static_cast<AppData*>(user_data));
        }),
        d
    );

    /* Actions and Menus */
    const GActionEntry acts[] = {
        {"open", action_open},
        {"save", action_save},
        {"save_as", action_save_as},
        {"find", action_find},
        {"quit", action_quit},
        {"cut", action_cut},
        {"copy", action_copy},
        {"paste", action_paste},
        {"select_all", action_select_all},
        {"wrap", action_wrap, nullptr, "true"}
    };
    g_action_map_add_action_entries(
        G_ACTION_MAP(app), acts, G_N_ELEMENTS(acts), d
    );

    GMenu *m = g_menu_new();

    GMenu *f = g_menu_new();
    g_menu_append(f, "Open…", "app.open");
    g_menu_append(f, "Save", "app.save");
    g_menu_append(f, "Save As…", "app.save_as");
    g_menu_append(f, "Quit", "app.quit");
    g_menu_append_submenu(m, "File", G_MENU_MODEL(f));

    GMenu *e = g_menu_new();
    g_menu_append(e, "Find…", "app.find");
    g_menu_append(e, "Cut", "app.cut");
    g_menu_append(e, "Copy", "app.copy");
    g_menu_append(e, "Paste", "app.paste");
    g_menu_append(e, "Select All", "app.select_all");
    g_menu_append_submenu(m, "Edit", G_MENU_MODEL(e));

    GMenu *v = g_menu_new();
    g_menu_append(v, "Word Wrap", "app.wrap");
    g_menu_append_submenu(m, "View", G_MENU_MODEL(v));

    gtk_application_set_menubar(app, G_MENU_MODEL(m));

    const char *accel[] = {"<Ctrl>f", nullptr};
    gtk_application_set_accels_for_action(app, "app.find", accel);

    gtk_window_present(d->window);
}

/* ================== main ================== */
int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new(
        "org.libresuite.librenote",
        G_APPLICATION_DEFAULT_FLAGS
    );

    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
