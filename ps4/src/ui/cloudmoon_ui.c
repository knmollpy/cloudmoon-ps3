#include "cloudmoon_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define C_BG       0xFF080D19u
#define C_PANEL    0xFF111B2Cu
#define C_PANEL2   0xFF17243Au
#define C_BORDER   0xFF263955u
#define C_TEXT     0xFFF2F7FFu
#define C_MUTED    0xFF92A5C0u
#define C_CYAN     0xFF41D9F4u
#define C_BLUE     0xFF318BFFu
#define C_GREEN    0xFF5CE0B2u
#define C_DARK     0xFF07111Fu
#define C_WHITE    0xFFFFFFFFu

static void rect(const cm_ui_surface_t *s, int x, int y, int w, int h,
                 uint32_t color) {
    int x0, y0, x1, y1, yy;
    if (!s || !s->pixels || s->width <= 0 || s->height <= 0 ||
        s->pitch_pixels < s->width || w <= 0 || h <= 0) return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w > s->width ? s->width : x + w;
    y1 = y + h > s->height ? s->height : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    for (yy = y0; yy < y1; ++yy) {
        uint32_t *row = s->pixels + (size_t)yy * (size_t)s->pitch_pixels;
        int xx;
        for (xx = x0; xx < x1; ++xx) row[xx] = color;
    }
}

static void outline(const cm_ui_surface_t *s, int x, int y, int w, int h,
                    uint32_t color) {
    rect(s, x, y, w, 2, color);
    rect(s, x, y + h - 2, w, 2, color);
    rect(s, x, y, 2, h, color);
    rect(s, x + w - 2, y, 2, h, color);
}

static const uint8_t *glyph(char ch) {
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t g[45][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
        {0,0,0,0,0,0,4}, {0,0,0,0,0,4,4},
        {0,0,0,0,0,0,31}, {0,0,4,0,4,0,0},
        {0,4,4,4,4,4,0}, {14,17,1,2,4,0,4},
        {0,0,0,31,0,0,0}, {0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0}
    };
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./:!?+'";
    const char *p;
    ch = (char)toupper((unsigned char)ch);
    p = strchr(chars, ch);
    if (!p) return blank;
    return g[(size_t)(p - chars)];
}

static void text(const cm_ui_surface_t *s, int x, int y, int scale,
                 uint32_t color, const char *str) {
    int origin = x;
    if (!str || scale < 1) return;
    while (*str) {
        const uint8_t *rows = glyph(*str++);
        int ry, rx;
        if (*str == '\n') { ++str; x = origin; y += 10 * scale; continue; }
        for (ry = 0; ry < 7; ++ry)
            for (rx = 0; rx < 5; ++rx)
                if (rows[ry] & (uint8_t)(1u << (4 - rx)))
                    rect(s, x + rx * scale, y + ry * scale,
                         scale, scale, color);
        x += 6 * scale;
    }
}

static void panel(const cm_ui_surface_t *s, int x, int y, int w, int h,
                  int active) {
    rect(s, x + 5, y + 7, w, h, 0x66000000u);
    rect(s, x, y, w, h, active ? C_PANEL2 : C_PANEL);
    outline(s, x, y, w, h, active ? C_CYAN : C_BORDER);
    if (active) rect(s, x + 3, y + 3, 4, h - 6, C_CYAN);
}

static void centered_text(const cm_ui_surface_t *s, int cx, int y, int scale,
                          uint32_t color, const char *str) {
    int width = str ? (int)strlen(str) * 6 * scale : 0;
    text(s, cx - width / 2, y, scale, color, str);
}

void cm_ui_init(cm_ui_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->view = CM_UI_HOME;
}

static int game_count(const cm_ui_model_t *model) {
    if (!model || !model->games || model->game_count <= 0) return 0;
    return model->game_count > CM_UI_MAX_GAMES ? CM_UI_MAX_GAMES : model->game_count;
}

static cm_ui_action_t action(cm_ui_action_type_t type, int index) {
    cm_ui_action_t a;
    a.type = type;
    a.game_index = index;
    return a;
}

