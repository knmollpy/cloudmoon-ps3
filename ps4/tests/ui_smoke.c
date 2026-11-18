#include "../src/ui/cloudmoon_ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    cm_ui_state_t state;
    cm_ui_model_t model = {0};
    cm_ui_surface_t surface;
    const char *games[] = {"ROBLOX", "BRAWL STARS"};
    cm_ui_action_t result;
    uint32_t *pixels = (uint32_t *)calloc(1280u * 720u, sizeof(uint32_t));

    assert(pixels != NULL);
    surface.pixels = pixels;
    surface.width = 1280;
    surface.height = 720;
    surface.pitch_pixels = 1280;

    model.games = games;
    model.game_count = 2;
    model.session_linked = 1;
    model.account_label = "TEST ACCOUNT";

    cm_ui_init(&state);
    assert(state.view == CM_UI_HOME);
    cm_ui_draw(&surface, &state, &model);
    assert(pixels[0] != 0u);

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

    state.view = CM_UI_LINK;
    result = cm_ui_input(&state, CM_UI_KEY_CROSS, &model);
    assert(result.type == CM_UI_ACTION_REQUEST_LINK);

    free(pixels);
    return 0;
}
