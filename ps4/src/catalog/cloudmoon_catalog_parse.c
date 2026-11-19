#include "cloudmoon_catalog.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *begin;
    const char *end;
    const char *cursor;
} json_cursor_t;

static void skip_ws(json_cursor_t *c) {
    while (c->cursor < c->end &&
           (*c->cursor == ' ' || *c->cursor == '\n' ||
            *c->cursor == '\r' || *c->cursor == '\t'))
        ++c->cursor;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int append_utf8(char *out, size_t cap, size_t *used, uint32_t cp) {
    unsigned char bytes[4];
    size_t n;
    if (cp <= 0x7f) {
        bytes[0] = (unsigned char)cp; n = 1;
    } else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0u | (cp >> 6));
        bytes[1] = (unsigned char)(0x80u | (cp & 0x3fu)); n = 2;
    } else if (cp <= 0xffff) {
        bytes[0] = (unsigned char)(0xe0u | (cp >> 12));
        bytes[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | (cp & 0x3fu)); n = 3;
    } else if (cp <= 0x10ffff) {
        bytes[0] = (unsigned char)(0xf0u | (cp >> 18));
        bytes[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3fu));
        bytes[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3fu));
        bytes[3] = (unsigned char)(0x80u | (cp & 0x3fu)); n = 4;
    } else return -1;
    if (*used + n >= cap) return -1;
    memcpy(out + *used, bytes, n);
    *used += n;
    return 0;
}

static int read_u16(const char **p, const char *end, uint32_t *out) {
    int i, d;
    uint32_t value = 0;
    if (end - *p < 4) return -1;
    for (i = 0; i < 4; ++i) {
        d = hex_digit((*p)[i]);
        if (d < 0) return -1;
        value = (value << 4) | (uint32_t)d;
    }
    *p += 4;
    *out = value;
    return 0;
}

static int json_string(json_cursor_t *c, char *out, size_t capacity) {
    size_t used = 0;
    if (!c || !out || capacity == 0) return -1;
    skip_ws(c);
    if (c->cursor >= c->end || *c->cursor != '"') return -1;
    ++c->cursor;

    while (c->cursor < c->end) {
        unsigned char ch = (unsigned char)*c->cursor++;
        if (ch == '"') {
            out[used] = 0;
            return 0;
        }
        if (ch < 0x20) return -1;
        if (ch != '\\') {
            if (used + 1 >= capacity) return -1;
            out[used++] = (char)ch;
            continue;
        }
        if (c->cursor >= c->end) return -1;
        ch = (unsigned char)*c->cursor++;
        switch (ch) {
        case '"': case '\\': case '/':
            if (used + 1 >= capacity) return -1;
            out[used++] = (char)ch;
            break;
        case 'b': if (used + 1 >= capacity) return -1; out[used++] = '\b'; break;
        case 'f': if (used + 1 >= capacity) return -1; out[used++] = '\f'; break;
        case 'n': if (used + 1 >= capacity) return -1; out[used++] = '\n'; break;
        case 'r': if (used + 1 >= capacity) return -1; out[used++] = '\r'; break;
        case 't': if (used + 1 >= capacity) return -1; out[used++] = '\t'; break;
        case 'u': {
            uint32_t cp;
            if (read_u16(&c->cursor, c->end, &cp) != 0) return -1;
            if (cp >= 0xd800 && cp <= 0xdbff) {
                uint32_t low;
                if (c->end - c->cursor < 6 || c->cursor[0] != '\\' ||
                    c->cursor[1] != 'u') return -1;
                c->cursor += 2;
                if (read_u16(&c->cursor, c->end, &low) != 0 ||
                    low < 0xdc00 || low > 0xdfff) return -1;
                cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
            } else if (cp >= 0xdc00 && cp <= 0xdfff) return -1;
            if (append_utf8(out, capacity, &used, cp) != 0) return -1;
            break;
        }
        default: return -1;
        }
    }
    return -1;
}

static int json_skip_value(json_cursor_t *c) {
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    skip_ws(c);
    if (c->cursor >= c->end) return -1;

    if (*c->cursor == '"' ) {
        char scratch[2];
        return json_string(c, scratch, sizeof(scratch));
    }
    if (*c->cursor == '{' || *c->cursor == '[') {
        const char open = *c->cursor;
        const char close = open == '{' ? '}' : ']';
        (void)close;
        do {
            char ch = *c->cursor++;
            if (in_string) {
                if (escaped) escaped = 0;
                else if (ch == '\\') escaped = 1;
                else if (ch == '"') in_string = 0;
                continue;
            }
            if (ch == '"') in_string = 1;
            else if (ch == '{' || ch == '[') ++depth;
            else if (ch == '}' || ch == ']') --depth;
        } while (c->cursor < c->end && depth > 0);
        return depth == 0 && !in_string ? 0 : -1;
    }
    while (c->cursor < c->end && *c->cursor != ',' &&
           *c->cursor != '}' && *c->cursor != ']' &&
           !isspace((unsigned char)*c->cursor))
        ++c->cursor;
    return 0;
}

