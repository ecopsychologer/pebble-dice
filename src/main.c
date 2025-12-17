#include <pebble.h>

#include "state.h"
#include "ui.h"

static Window *s_main_window;
static bool s_state_initialized;

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_select();
}

static void prv_select_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_select_long();
}

static void prv_back_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_back();
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_up();
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_down();
}

static void prv_down_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  state_handle_down_long();
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 600, prv_select_long_click_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click_handler);
  window_long_click_subscribe(BUTTON_ID_DOWN, 600, prv_down_long_click_handler, NULL);
}

static void prv_accel_tap_handler(AccelAxisType axis, int32_t direction) {
  state_handle_tap();
}

static void prv_window_load(Window *window) {
  window_set_click_config_provider(window, prv_click_config_provider);
  ui_init(window);
  state_init();
  s_state_initialized = true;
}

static void prv_window_unload(Window *window) {
  if (s_state_initialized) {
    state_deinit();
    s_state_initialized = false;
  }
  ui_deinit();
}

static void prv_init(void) {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_main_window, true);
  accel_tap_service_subscribe(prv_accel_tap_handler);
}

static void prv_deinit(void) {
  accel_tap_service_unsubscribe();
  if (s_main_window) {
    window_destroy(s_main_window);
    s_main_window = NULL;
  }
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
