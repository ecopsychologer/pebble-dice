#pragma once

#include <pebble.h>

typedef enum {
  PICK_DIE,
  PICK_COUNT,
  ADD_GROUP_PROMPT,
  ROLLING,
  RESULTS
} AppState;

void state_init(void);
void state_deinit(void);

void state_handle_select(void);
void state_handle_select_long(void);
void state_handle_back(void);
void state_handle_up(void);
void state_handle_down(void);
void state_handle_down_long(void);
void state_handle_tap(void);
