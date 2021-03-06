/* See COPYING file for license and copyright information */

#include <string.h>
#include "weechat-protocol.h"

static const char* types[] = {
    "chr", "int", "lon", "str", "buf", "ptr", "tim", "htb", "hda", "inf", "inl", "arr"
};

static type_t type_char_to_enum(const gchar* s)
{
    for (int t = CHR; t <= ARR; ++t) {
        if (g_strcmp0(s, types[t]) == 0) {
            return t;
        }
    }
    g_error("Should be unreachable\n");
}

static gchar* wtype_to_gvtype(const gchar* wtype, gboolean maybe)
{
    GString* gvtype = g_string_new("");

    if (maybe) {
        g_string_append_c(gvtype, 'm');
    }

    switch (type_char_to_enum(wtype)) {
    case INT:
        g_string_append_c(gvtype, 'i');
        break;
    case LON:
        g_string_append_c(gvtype, 'x');
        break;
    case STR:
        g_string_append_c(gvtype, 's');
        break;
    case BUF:
        g_string_append_c(gvtype, 's');
        break;
    case TIM:
        g_string_append_c(gvtype, 's');
        break;
    case CHR:
        g_string_append_c(gvtype, 'y');
        break;
    case PTR:
        g_string_append_c(gvtype, 's');
        break;
    default:
        g_error("wtype_to_gvtype: type [%s] not handled\n", wtype);
        break;
    }

    return g_string_free(gvtype, FALSE);
}

// TODO: Test me.
static GVariant* weechat_decode_from_arg_to_gvariant(GDataInputStream* stream,
                                                     type_t type, gboolean maybe, gsize* remaining)
{
    GVariant* val;
    gchar* s_type = g_strdup(types[type]);

    if (g_strcmp0(s_type, "int") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_int(stream, remaining));
    } else if (g_strcmp0(s_type, "lon") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_lon(stream, remaining));
    } else if (g_strcmp0(s_type, "chr") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_chr(stream, remaining));
    } else if (g_strcmp0(s_type, "str") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_str(stream, remaining));
    } else if (g_strcmp0(s_type, "buf") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_str(stream, remaining));
    } else if (g_strcmp0(s_type, "ptr") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_ptr(stream, remaining));
    } else if (g_strcmp0(s_type, "tim") == 0) {
        val = g_variant_new(wtype_to_gvtype(s_type, maybe), weechat_decode_tim(stream, remaining));
    } else if (g_strcmp0(s_type, "arr") == 0) {
        val = weechat_decode_arr(stream, remaining);
    } else if (g_strcmp0(s_type, "inf") == 0) {
        val = weechat_decode_inf(stream, remaining);
    } else if (g_strcmp0(s_type, "inl") == 0) {
        val = weechat_decode_inl(stream, remaining);
    } else if (g_strcmp0(s_type, "htb") == 0) {
        val = weechat_decode_htb(stream, remaining);
    } else if (g_strcmp0(s_type, "hda") == 0) {
        val = weechat_decode_hda(stream, remaining);
    } else {
        g_error("weechat_decode_from_arg_to_gvariant: [%s] not handled\n", types[type]);
        return NULL;
    }

    g_free(s_type);
    return val;
}

weechat_t* weechat_create()
{
    weechat_t* weechat = g_try_malloc0(sizeof(weechat_t));

    if (weechat == NULL) {
        return NULL;
    }

    weechat->socket.client = g_socket_client_new();

    return weechat;
}

gboolean weechat_init(weechat_t* weechat, const gchar* host_and_port,
                      guint16 default_port)
{
    g_return_val_if_fail(weechat != NULL, FALSE);

    /* Socket */
    weechat->socket.connection = g_socket_client_connect_to_host(
        weechat->socket.client, host_and_port, default_port, NULL,
        &weechat->error);

    if (weechat->error != NULL) {
        g_critical(weechat->error->message);
        goto error_free;
    }

    /* I/O streams */
    weechat->stream.input = g_io_stream_get_input_stream(
        G_IO_STREAM(weechat->socket.connection));
    weechat->stream.output = g_io_stream_get_output_stream(
        G_IO_STREAM(weechat->socket.connection));

    /* Data stream */
    weechat->incoming = g_data_input_stream_new(weechat->stream.input);
    g_data_input_stream_set_byte_order(weechat->incoming,
                                       G_DATA_STREAM_BYTE_ORDER_BIG_ENDIAN);

    return TRUE;

error_free:
    // TODO: Do weechat_free() here
    return FALSE;
}

