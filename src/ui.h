#pragma once

#include <pebble.h>

#include "model.h"
#include "state.h"

#define UI_HINT_TEXT_LENGTH 12

typedef struct {
  AppState state;
  int rolling_value;
  bool skip_hint;
  bool is_animating;
  bool skip_requested;
  bool confirm_clear_prompt;
  char hint_top[UI_HINT_TEXT_LENGTH];
  char hint_middle[UI_HINT_TEXT_LENGTH];
  char hint_bottom[UI_HINT_TEXT_LENGTH];
} UiRenderData;

void ui_init(Window *window);
void ui_deinit(void);
void ui_render(const UiRenderData *data, const DiceModel *model);
void ui_scroll_reset(void);
bool ui_scroll_step(int direction);