cm_ui_action_t cm_ui_input(cm_ui_state_t *state, unsigned int keys,
                           const cm_ui_model_t *model) {
    int count;
    if (!state) return action(CM_UI_ACTION_NONE, -1);
    count = game_count(model);
    if (keys & CM_UI_KEY_OPTIONS) {
        state->view = CM_UI_HOME;
        return action(CM_UI_ACTION_BACK, -1);
    }
    if (keys & CM_UI_KEY_L1) {
        state->view = (cm_ui_view_t)((state->view + CM_UI_VIEW_COUNT - 1) % CM_UI_VIEW_COUNT);
    } else if (keys & CM_UI_KEY_R1) {
        state->view = (cm_ui_view_t)((state->view + 1) % CM_UI_VIEW_COUNT);
    }
    switch (state->view) {
    case CM_UI_HOME:
        if (keys & CM_UI_KEY_UP) state->home_focus = 0;
        if (keys & CM_UI_KEY_DOWN) state->home_focus = 1;
        if (keys & CM_UI_KEY_CROSS) {
            if (state->home_focus == 0) {
                state->view = CM_UI_LIBRARY;
                state->selected_game = 0;
                return action(CM_UI_ACTION_OPEN_LIBRARY, -1);
            }
            state->view = CM_UI_LINK;
            return action(CM_UI_ACTION_REQUEST_LINK, -1);
        }
        break;
    case CM_UI_LIBRARY:
        if (keys & CM_UI_KEY_CIRCLE) {
            state->view = CM_UI_HOME;
            return action(CM_UI_ACTION_BACK, -1);
        }
        if (count == 0) {
            if (keys & CM_UI_KEY_CROSS) {
                state->view = CM_UI_LINK;
                return action(CM_UI_ACTION_REQUEST_LINK, -1);
            }
            break;
        }
        if (keys & CM_UI_KEY_LEFT)  --state->selected_game;
        if (keys & CM_UI_KEY_RIGHT) ++state->selected_game;
        if (keys & CM_UI_KEY_UP)    state->selected_game -= 4;
        if (keys & CM_UI_KEY_DOWN)  state->selected_game += 4;
        if (state->selected_game < 0) state->selected_game = 0;
        if (state->selected_game >= count) state->selected_game = count - 1;
        if (keys & CM_UI_KEY_CROSS)
            return action(CM_UI_ACTION_PLAY_GAME, state->selected_game);
        break;
    case CM_UI_LINK:
        if (keys & CM_UI_KEY_CIRCLE) {
            state->view = CM_UI_HOME;
            return action(CM_UI_ACTION_BACK, -1);
        }
        if (keys & CM_UI_KEY_CROSS)
            return action(CM_UI_ACTION_REQUEST_LINK, -1);
        break;
    case CM_UI_SETTINGS:
        if (keys & CM_UI_KEY_CIRCLE) {
            state->view = CM_UI_HOME;
            return action(CM_UI_ACTION_BACK, -1);
        }
        if (keys & CM_UI_KEY_UP) state->selected_setting = (state->selected_setting + 2) % 3;
        if (keys & CM_UI_KEY_DOWN) state->selected_setting = (state->selected_setting + 1) % 3;
        break;
    default:
        state->view = CM_UI_HOME;
        break;
    }
    return action(CM_UI_ACTION_NONE, -1);
}

static const char *view_name(cm_ui_view_t view) {
    static const char *names[] = {"INICIO", "CATALOGO", "VINCULAR", "AJUSTES"};
    return (view >= 0 && view < CM_UI_VIEW_COUNT) ? names[view] : "CLOUDMOON";
}

static void draw_header(const cm_ui_surface_t *s, cm_ui_view_t view,
                        const cm_ui_model_t *model) {
    int i;
    rect(s, 0, 0, s->width, 86, C_DARK);
    rect(s, 0, 84, s->width, 2, C_BORDER);
    rect(s, 46, 29, 5, 31, C_CYAN);
    rect(s, 56, 29, 5, 31, C_BLUE);
    text(s, 74, 29, 3, C_TEXT, "CLOUDMOON");
    text(s, 278, 40, 1, C_CYAN, "PS4 EDITION");
    for (i = 0; i < CM_UI_VIEW_COUNT; ++i) {
        int x = 490 + i * 135;
        int active = (i == (int)view);
        text(s, x, 40, 2, active ? C_CYAN : C_MUTED, view_name((cm_ui_view_t)i));
        if (active) rect(s, x, 62, (int)strlen(view_name((cm_ui_view_t)i)) * 12, 3, C_CYAN);
    }
    if (model && model->session_linked) {
        rect(s, s->width - 205, 31, 9, 9, C_GREEN);
        text(s, s->width - 188, 31, 1, C_GREEN, "CUENTA CONECTADA");
    } else {
        rect(s, s->width - 205, 31, 9, 9, C_MUTED);
        text(s, s->width - 188, 31, 1, C_MUTED, "SIN VINCULAR");
    }
}