gboolean weechat_send(weechat_t* weechat, const gchar* msg)
{
    gchar* str_on_wire = g_strdup_printf("%s\n", msg);
    gboolean ret = TRUE;

    g_output_stream_write(weechat->stream.output, str_on_wire,
                          strlen(str_on_wire), NULL, &weechat->error);
    if (weechat->error != NULL) {
        g_warning(weechat->error->message);
        ret = FALSE;
        goto error_free;
    }

    g_output_stream_flush(weechat->stream.output, NULL, &weechat->error);
    if (weechat->error != NULL) {
        g_warning(weechat->error->message);
        ret = FALSE;
        goto error_free;
    }

error_free:
    g_free(str_on_wire);
    return ret;
}

answer_t* weechat_receive(weechat_t* weechat)
{
    answer_t* answer = weechat_parse_header(weechat);
    gsize remaining = answer->length - 5;
    GDataInputStream* mem;

    if (answer->compression == 1) {
        /* This is bad and (really) ugly */

        /* Stream used to guess size */
        GDataInputStream* tmp = g_data_input_stream_new(
            g_converter_input_stream_new(
                g_memory_input_stream_new_from_data(answer->data.body, remaining, NULL),
                (GConverter*)g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB)));

        /* Get uncompressed size (read until the end) */
        remaining = 0;
        while (g_buffered_input_stream_read_byte((GBufferedInputStream*)tmp, NULL, NULL) != -1) {
            ++remaining;
        }
        g_debug("Payload size: %zuB (%zuB compressed)\n", remaining, answer->length - 5);

        /* Real stream */
        mem = g_data_input_stream_new(
            g_converter_input_stream_new(
                g_memory_input_stream_new_from_data(answer->data.body, remaining, NULL),
                (GConverter*)g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB)));
    } else {
        mem = g_data_input_stream_new(
            g_memory_input_stream_new_from_data(answer->data.body, remaining, NULL));
        g_debug("Payload size: %zuB\n", remaining);
    }

    /* Identifier */
    answer->id = weechat_decode_str(mem, &remaining);

    GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);
    /* As long as there is still data */
    while (remaining > 0) {
        type_t type = weechat_decode_type(mem, &remaining);
        //weechat_unmarshal(mem, type, &remaining);
        GVariant* item = weechat_decode_from_arg_to_gvariant(mem, type, FALSE, &remaining);
        g_variant_builder_add_value(builder, item);
    }

    answer->data.object = g_variant_builder_end(builder);

    return answer;
}

answer_t* weechat_parse_header(weechat_t* weechat)
{
    answer_t* answer = g_try_malloc0(sizeof(answer_t));

    if (answer == NULL) {
        return NULL;
    }

    /* -- HEADER (5B) -- */

    /* Length (4B) */
    answer->length = g_data_input_stream_read_uint32(weechat->incoming, NULL,
                                                     &weechat->error);

    /* Compression (1B) */
    answer->compression = g_data_input_stream_read_byte(weechat->incoming,
                                                        NULL, &weechat->error);

    /* -- BODY -- */

    /* Payload (Length - HEADER) */
    answer->data.body = g_try_malloc0(answer->length - 5);
    g_input_stream_read(weechat->stream.input, answer->data.body,
                        answer->length - 5, NULL, &weechat->error);

    if (weechat->error != NULL) {
        g_error(weechat->error->message);
    }

    return answer;
}

gchar* weechat_decode_str(GDataInputStream* stream, gsize* remaining)
{
    gint32 str_len = weechat_decode_int(stream, remaining);

    /* NULL should be returned, but GVariant stuff would be more difficult */
    if (str_len == -1) {
        return g_strdup("");
    }

    if (str_len == 0) {
        return g_strdup("");
    }

    GString* msg = g_string_sized_new(str_len);

    for (int i = 0; i < str_len; ++i) {
        guchar c = weechat_decode_chr(stream, remaining);
        g_string_append_c(msg, c);
    }

    return g_string_free(msg, FALSE);
}

gchar weechat_decode_chr(GDataInputStream* stream, gsize* remaining)
{
    gchar c = g_data_input_stream_read_byte(stream, NULL, NULL);
    *remaining -= 1;

    return c;
}

gint32 weechat_decode_int(GDataInputStream* stream, gsize* remaining)
{
    gint32 i = g_data_input_stream_read_int32(stream, NULL, NULL);
    *remaining -= 4;

    return i;
}

