#ifndef CLOUDMOON_PS4_UI_H
#define CLOUDMOON_PS4_UI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CM_UI_MAX_GAMES 512

typedef enum {
    CM_UI_HOME = 0,
    CM_UI_LIBRARY,
    CM_UI_LINK,
    CM_UI_SETTINGS,
    CM_UI_VIEW_COUNT
} cm_ui_view_t;

typedef enum {
    CM_UI_ACTION_NONE = 0,
    CM_UI_ACTION_OPEN_LIBRARY,
    CM_UI_ACTION_REQUEST_LINK,
    CM_UI_ACTION_PLAY_GAME,
    CM_UI_ACTION_BACK
} cm_ui_action_type_t;

typedef struct {
    cm_ui_action_type_t type;
    int game_index;
} cm_ui_action_t;

enum {
    CM_UI_KEY_UP      = 1u << 0,
    CM_UI_KEY_DOWN    = 1u << 1,
    CM_UI_KEY_LEFT    = 1u << 2,
    CM_UI_KEY_RIGHT   = 1u << 3,
    CM_UI_KEY_CROSS   = 1u << 4,
    CM_UI_KEY_CIRCLE  = 1u << 5,
    CM_UI_KEY_L1      = 1u << 6,
    CM_UI_KEY_R1      = 1u << 7,
    CM_UI_KEY_OPTIONS = 1u << 8
};

typedef struct {
    uint32_t *pixels;
    int width;
    int height;
    int pitch_pixels;
} cm_ui_surface_t;

typedef struct {
    const char *account_label;
    const char *status_text;
    const char *const *games;
    int game_count;
    int session_linked;

    /* A real pairing service may supply a QR matrix. No placeholder QR is
       generated: qr_modules must contain qr_size * qr_size 0/1 values. */
    const uint8_t *qr_modules;
    int qr_size;
    const char *link_url;
    const char *link_code;
} cm_ui_model_t;

typedef struct {
    cm_ui_view_t view;
    int home_focus;
    int selected_game;
    int selected_setting;
} cm_ui_state_t;

void cm_ui_init(cm_ui_state_t *state);
cm_ui_action_t cm_ui_input(cm_ui_state_t *state, unsigned int pressed,
                           const cm_ui_model_t *model);
void cm_ui_draw(const cm_ui_surface_t *surface,
                const cm_ui_state_t *state,
                const cm_ui_model_t *model);

#ifdef __cplusplus
}
#endif
#endif
