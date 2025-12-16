#include "roll_anim.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  RollAnimCallbacks callbacks;
  void *callback_context;
  AppTimer *timer;
  int sides;
  bool running;
  int stage_index;
  int stage_tick;
  int stage_tick_limit;
} RollAnimState;

static RollAnimState s_state;

typedef struct {
  uint16_t duration_ms;
  uint16_t step_ms;
} RollAnimStage;

static const RollAnimStage s_anim_stages[] = {
  {.duration_ms = 700, .step_ms = 40},
  {.duration_ms = 500, .step_ms = 70},
  {.duration_ms = 300, .step_ms = 110},
};
static const int s_anim_stage_count = ARRAY_LENGTH(s_anim_stages);

static int prv_random_roll(int sides) {
  if (sides <= 0) {
    return 0;
  }
  return (rand() % sides) + 1;
}

static int prv_stage_tick_limit(int stage_index) {
  if (stage_index < 0 || stage_index >= s_anim_stage_count) {
    return 1;
  }
  const RollAnimStage *stage = &s_anim_stages[stage_index];
  int ticks = stage->duration_ms / stage->step_ms;
  return (ticks < 1) ? 1 : ticks;
}

static void prv_finish_animation(void) {
  s_state.running = false;
  s_state.timer = NULL;
}

static void prv_timer_handler(void *data) {
  const bool final_stage = (s_state.stage_index >= s_anim_stage_count - 1);
  const bool final_tick = final_stage && (s_state.stage_tick >= s_state.stage_tick_limit - 1);
  const int value = prv_random_roll(s_state.sides);

  if (s_state.callbacks.on_preview) {
    s_state.callbacks.on_preview(value, s_state.callback_context);
  }

  if (final_tick) {
    if (s_state.callbacks.on_complete) {
      s_state.callbacks.on_complete(value, s_state.callback_context);
    }
    prv_finish_animation();
    return;
  }

  s_state.stage_tick++;
  if (s_state.stage_tick >= s_state.stage_tick_limit) {
    s_state.stage_index++;
    if (s_state.stage_index >= s_anim_stage_count) {
      s_state.stage_index = s_anim_stage_count - 1;
    }
    s_state.stage_tick = 0;
    s_state.stage_tick_limit = prv_stage_tick_limit(s_state.stage_index);
  }

  const RollAnimStage *stage = &s_anim_stages[s_state.stage_index];
  s_state.timer = app_timer_register(stage->step_ms, prv_timer_handler, NULL);
}

void roll_anim_init(const RollAnimCallbacks *callbacks, void *context) {
  memset(&s_state, 0, sizeof(s_state));
  if (callbacks) {
    s_state.callbacks = *callbacks;
  }
  s_state.callback_context = context;
  s_state.stage_index = 0;
  s_state.stage_tick = 0;
  s_state.stage_tick_limit = prv_stage_tick_limit(0);
  srand(time(NULL));
}

void roll_anim_deinit(void) {
  if (s_state.timer) {
    app_timer_cancel(s_state.timer);
    s_state.timer = NULL;
  }
  s_state.running = false;
}

void roll_anim_start(int sides) {
  if (s_state.running && s_state.timer) {
    app_timer_cancel(s_state.timer);
  }
  s_state.sides = sides;
  s_state.stage_index = 0;
  s_state.stage_tick = 0;
  s_state.stage_tick_limit = prv_stage_tick_limit(0);
  s_state.running = true;
  s_state.timer = app_timer_register(s_anim_stages[0].step_ms, prv_timer_handler, NULL);
}

void roll_anim_skip(void) {
  if (!s_state.running) {
    return;
  }

  if (s_state.timer) {
    app_timer_cancel(s_state.timer);
    s_state.timer = NULL;
  }

  const int result = prv_random_roll(s_state.sides);
  if (s_state.callbacks.on_preview) {
    s_state.callbacks.on_preview(result, s_state.callback_context);
  }
  if (s_state.callbacks.on_complete) {
    s_state.callbacks.on_complete(result, s_state.callback_context);
  }
  prv_finish_animation();
}

bool roll_anim_is_running(void) {
  return s_state.running;
}