gint64 weechat_decode_lon(GDataInputStream* stream, gsize* remaining)
{
    gchar length = weechat_decode_chr(stream, remaining);

    GString* msg = g_string_sized_new(length);
    for (int i = 0; i < length; ++i) {
        guchar c = weechat_decode_chr(stream, remaining);
        g_string_append_c(msg, c);
    }
    gchar* lon = g_string_free(msg, FALSE);

    return g_ascii_strtoll(lon, NULL, 10);
}

gchar* weechat_decode_ptr(GDataInputStream* stream, gsize* remaining)
{
    gchar length = weechat_decode_chr(stream, remaining);

    GString* msg = g_string_sized_new(length);
    for (int i = 0; i < length; ++i) {
        guchar c = weechat_decode_chr(stream, remaining);
        g_string_append_c(msg, c);
    }
    gchar* ptr = g_string_free(msg, FALSE);

    return g_strdup_printf("0x%s", ptr);
}

gchar* weechat_decode_tim(GDataInputStream* stream, gsize* remaining)
{
    gchar length = weechat_decode_chr(stream, remaining);

    GString* msg = g_string_sized_new(length);
    for (int i = 0; i < length; ++i) {
        guchar c = weechat_decode_chr(stream, remaining);
        g_string_append_c(msg, c);
    }

    return g_string_free(msg, FALSE);
}

GVariant* weechat_decode_arr(GDataInputStream* stream, gsize* remaining)
{
    type_t arr_t = weechat_decode_type(stream, remaining);
    gint32 arr_l = weechat_decode_int(stream, remaining);
    gchar* type = wtype_to_gvtype(types[arr_t], FALSE);

    GVariantBuilder* builder = g_variant_builder_new(g_variant_type_new_array(G_VARIANT_TYPE(type)));

    for (int i = 0; i < arr_l; ++i) {
        GVariant* val = weechat_decode_from_arg_to_gvariant(stream, arr_t, FALSE, remaining);
        g_variant_builder_add_value(builder, val);
    }

    g_free(type);

    return g_variant_builder_end(builder);
}

GVariant* weechat_decode_inf(GDataInputStream* stream, gsize* remaining)
{
    GVariant* pair;

    /* Key */
    gchar* key = weechat_decode_str(stream, remaining);

    /* Value */
    gchar* val = weechat_decode_str(stream, remaining);

    /* K-V pair */
    pair = g_variant_new("{ss}", key, val);

    g_free(key);
    g_free(val);

    return pair;
}

/*
 * {
 *   "name"    => "buffer/window/bar/...",
 *   "objects" => [
 *     {
 *       "foo" => "abc",
 *       "bar" => 12345
 *     },
 *     {
 *       "baz" => '0xaabbcc'
 *     }
 *   ]
 * }
 */
GVariant* weechat_decode_inl(GDataInputStream* stream, gsize* remaining)
{
    GVariantDict* inl = g_variant_dict_new(NULL);

    gchar* name = weechat_decode_str(stream, remaining);
    gsize count = weechat_decode_int(stream, remaining);
    g_variant_dict_insert(inl, "name", "s", name);
    g_free(name);

    /* Create a new array of dict */
    GVariantBuilder* builder = g_variant_builder_new(g_variant_type_new_array(G_VARIANT_TYPE_VARDICT));
    for (gsize n = 0; n < count; ++n) {

        gint32 count_n = weechat_decode_int(stream, remaining);

        /* Create a new dict */
        GVariantDict* item = g_variant_dict_new(NULL);
        for (gint32 i = 0; i < count_n; ++i) {

            gchar* name_i = weechat_decode_str(stream, remaining);
            type_t type_i = weechat_decode_type(stream, remaining);

            /* Decode based on the previously decoded type, allowing NULL keys */
            GVariant* val = weechat_decode_from_arg_to_gvariant(stream, type_i, TRUE, remaining);

            /* Add to dict */
            g_variant_dict_insert_value(item, name_i, val);

            g_free(name_i);
        }
        /* Append the dict to the array */
        g_variant_builder_add_value(builder, g_variant_dict_end(item));
    }

    /* Add the array of dict to the root with key "objects"
     * (see definition prototype)
     */
    g_variant_dict_insert_value(inl, "objects", g_variant_builder_end(builder));

    return g_variant_dict_end(inl);
}

