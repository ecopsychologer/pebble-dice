#include "state.h"

#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "roll_anim.h"
#include "ui.h"

#define RESULT_HOLD_MS 1000

#define HINT_REROLL "RR"
#define HINT_SELECT_HOLD_ROLL "Sel/\nHold\nRoll"
#define HINT_SELECT_CLEAR "Sel\nClr"
#define HINT_SELECT_DONE "Sel\nDone"
#define HINT_SELECT_SKIP "Tap\nSkip"
#define HINT_HINT_SCROLL "Scroll"
#define HINT_BOTTOM_CLEAR "Clr"
#define HINT_BOTTOM_CONFIRM "Cnfm"

typedef struct {
  AppState current_state;
  DiceModel model;
  int rolling_value;
  bool skip_requested;
  bool initialized;
  bool quick_roll_active;
  bool has_saved_model;
  DiceModel saved_model;
  AppTimer *result_hold_timer;
  bool confirm_clear_prompt;
} StateContext;

static StateContext s_ctx;

static void prv_render(void);
static void prv_start_next_die(void);
static void prv_finish_roll(void);
static void prv_begin_roll(void);
static void prv_restore_saved_model(void);
static void prv_begin_quick_roll(void);
static void prv_cancel_result_hold_timer(void);
static void prv_result_hold_timer_cb(void *context);
static void prv_after_result(void);
static bool prv_rewind_last_group(void);

static int prv_random_roll(int sides) {
  if (sides <= 0) {
    return 0;
  }
  return (rand() % sides) + 1;
}

static const char *prv_state_name(AppState state) {
  switch (state) {
    case PICK_DIE:
      return "PICK_DIE";
    case PICK_COUNT:
      return "PICK_COUNT";
    case ADD_GROUP_PROMPT:
      return "ADD_GROUP_PROMPT";
    case ROLLING:
      return "ROLLING";
    case RESULTS:
      return "RESULTS";
  }
  return "UNKNOWN";
}

static void prv_render(void) {
  UiRenderData view = {
    .state = s_ctx.current_state,
    .rolling_value = s_ctx.rolling_value,
    .skip_hint = s_ctx.current_state == ROLLING,
    .is_animating = roll_anim_is_running(),
    .skip_requested = s_ctx.skip_requested,
    .confirm_clear_prompt = s_ctx.confirm_clear_prompt,
  };

  memset(view.hint_top, 0, sizeof(view.hint_top));
  memset(view.hint_middle, 0, sizeof(view.hint_middle));
  memset(view.hint_bottom, 0, sizeof(view.hint_bottom));

  switch (s_ctx.current_state) {
    case PICK_DIE:
    case PICK_COUNT:
      snprintf(view.hint_middle, sizeof(view.hint_middle), "Hold Roll");
      break;
    case ADD_GROUP_PROMPT:
      if (model_has_groups(&s_ctx.model)) {
        snprintf(view.hint_middle, sizeof(view.hint_middle), "Hold Roll");
        if (s_ctx.confirm_clear_prompt) {
          snprintf(view.hint_bottom, sizeof(view.hint_bottom), "Confirm");
        } else {
          snprintf(view.hint_bottom, sizeof(view.hint_bottom), "Clear");
        }
      }
      break;
    case ROLLING:
      snprintf(view.hint_middle, sizeof(view.hint_middle), "Hold Skip");
      break;
    case RESULTS:
      snprintf(view.hint_top, sizeof(view.hint_top), "RR");
      snprintf(view.hint_middle, sizeof(view.hint_middle), "Hold Roll");
      break;
  }

  ui_render(&view, &s_ctx.model);
}

static void prv_set_state(AppState new_state) {
  if (s_ctx.current_state == new_state) {
    prv_render();
    return;
  }

  if (new_state != ADD_GROUP_PROMPT) {
    s_ctx.confirm_clear_prompt = false;
  }

  s_ctx.current_state = new_state;
  APP_LOG(APP_LOG_LEVEL_INFO, "STATE -> %s", prv_state_name(new_state));
  prv_render();
}

static void prv_anim_preview(int value, void *context) {
  s_ctx.rolling_value = value;
  prv_render();
}

