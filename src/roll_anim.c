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
  bool in_final_stage;
  bool in_hold_stage;
  int final_tick_target;
  int final_tick_count;
  int final_tick_interval_ms;
  int final_duration_ms;
  int hold_duration_ms;
  int pending_final_value;
  bool has_pending_value;
  int total_duration_ms;
  int elapsed_ms;
} RollAnimState;

static RollAnimState s_state;

typedef struct {
  uint16_t duration_ms;
  uint16_t step_ms;
} RollAnimStage;

static const RollAnimStage s_main_stages[] = {
  {.duration_ms = 700, .step_ms = 40},
  {.duration_ms = 500, .step_ms = 70},
  {.duration_ms = 300, .step_ms = 110},
};
static const int s_main_stage_count = ARRAY_LENGTH(s_main_stages);
static const int s_final_hold_ms = 350;
static const int s_final_ticks_min = 3;
static const int s_final_ticks_max = 4;

static int prv_total_main_duration(void) {
  int total = 0;
  for (int i = 0; i < s_main_stage_count; ++i) {
    total += s_main_stages[i].duration_ms;
  }
  return total;
}

static int prv_random_roll(int sides) {
  if (sides <= 0) {
    return 0;
  }
  return (rand() % sides) + 1;
}

static int prv_stage_tick_limit(int stage_index) {
  if (stage_index < 0 || stage_index >= s_main_stage_count) {
    return 1;
  }
  const RollAnimStage *stage = &s_main_stages[stage_index];
  const int ticks = stage->duration_ms / stage->step_ms;
  return (ticks < 1) ? 1 : ticks;
}

static void prv_finish_animation(void) {
  s_state.running = false;
  s_state.timer = NULL;
  s_state.elapsed_ms = s_state.total_duration_ms;
}

static void prv_timer_handler(void *data) {
  if (s_state.in_hold_stage) {
    s_state.elapsed_ms += s_state.hold_duration_ms;
    s_state.in_hold_stage = false;
    if (s_state.callbacks.on_complete && s_state.has_pending_value) {
      s_state.callbacks.on_complete(s_state.pending_final_value, s_state.callback_context);
    }
    prv_finish_animation();
    return;
  }

  const bool playing_final = s_state.in_final_stage;
  const int step_ms = playing_final ? s_state.final_tick_interval_ms : s_main_stages[s_state.stage_index].step_ms;
  const int value = prv_random_roll(s_state.sides);

  if (s_state.callbacks.on_preview) {
    s_state.callbacks.on_preview(value, s_state.callback_context);
  }

  s_state.elapsed_ms += step_ms;

  if (!playing_final) {
    s_state.stage_tick++;
    if (s_state.stage_tick >= s_state.stage_tick_limit) {
      s_state.stage_index++;
      if (s_state.stage_index >= s_main_stage_count) {
        s_state.in_final_stage = true;
        s_state.final_tick_count = 0;
      } else {
        s_state.stage_tick = 0;
        s_state.stage_tick_limit = prv_stage_tick_limit(s_state.stage_index);
      }
    }

    const int next_delay = s_state.in_final_stage ? s_state.final_tick_interval_ms : s_main_stages[s_state.stage_index].step_ms;
    s_state.timer = app_timer_register(next_delay, prv_timer_handler, NULL);
    return;
  }

  s_state.final_tick_count++;
  if (s_state.final_tick_count >= s_state.final_tick_target) {
    s_state.pending_final_value = value;
    s_state.has_pending_value = true;
    s_state.in_hold_stage = true;
    s_state.timer = app_timer_register(s_state.hold_duration_ms, prv_timer_handler, NULL);
    return;
  }

  s_state.timer = app_timer_register(s_state.final_tick_interval_ms, prv_timer_handler, NULL);
}

void roll_anim_init(const RollAnimCallbacks *callbacks, void *context) {
  memset(&s_state, 0, sizeof(s_state));
  if (callbacks) {
    s_state.callbacks = *callbacks;
  }
  s_state.callback_context = context;
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
  s_state.in_final_stage = false;
  s_state.in_hold_stage = false;
  const int span = s_final_ticks_max - s_final_ticks_min + 1;
  s_state.final_tick_target = s_final_ticks_min + ((span > 0) ? rand() % span : 0);
  if (s_state.final_tick_target <= 0) {
    s_state.final_tick_target = s_final_ticks_min;
  }
  s_state.final_duration_ms = 1500;
  s_state.final_tick_interval_ms = s_state.final_duration_ms / s_state.final_tick_target;
  if (s_state.final_tick_interval_ms <= 0) {
    s_state.final_tick_interval_ms = 350;
  }
  s_state.final_tick_count = 0;
  s_state.hold_duration_ms = s_final_hold_ms;
  s_state.pending_final_value = 0;
  s_state.has_pending_value = false;
  s_state.elapsed_ms = 0;
  s_state.total_duration_ms = prv_total_main_duration() + s_state.final_duration_ms + s_state.hold_duration_ms;
  s_state.running = true;
  s_state.timer = app_timer_register(s_main_stages[0].step_ms, prv_timer_handler, NULL);
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
  s_state.elapsed_ms = s_state.total_duration_ms;
  prv_finish_animation();
}

bool roll_anim_is_running(void) {
  return s_state.running;
}

int roll_anim_progress_per_mille(void) {
  if (s_state.total_duration_ms <= 0) {
    return s_state.running ? 0 : 1000;
  }
  int progress = (s_state.elapsed_ms * 1000) / s_state.total_duration_ms;
  if (progress > 1000) {
    progress = 1000;
  }
  if (progress < 0) {
    progress = 0;
  }
  return progress;
}