// TODO: Test me.
GVariant* weechat_decode_htb(GDataInputStream* stream, gsize* remaining)
{
    GVariantBuilder* builder;
    type_t k, v;
    gsize count;

    k = weechat_decode_type(stream, remaining);
    v = weechat_decode_type(stream, remaining);
    count = weechat_decode_int(stream, remaining);

    GVariantType* entry_type = g_variant_type_new_dict_entry(
        g_variant_type_new(wtype_to_gvtype(types[k], FALSE)),
        g_variant_type_new(wtype_to_gvtype(types[v], FALSE)));

    builder = g_variant_builder_new(g_variant_type_new_array(entry_type));

    for (gsize i = 0; i < count; ++i) {
        GVariant* key = weechat_decode_from_arg_to_gvariant(stream, k, FALSE, remaining);
        GVariant* val = weechat_decode_from_arg_to_gvariant(stream, v, FALSE, remaining);
        GVariant* entry = g_variant_new_dict_entry(key, val);
        g_variant_builder_add_value(builder, entry);
    }

    return g_variant_builder_end(builder);
}

GVariant* weechat_decode_hda(GDataInputStream* stream, gsize* remaining)
{
    GVariantBuilder* builder;
    gchar* path, *keys;
    gint32 count;
    gsize list_path_length = 0;

    path = weechat_decode_str(stream, remaining);
    keys = weechat_decode_str(stream, remaining);
    count = weechat_decode_int(stream, remaining);

    /* Construction of the object needs to be generic enough
     *
     * [
     *   {
     *     pointers => [0xaaaaaa, 0xbbbbbb, ...],
     *     "foo"    => (int) 12345,
     *     "bar"    => (str) "abc",
     *     "baz"    => (chr) 'z',
     *     ...
     *   },
     *   {
     *     pointers => [0xaaabbb, 0xbbbccc, ...],
     *     "foo"    => (int) 67890,
     *     "bar"    => (str) "def",
     *     "bar"    => (chr) 'x',
     *     ...
     *   },
     *   ...
     * ]
     *
     * In C++ it would be std::vector<std::map<std::string, void*>> with
     * pointer always being first.
     *
     */
    builder = g_variant_builder_new(g_variant_type_new_array(G_VARIANT_TYPE_VARDICT));

    gchar** list_path = g_strsplit(path, "/", -1);

    for (int j = 0; list_path[j] != NULL; ++j) {
        ++list_path_length;
    }

    gchar** list_keys = g_strsplit(keys, ",", -1);

    /* Construct and add dicts to the array */
    for (int buffer_n = 0; buffer_n < count; ++buffer_n) {
        /* Create an empty dict */
        GVariantDict* dict = g_variant_dict_new(NULL);

        /* Create a builder for the pointer array */
        GVariantBuilder* ptr_array = g_variant_builder_new(
            g_variant_type_new_array(G_VARIANT_TYPE_STRING));

        /* Add each pointer to it */
        for (gsize ptr_n = 0; ptr_n < list_path_length; ++ptr_n) {
            gchar* pptr = weechat_decode_ptr(stream, remaining);
            g_variant_builder_add(ptr_array, "s", pptr);
        }

        /* Add the constructed pointer array to the dict */
        g_variant_dict_insert_value(dict, "__path", g_variant_builder_end(ptr_array));

        /* For each object */
        for (int object_n = 0; list_keys[object_n] != NULL; ++object_n) {
            /* We have a "name:type" string to split */
            gchar** name_and_type = g_strsplit(list_keys[object_n], ":", -1);
            type_t cur_type = type_char_to_enum(name_and_type[1]);

            /* We decode using the right type, allowing NULL values */
            GVariant* val = weechat_decode_from_arg_to_gvariant(stream, cur_type, FALSE, remaining);

            /* We insert with name as the key */
            g_variant_dict_insert_value(dict, name_and_type[0], val);
            g_strfreev(name_and_type);
        }

        /* Add the dict to the builder */
        g_variant_builder_add_value(builder, g_variant_dict_end(dict));
    }

    g_strfreev(list_path);
    g_strfreev(list_keys);

    /* Finish the build and return the constructed object */
    return g_variant_builder_end(builder);
}

type_t weechat_decode_type(GDataInputStream* stream, gsize* remaining)
{
    gchar type[4];

    type[0] = weechat_decode_chr(stream, remaining);
    type[1] = weechat_decode_chr(stream, remaining);
    type[2] = weechat_decode_chr(stream, remaining);
    type[3] = '\0';

    return type_char_to_enum(type);

    g_error("Unknown type : [%s]\n", type);
}