static void prv_commit_result(int value) {
  const int sides = model_current_roll_sides(&s_ctx.model);
  model_commit_roll_result(&s_ctx.model, value);
  s_ctx.rolling_value = value;

  const int completed = model_roll_completed_dice(&s_ctx.model);
  const int total = model_roll_total_dice(&s_ctx.model);
  APP_LOG(APP_LOG_LEVEL_INFO, "ROLL d%d → %d (%d/%d)", sides, value, completed, total);
}

static void prv_anim_complete(int value, void *context) {
  prv_commit_result(value);
  prv_after_result();
}

static void prv_set_skip_requested(void) {
  if (s_ctx.current_state != ROLLING) {
    return;
  }

  if (s_ctx.skip_requested) {
    return;
  }

  s_ctx.skip_requested = true;
  if (s_ctx.result_hold_timer) {
    prv_cancel_result_hold_timer();
    prv_start_next_die();
  }
  if (roll_anim_is_running()) {
    roll_anim_skip();
  }
}

static void prv_start_next_die(void) {
  prv_cancel_result_hold_timer();

  if (!model_has_roll_remaining(&s_ctx.model)) {
    prv_finish_roll();
    return;
  }

  if (s_ctx.skip_requested) {
    const int result = prv_random_roll(model_current_roll_sides(&s_ctx.model));
    prv_commit_result(result);
    prv_start_next_die();
    return;
  }

  const int sides = model_current_roll_sides(&s_ctx.model);
  s_ctx.rolling_value = 0;
  roll_anim_start(sides);
}

static void prv_finish_roll(void) {
  prv_cancel_result_hold_timer();
  s_ctx.skip_requested = false;
  prv_set_state(RESULTS);
}

static void prv_begin_roll(void) {
  if (!model_has_groups(&s_ctx.model)) {
    return;
  }

  prv_cancel_result_hold_timer();
  model_begin_roll(&s_ctx.model);
  s_ctx.skip_requested = false;
  s_ctx.rolling_value = 0;

  prv_set_state(ROLLING);
  prv_start_next_die();
}

static void prv_restore_saved_model(void) {
  if (!s_ctx.quick_roll_active || !s_ctx.has_saved_model) {
    return;
  }
  s_ctx.model = s_ctx.saved_model;
  s_ctx.quick_roll_active = false;
  s_ctx.has_saved_model = false;
  APP_LOG(APP_LOG_LEVEL_INFO, "Quick roll complete, restoring configuration");
  prv_render();
}

