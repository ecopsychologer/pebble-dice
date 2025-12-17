#pragma once

#include <pebble.h>

typedef void (*RollAnimValueHandler)(int value, void *context);

typedef struct {
  RollAnimValueHandler on_preview;
  RollAnimValueHandler on_complete;
} RollAnimCallbacks;

void roll_anim_init(const RollAnimCallbacks *callbacks, void *context);
void roll_anim_deinit(void);

void roll_anim_start(int sides);
void roll_anim_skip(void);
bool roll_anim_is_running(void);
int roll_anim_progress_per_mille(void);