static int json_key_value(const char *object, size_t length,
                          const char *wanted, char *out, size_t capacity,
                          int *is_string) {
    json_cursor_t c;
    if (!object || !wanted || !out || capacity == 0) return -1;
    c.begin = object;
    c.end = object + length;
    c.cursor = object;
    skip_ws(&c);
    if (c.cursor >= c.end || *c.cursor != '{') return -1;
    ++c.cursor;

    while (c.cursor < c.end) {
        char key[64];
        skip_ws(&c);
        if (c.cursor < c.end && *c.cursor == '}') break;
        if (json_string(&c, key, sizeof(key)) != 0) return -1;
        skip_ws(&c);
        if (c.cursor >= c.end || *c.cursor++ != ':') return -1;
        skip_ws(&c);

        if (strcmp(key, wanted) == 0) {
            if (is_string) *is_string = (*c.cursor == '"');
            if (*c.cursor == '"')
                return json_string(&c, out, capacity);
            {
                size_t n = 0;
                while (c.cursor < c.end && *c.cursor != ',' &&
                       *c.cursor != '}' && !isspace((unsigned char)*c.cursor)) {
                    if (n + 1 >= capacity) return -1;
                    out[n++] = *c.cursor++;
                }
                out[n] = 0;
                return 0;
            }
        }
        if (json_skip_value(&c) != 0) return -1;
        skip_ws(&c);
        if (c.cursor < c.end && *c.cursor == ',') ++c.cursor;
        else if (c.cursor < c.end && *c.cursor != '}') return -1;
    }
    return 1;
}

static int get_int(const char *object, size_t length, const char *key,
                   int default_value) {
    char value[32];
    int string_value = 0;
    char *end = NULL;
    long parsed;
    if (json_key_value(object, length, key, value, sizeof(value),
                       &string_value) != 0 || string_value)
        return default_value;
    parsed = strtol(value, &end, 10);
    if (!end || end == value || *end != 0 || parsed < -2147483647L ||
        parsed > 2147483647L)
        return default_value;
    return (int)parsed;
}

static int get_bool(const char *object, size_t length, const char *key,
                    int default_value) {
    char value[16];
    int string_value = 0;
    if (json_key_value(object, length, key, value, sizeof(value),
                       &string_value) != 0 || string_value)
        return default_value;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) return 1;
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) return 0;
    return default_value;
}

static int get_string(const char *object, size_t length, const char *key,
                      char *out, size_t capacity) {
    int string_value = 0;
    int rc = json_key_value(object, length, key, out, capacity, &string_value);
    return rc == 0 && string_value ? 0 : -1;
}

int cm_catalog_parse_json(const char *json, size_t length,
                          cm_catalog_t *catalog) {
    const char *list, *list_end, *p;
    size_t count = 0;
    int root_code = -1;

    if (!json || !catalog || length == 0 || length > 1024u * 1024u)
        return -1;
    memset(catalog, 0, sizeof(*catalog));

    {
        json_cursor_t root;
        char value[32];
        int type = 0;
        root.begin = json;
        root.end = json + length;
        root.cursor = json;
        if (json_key_value(root.cursor, length, "code", value,
                           sizeof(value), &type) == 0 && !type)
            root_code = atoi(value);
    }
    if (root_code != 0)
        return -1;

    list = strstr(json, "\"list\"");
    if (!list || (size_t)(json + length - list) < 7u) return -1;
    p = strchr(list, '[');
    if (!p || p >= json + length) return -1;
    list_end = json + length;

    while (p < list_end && count < CM_CATALOG_MAX_GAMES) {
        const char *object_start, *object_end;
        int depth = 0, in_string = 0, escaped = 0;
        cm_catalog_game_t game;
        ++p;
        while (p < list_end && (*p == ' ' || *p == '\n' ||
               *p == '\r' || *p == '\t' || *p == ',')) ++p;
        if (p >= list_end || *p == ']') break;
        if (*p != '{') return -1;
        object_start = p;

        do {
            char ch = *p++;
            if (in_string) {
                if (escaped) escaped = 0;
                else if (ch == '\\') escaped = 1;
                else if (ch == '"') in_string = 0;
            } else if (ch == '"') in_string = 1;
            else if (ch == '{') ++depth;
            else if (ch == '}') --depth;
        } while (p < list_end && depth > 0);
        if (depth != 0 || in_string) return -1;
        object_end = p;

        memset(&game, 0, sizeof(game));
        game.status = get_int(object_start, (size_t)(object_end - object_start),
                              "status", 0);
        game.is_pc = get_bool(object_start, (size_t)(object_end - object_start),
                              "is_pc", 0);
        game.is_beta = get_bool(object_start, (size_t)(object_end - object_start),
                                "is_beta", 0);
        game.need_login_google = get_bool(object_start,
                              (size_t)(object_end - object_start),
                              "need_login_google", 0);
        game.min_vip_level = get_int(object_start,
                              (size_t)(object_end - object_start),
                              "min_vip_level", 1);
        if (get_string(object_start, (size_t)(object_end - object_start),
                       "title", game.title, sizeof(game.title)) == 0 &&
            get_string(object_start, (size_t)(object_end - object_start),
                       "package_name", game.package_name,
                       sizeof(game.package_name)) == 0) {
            (void)get_string(object_start,
                             (size_t)(object_end - object_start),
                             "icon_url", game.icon_url,
                             sizeof(game.icon_url));
            (void)get_string(object_start,
                             (size_t)(object_end - object_start),
                             "maintenance_info", game.maintenance_info,
                             sizeof(game.maintenance_info));
            if (game.status > 0 && !game.is_pc)
                catalog->games[count++] = game;
        }
    }
    catalog->count = count;
    return count > 0 ? (int)count : -1;
}