static void prv_begin_quick_roll(void) {
  if (s_ctx.quick_roll_active) {
    return;
  }

  s_ctx.saved_model = s_ctx.model;
  s_ctx.has_saved_model = true;
  s_ctx.quick_roll_active = true;

  DiceModel temp = s_ctx.model;
  model_clear_groups(&s_ctx.model);
  s_ctx.model.selected_die_index = temp.selected_die_index;
  s_ctx.model.selected_count = 1;

  if (!model_commit_group(&s_ctx.model)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Quick roll setup failed");
    s_ctx.model = s_ctx.saved_model;
    s_ctx.has_saved_model = false;
    s_ctx.quick_roll_active = false;
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Quick roll %s", model_get_selected_label(&temp));
  prv_begin_roll();
}

static void prv_cancel_result_hold_timer(void) {
  if (s_ctx.result_hold_timer) {
    app_timer_cancel(s_ctx.result_hold_timer);
    s_ctx.result_hold_timer = NULL;
  }
}

static void prv_result_hold_timer_cb(void *context) {
  s_ctx.result_hold_timer = NULL;
  prv_start_next_die();
}

static void prv_after_result(void) {
  if (s_ctx.skip_requested) {
    prv_start_next_die();
    return;
  }
  prv_cancel_result_hold_timer();
  s_ctx.result_hold_timer = app_timer_register(RESULT_HOLD_MS, prv_result_hold_timer_cb, NULL);
}

static bool prv_rewind_last_group(void) {
  if (s_ctx.model.group_count <= 0) {
    return false;
  }

  DiceGroup *last = &s_ctx.model.groups[s_ctx.model.group_count - 1];
  s_ctx.model.group_count--;
  s_ctx.model.selected_die_index = last->die_def_index;
  s_ctx.model.selected_count = last->count;
  return true;
}

void state_init(void) {
  if (s_ctx.initialized) {
    return;
  }

  memset(&s_ctx, 0, sizeof(s_ctx));
  model_init(&s_ctx.model);
  RollAnimCallbacks callbacks = {
    .on_preview = prv_anim_preview,
    .on_complete = prv_anim_complete,
  };
  roll_anim_init(&callbacks, NULL);
  s_ctx.initialized = true;

  prv_set_state(PICK_DIE);
}

void state_deinit(void) {
  prv_cancel_result_hold_timer();
  roll_anim_deinit();
  s_ctx.initialized = false;
}

void state_handle_select(void) {
  switch (s_ctx.current_state) {
    case PICK_DIE:
      model_reset_selection_count(&s_ctx.model);
      prv_set_state(PICK_COUNT);
      break;
    case PICK_COUNT:
      if (model_commit_group(&s_ctx.model)) {
        model_reset_selection_count(&s_ctx.model);
        prv_set_state(ADD_GROUP_PROMPT);
      } else {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Cannot add more groups");
      }
      break;
    case ADD_GROUP_PROMPT:
      if (s_ctx.confirm_clear_prompt) {
        model_clear_groups(&s_ctx.model);
        model_reset_selection_count(&s_ctx.model);
        s_ctx.confirm_clear_prompt = false;
      }
      prv_set_state(PICK_DIE);
      break;
    case ROLLING:
      prv_set_skip_requested();
      break;
    case RESULTS:
      prv_restore_saved_model();
      model_clear_groups(&s_ctx.model);
      model_reset_selection_count(&s_ctx.model);
      prv_set_state(PICK_DIE);
      break;
  }
}

void state_handle_back(void) {
  switch (s_ctx.current_state) {
    case PICK_DIE:
      if (model_has_groups(&s_ctx.model)) {
        prv_set_state(ADD_GROUP_PROMPT);
      } else {
        window_stack_pop(true);
      }
      break;
    case PICK_COUNT:
      prv_set_state(PICK_DIE);
      break;
    case ADD_GROUP_PROMPT:
      if (s_ctx.confirm_clear_prompt) {
        s_ctx.confirm_clear_prompt = false;
        prv_render();
      } else if (prv_rewind_last_group()) {
        prv_set_state(PICK_COUNT);
      } else {
        prv_set_state(PICK_DIE);
      }
      break;
    case ROLLING:
      prv_set_skip_requested();
      break;
    case RESULTS:
      prv_restore_saved_model();
      model_clear_groups(&s_ctx.model);
      model_reset_selection_count(&s_ctx.model);
      prv_set_state(PICK_DIE);
      break;
  }
}

void state_handle_up(void) {
  switch (s_ctx.current_state) {
    case PICK_DIE:
      model_increment_selected_die(&s_ctx.model, 1);
      prv_render();
      break;
    case PICK_COUNT:
      model_increment_selected_count(&s_ctx.model, 1);
      prv_render();
      break;
    case RESULTS:
      if (model_has_groups(&s_ctx.model)) {
        prv_begin_roll();
      }
      break;
    default:
      break;
  }
}

void state_handle_down(void) {
  switch (s_ctx.current_state) {
    case PICK_DIE:
      model_increment_selected_die(&s_ctx.model, -1);
      prv_render();
      break;
    case PICK_COUNT:
      model_increment_selected_count(&s_ctx.model, -1);
      prv_render();
      break;
    case ADD_GROUP_PROMPT:
      if (model_has_groups(&s_ctx.model)) {
        s_ctx.confirm_clear_prompt = true;
        prv_render();
      }
      break;
    default:
      break;
  }
}

void state_handle_tap(void) {
  prv_set_skip_requested();
}

void state_handle_select_long(void) {
  if (s_ctx.current_state == ROLLING) {
    prv_set_skip_requested();
    return;
  }

  if (s_ctx.current_state == PICK_DIE || s_ctx.current_state == PICK_COUNT) {
    prv_begin_quick_roll();
    return;
  }

  if (model_has_groups(&s_ctx.model)) {
    prv_begin_roll();
  } else {
    prv_begin_quick_roll();
  }
}