static void draw_home(const cm_ui_surface_t *s, const cm_ui_state_t *st,
                      const cm_ui_model_t *m) {
    int count = game_count(m);
    text(s, 62, 128, 2, C_CYAN, "CLOUD GAMING, DIRECTO EN TU PS4");
    text(s, 62, 177, 6, C_TEXT, "TU NUBE.");
    text(s, 62, 224, 6, C_TEXT, "TU JUEGO.");
    text(s, 66, 290, 2, C_MUTED, "ENTRA A TU CUENTA CLOUDMOON Y ABRE TU CATALOGO.");
    panel(s, 62, 355, 360, 104, st->home_focus == 0);
    text(s, 91, 382, 3, C_TEXT, "EXPLORAR CATALOGO");
    text(s, 91, 421, 1, C_MUTED, "VER JUEGOS DISPONIBLES");
    panel(s, 62, 475, 360, 92, st->home_focus == 1);
    text(s, 91, 500, 3, C_TEXT, "VINCULAR CUENTA");
    text(s, 91, 537, 1, C_MUTED, "INICIAR CON TU SESION CLOUDMOON");
    panel(s, 480, 128, 735, 439, 0);
    text(s, 520, 164, 2, C_CYAN, "ESTADO DE LA CUENTA");
    if (m && m->session_linked) {
        text(s, 520, 225, 4, C_GREEN, "CONECTADO");
        text(s, 520, 288, 2, C_TEXT, m->account_label ? m->account_label : "CUENTA VINCULADA");
        text(s, 520, 330, 2, C_MUTED, count > 0 ? "CATALOGO LISTO PARA EXPLORAR" : "ESPERANDO CATALOGO");
    } else {
        text(s, 520, 225, 4, C_TEXT, "TU CUENTA TE ESPERA");
        text(s, 520, 288, 2, C_MUTED, "VINCULA TU SESION PARA CARGAR");
        text(s, 520, 320, 2, C_MUTED, "TU BIBLIOTECA Y TUS JUEGOS.");
    }
    rect(s, 520, 388, 650, 2, C_BORDER);
    text(s, 520, 422, 2, C_CYAN, "REPRODUCCION NATIVA");
    text(s, 520, 462, 2, C_MUTED, "INTERFAZ CLOUDMOON  |  CONTROL DUALSHOCK 4");
    text(s, 520, 501, 2, C_MUTED, "720P / 60 FPS COMO PERFIL INICIAL");
}

static void draw_library(const cm_ui_surface_t *s, const cm_ui_state_t *st,
                         const cm_ui_model_t *m) {
    int count = game_count(m), i;
    text(s, 62, 124, 3, C_TEXT, "TU CATALOGO");
    text(s, 62, 166, 2, C_MUTED, "JUEGOS DISPONIBLES EN TU CUENTA CLOUDMOON");
    if (count == 0) {
        panel(s, 62, 230, s->width - 124, 330, 0);
        centered_text(s, s->width / 2, 310, 4, C_TEXT, "CATALOGO AUN NO VINCULADO");
        centered_text(s, s->width / 2, 390, 2, C_MUTED, "VINCULA TU CUENTA PARA CARGAR TUS JUEGOS.");
        centered_text(s, s->width / 2, 450, 2, C_CYAN, "PULSA X PARA VINCULAR");
        return;
    }
    {
        int page_start = (st->selected_game / 8) * 8;
        int visible = count - page_start;
        if (visible > 8) visible = 8;
        for (i = 0; i < visible; ++i) {
            int game_index = page_start + i;
            int col = i % 4, row = i / 4;
            int x = 62 + col * 292, y = 222 + row * 160;
            panel(s, x, y, 264, 128, game_index == st->selected_game);
            text(s, x + 24, y + 35, 2, C_TEXT,
                 m->games[game_index] ? m->games[game_index] : "JUEGO");
            text(s, x + 24, y + 82, 1, C_MUTED,
                 game_index == st->selected_game ? "PULSA X PARA JUGAR" : "SELECCIONAR");
        }
        if (count > 8) {
            char page[48];
            (void)snprintf(page, sizeof(page), "PAGINA %d / %d",
                           st->selected_game / 8 + 1, (count + 7) / 8);
            text(s, s->width - 240, 166, 1, C_CYAN, page);
            text(s, 62, 560, 1, C_MUTED, "USA ARRIBA / ABAJO PARA CAMBIAR DE PAGINA");
        }
    }
}

