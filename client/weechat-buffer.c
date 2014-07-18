/* See COPYING file for license and copyright information */

#include <glib/gprintf.h>
#include "weechat-buffer.h"

buffer_t* buffer_create(GVariant* buf)
{
    buffer_t* buffer = g_try_malloc0(sizeof(buffer_t));

    if (buffer == NULL) {
        return NULL;
    }
    GVariantDict* dict = g_variant_dict_new(buf);

    /* Extract GVariant dict to C struct */
    g_variant_dict_lookup(dict, "full_name", "ms", &buffer->full_name);
    g_variant_dict_lookup(dict, "short_name", "ms", &buffer->short_name);
    g_variant_dict_lookup(dict, "title", "ms", &buffer->title);
    g_variant_dict_lookup(dict, "notify", "i", &buffer->notify);
    g_variant_dict_lookup(dict, "number", "i", &buffer->number);
    buffer->pointers = g_variant_dup_strv(
        g_variant_dict_lookup_value(dict, "__path", NULL), NULL);

    g_variant_dict_unref(dict);

    /* Create local variables hash table */
    buffer->local_variables = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    /* Create widgets */
    buffer->ui.tab_layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    buffer->ui.tab_title = gtk_label_new(buffer->title);
    buffer->ui.label = gtk_label_new(buffer_get_canonical_name(buffer));
    buffer->ui.log_and_nick = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    buffer->ui.log_scroll = gtk_scrolled_window_new(0, 0);
    buffer->ui.log_view = gtk_text_view_new();
    buffer->ui.sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    buffer->ui.nick_scroll = gtk_scrolled_window_new(0, 0);
    buffer->ui.nick_adapt = gtk_viewport_new(NULL, NULL);
    buffer->ui.nick_list = gtk_list_box_new();
    buffer->ui.entry = gtk_entry_new();

    return buffer;
}

void buffer_ui_init(buffer_t* buf)
{
    PangoFontDescription* font_desc = pango_font_description_from_string("Monospace 10");

    /* Make the title selectable */
    //gtk_label_set_selectable(GTK_LABEL(buf->ui.tab_title), TRUE);

    /* Add the tab title */
    gtk_container_add(GTK_CONTAINER(buf->ui.tab_layout), buf->ui.tab_title);

    /* Create the log view */
    buf->ui.textbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(buf->ui.log_view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(buf->ui.log_view), GTK_WRAP_WORD);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(buf->ui.log_view), FALSE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(buf->ui.log_view), FALSE);
    gtk_widget_override_font(buf->ui.log_view, font_desc);
    gtk_widget_set_can_focus(buf->ui.log_view, FALSE);

    /* Add the log view to the log scrolling window */
    gtk_container_add(GTK_CONTAINER(buf->ui.log_scroll), buf->ui.log_view);

    /* Add the log scrolling window to the log and nick view */
    gtk_container_add_with_properties(GTK_CONTAINER(buf->ui.log_and_nick), buf->ui.log_scroll,
                                      "expand", TRUE, "fill", TRUE, NULL);

    /* Add the separator between log and nicklist */
    gtk_container_add(GTK_CONTAINER(buf->ui.log_and_nick), buf->ui.sep);

    /* Create the nick list */

    /* Add the nick list to the nick scrolling window */
    GtkStyleContext* ctx = gtk_widget_get_style_context(buf->ui.tab_title);
    GdkRGBA bg;
    gtk_style_context_get_background_color(ctx, GTK_STATE_FLAG_NORMAL, &bg);
    gtk_widget_override_background_color(buf->ui.nick_list, GTK_STATE_FLAG_NORMAL, &bg);
    gtk_widget_override_background_color(buf->ui.nick_adapt, GTK_STATE_FLAG_NORMAL, &bg);

    gtk_container_add(GTK_CONTAINER(buf->ui.nick_adapt), buf->ui.nick_list);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(buf->ui.nick_adapt), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(buf->ui.nick_scroll), buf->ui.nick_adapt);

    /* Add the nick scrolling window to the log and nick view */
    gtk_container_add(GTK_CONTAINER(buf->ui.log_and_nick), buf->ui.nick_scroll);

    /* Add the log and nick view to the tab layout */
    gtk_container_add_with_properties(GTK_CONTAINER(buf->ui.tab_layout), buf->ui.log_and_nick,
                                      "expand", TRUE, "fill", TRUE, NULL);

    /* Create the text entry */
    gtk_entry_set_has_frame(GTK_ENTRY(buf->ui.entry), FALSE);
    gtk_widget_set_can_default(buf->ui.entry, TRUE);
    g_object_set(buf->ui.entry, "activates-default", TRUE, NULL);

    /* Add the text entry to the tab layout */
    gtk_container_add(GTK_CONTAINER(buf->ui.tab_layout), buf->ui.entry);

    /* Set the widget name to the full_name to help the callback */
    gtk_widget_set_name(GTK_WIDGET(buf->ui.tab_layout), buf->full_name);
    gtk_widget_set_name(GTK_WIDGET(buf->ui.entry), buf->full_name);

    gtk_label_set_width_chars(GTK_LABEL(buf->ui.label), 20);
    gtk_label_set_ellipsize(GTK_LABEL(buf->ui.label), PANGO_ELLIPSIZE_END);
    //gtk_misc_set_alignment(GTK_MISC(buf->ui.label), 1, 0);
}

void buffer_delete(buffer_t* buffer)
{
    g_free(buffer->full_name);
    g_free(buffer->short_name);
    g_free(buffer->title);
    g_strfreev(buffer->pointers);
    g_hash_table_unref(buffer->local_variables);
    g_free(buffer);
}

const gchar* buffer_get_canonical_name(buffer_t* buffer)
{
    if (buffer->short_name != NULL) {
        return buffer->short_name;
    } else {
        return buffer->full_name;
    }
}

void buffer_append_text(buffer_t* buffer, const gchar* prefix, const gchar* text)
{
    GtkTextMark* mark;
    GtkTextIter iter;

    gchar* str = g_strdup_printf("%s\t%s", prefix, text);

    /* Gtk buffer magic */
    mark = gtk_text_buffer_get_insert(buffer->ui.textbuf);
    gtk_text_buffer_get_iter_at_mark(buffer->ui.textbuf, &iter, mark);
    if (gtk_text_buffer_get_char_count(buffer->ui.textbuf))
        gtk_text_buffer_insert(buffer->ui.textbuf, &iter, "\n", 1);
    gtk_text_buffer_insert(buffer->ui.textbuf, &iter, str, -1);

    /* Scroll to the end of the text view */
    mark = gtk_text_buffer_create_mark(buffer->ui.textbuf, NULL, &iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(buffer->ui.log_view), mark, 0, FALSE, 0, 0);

    g_free(str);
}
