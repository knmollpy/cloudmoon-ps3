#include "../src/ui/cloudmoon_ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    cm_ui_state_t state;
    cm_ui_model_t model = {0};
    cm_ui_surface_t surface;
    const char *games[] = {
        "GAME 00", "GAME 01", "GAME 02", "GAME 03",
        "GAME 04", "GAME 05", "GAME 06", "GAME 07",
        "GAME 08", "GAME 09", "GAME 10", "GAME 11",
        "GAME 12", "GAME 13", "GAME 14", "GAME 15",
        "GAME 16"
    };
    cm_ui_action_t result;
    uint32_t *pixels = (uint32_t *)calloc(1280u * 720u, sizeof(uint32_t));

    assert(pixels != NULL);
    surface.pixels = pixels;
    surface.width = 1280;
    surface.height = 720;
    surface.pitch_pixels = 1280;

    model.games = games;
    model.game_count = 17;
    model.session_linked = 1;
    model.account_label = "TEST ACCOUNT";

    cm_ui_init(&state);
    assert(state.view == CM_UI_HOME);
    cm_ui_draw(&surface, &state, &model);
    assert(pixels[0] != 0u);

    /* The in-stream menu is composited over the decoded frame, not a
       touch-control layer. Its central panel must be rendered visibly. */
    pixels[260u * 1280u + 640u] = 0xFF606060u;
    cm_ui_draw_game_menu_overlay(&surface);
    assert(pixels[260u * 1280u + 640u] == 0xFF111B2Cu);

    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_OPEN_LIBRARY);
    assert(state.view == CM_UI_LIBRARY);

    result = cm_ui_input(&state, CM_UI_KEY_RIGHT, &model);
    assert(result.type == CM_UI_ACTION_NONE);
    assert(state.selected_game == 1);

    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_PLAY_GAME);
    assert(result.game_index == 1);

    result = cm_ui_input(&state, CM_UI_KEY_CIRCLE, &model);
    assert(result.type == CM_UI_ACTION_BACK);
    assert(state.view == CM_UI_HOME);

    state.view = CM_UI_LIBRARY;
    state.selected_game = 0;
    (void)cm_ui_input(&state, CM_UI_KEY_DOWN, &model);
    (void)cm_ui_input(&state, CM_UI_KEY_DOWN, &model);
    assert(state.selected_game == 8);
    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_PLAY_GAME);
    assert(result.game_index == 8);

    /* An empty catalog must not launch the login keyboard when CROSS is
       pressed. The user enters VINCULAR explicitly with R1/L1. */
    model.game_count = 0;
    state.view = CM_UI_LIBRARY;
    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_NONE);
    assert(state.view == CM_UI_LIBRARY);

    result = cm_ui_input(&state, CM_UI_KEY_R1, &model);
    assert(result.type == CM_UI_ACTION_REQUEST_LINK);
    assert(state.view == CM_UI_LINK);
    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_NONE);
    result = cm_ui_input(&state, CM_UI_KEY_CIRCLE, &model);
    assert(result.type == CM_UI_ACTION_BACK);

    free(pixels);
    return 0;
}