static void draw_qr(const cm_ui_surface_t *s, const cm_ui_model_t *m) {
    int size, scale, x, y, xx, yy;
    if (!m || !m->qr_modules || m->qr_size < 21 || m->qr_size > 177) return;
    size = m->qr_size;
    scale = 260 / (size + 8);
    if (scale < 1) return;
    x = 830 + (280 - (size + 8) * scale) / 2;
    y = 190 + (280 - (size + 8) * scale) / 2;
    rect(s, 830, 190, 280, 280, C_WHITE);
    for (yy = 0; yy < size; ++yy)
        for (xx = 0; xx < size; ++xx)
            if (m->qr_modules[(size_t)yy * (size_t)size + (size_t)xx])
                rect(s, x + (xx + 4) * scale, y + (yy + 4) * scale,
                     scale, scale, C_DARK);
}

static void draw_link(const cm_ui_surface_t *s, const cm_ui_model_t *m) {
    text(s, 62, 125, 3, C_TEXT, "VINCULAR TU CUENTA");
    text(s, 62, 170, 2, C_MUTED, "AUTORIZA TU CUENTA DE CLOUDMOON DESDE EL TELEFONO.");
    panel(s, 62, 220, 690, 320, 0);
    text(s, 100, 260, 2, C_CYAN, "ACCESO SEGURO");
    text(s, 100, 310, 2, C_TEXT, "1. ESCANEA EL QR CON TU TELEFONO.");
    text(s, 100, 345, 2, C_TEXT, "2. INICIA SESION EN CLOUDMOON.");
    text(s, 100, 380, 2, C_TEXT, "3. USA EL MARCADOR VINCULAR PS4.");
    text(s, 100, 430, 1, C_MUTED, m && m->status_text ? m->status_text : "ESPERANDO AUTORIZACION DEL TELEFONO");
    if (m && m->link_code && m->link_code[0]) {
        text(s, 100, 465, 2, C_CYAN, "CODIGO DE VINCULACION:");
        text(s, 100, 500, 3, C_TEXT, m->link_code);
    }
    panel(s, 790, 155, 360, 360, 0);
    if (m && m->session_linked) {
        centered_text(s, 970, 295, 3, C_GREEN, "VINCULADA");
        centered_text(s, 970, 350, 2, C_TEXT, "SESION VALIDADA");
    } else {
        draw_qr(s, m);
        if (!m || !m->qr_modules)
            centered_text(s, 970, 310, 2, C_MUTED, "QR PENDIENTE");
        if (m && m->link_url && m->link_url[0])
            text(s, 830, 490, 1, C_CYAN, m->link_url);
    }
}

static void draw_settings(const cm_ui_surface_t *s, const cm_ui_state_t *st) {
    static const char *items[] = {"RESOLUCION DE STREAM", "FOTOGRAMAS POR SEGUNDO", "BITRATE"};
    static const char *values[] = {"1280 X 720", "60 FPS", "10 MBPS"};
    int i;
    text(s, 62, 125, 3, C_TEXT, "AJUSTES DE CLOUDMOON");
    text(s, 62, 170, 2, C_MUTED, "PERFIL INICIAL OPTIMIZADO PARA ESTABILIDAD");
    for (i = 0; i < 3; ++i) {
        int y = 230 + i * 100;
        panel(s, 62, y, 900, 78, i == st->selected_setting);
        text(s, 100, y + 25, 2, C_TEXT, items[i]);
        text(s, 740, y + 25, 2, C_CYAN, values[i]);
    }
    text(s, 62, 565, 1, C_MUTED, "LOS VALORES SE ACTIVARAN CUANDO ESTE LISTO EL MOTOR PS4.");
}

void cm_ui_draw(const cm_ui_surface_t *s, const cm_ui_state_t *st,
                const cm_ui_model_t *m) {
    if (!s || !s->pixels || !st || s->width < 640 || s->height < 360 ||
        s->pitch_pixels < s->width) return;
    rect(s, 0, 0, s->width, s->height, C_BG);
    draw_header(s, st->view, m);
    switch (st->view) {
    case CM_UI_HOME:     draw_home(s, st, m); break;
    case CM_UI_LIBRARY:  draw_library(s, st, m); break;
    case CM_UI_LINK:     draw_link(s, m); break;
    case CM_UI_SETTINGS: draw_settings(s, st); break;
    default:             draw_home(s, st, m); break;
    }
    rect(s, 0, s->height - 34, s->width, 34, C_DARK);
    text(s, 44, s->height - 22, 1, C_MUTED, "L1 / R1: SECCIONES     X: CONFIRMAR     O: VOLVER");
    text(s, s->width - 160, s->height - 22, 1, C_CYAN, "CLOUDMOON PS4");
}
