#include "state.h"

#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "roll_anim.h"
#include "ui.h"

// -----------------------------------------------------------------------------
// STATE MACHINE MODULE
// -----------------------------------------------------------------------------
// Owns the app-wide state machine, button handlers, and roll animation flow.
// ui.c never manipulates UI state by itself: it receives only UiRenderData.
//
// Safe tweaks:
// - Update the hint macros below to change button labels per screen.
// - Adjust RESULT_HOLD_MS if you want longer/shorter pauses between dice.
// - Extend the switch blocks in prv_render or state_handle_* when adding states.

#define RESULT_HOLD_MS 1000

#define HINT_REROLL "RE"
#define HINT_SELECT_HOLD_ROLL "Sel/\nHold\nRoll"
#define HINT_SELECT_SKIP "Tap\nSkip"
#define HINT_SCROLL "v"
#define HINT_BOTTOM_CLEAR "Clr"
#define HINT_CONFIRM "Cnfm"
#define HINT_ARROW_UP "^"
#define HINT_ARROW_DOWN "v"
#define HINT_PLUS "+"
#define HINT_MINUS "-"

// All mutable runtime info lives in this struct so we can reason about state
// transitions and animation timing in one place.
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
  DiceKind roll_kind;
  int roll_range;
  bool roll_zero_based;
  bool roll_tens_mode;
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
static void prv_prepare_roll_metadata(void);
static int prv_normalize_roll_value(int raw_value);
static int prv_random_result_value(void);

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

static void prv_copy_hint(char *dest, size_t size, const char *src) {
  if (!dest || size == 0) {
    return;
  }
  if (!src) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, size);
  dest[size - 1] = '\0';
}

// Utility to keep the “button legend” column easy to tweak per screen.
static void prv_set_hints(UiRenderData *view, const char *top, const char *middle, const char *bottom) {
  prv_copy_hint(view->hint_top, sizeof(view->hint_top), top);
  prv_copy_hint(view->hint_middle, sizeof(view->hint_middle), middle);
  prv_copy_hint(view->hint_bottom, sizeof(view->hint_bottom), bottom);
}

// Grab metadata about the die currently animating so we can normalize previews
// (d%, d100, etc.) consistently.
static void prv_prepare_roll_metadata(void) {
  s_ctx.roll_kind = model_current_roll_kind(&s_ctx.model);
  s_ctx.roll_range = model_current_roll_range(&s_ctx.model);
  if (s_ctx.roll_range <= 0) {
    s_ctx.roll_range = model_current_roll_sides(&s_ctx.model);
  }
  if (s_ctx.roll_range <= 0) {
    s_ctx.roll_range = 1;
  }
  s_ctx.roll_zero_based = model_kind_zero_based(s_ctx.roll_kind);
  s_ctx.roll_tens_mode = model_kind_tens_mode(s_ctx.roll_kind);
}

static int prv_normalize_roll_value(int raw_value) {
  if (raw_value <= 0) {
    return 0;
  }
  int value = raw_value;
  if (s_ctx.roll_zero_based) {
    value = raw_value - 1;
    if (value < 0) {
      value = 0;
    }
  }
  if (s_ctx.roll_tens_mode) {
    value *= 10;
  }
  return value;
}

static int prv_random_result_value(void) {
  if (s_ctx.roll_range <= 0) {
    return 0;
  }
  const int raw = (rand() % s_ctx.roll_range) + 1;
  return prv_normalize_roll_value(raw);
}

// Pushes state & hint data to ui.c so only this file needs to be touched when
// experimenting with flows/instructions. All UI screens are handled within this
// switch so it’s obvious which hints map to which state.
static void prv_render(void) {
  UiRenderData view = {
    .state = s_ctx.current_state,
    .rolling_value = s_ctx.rolling_value,
    .anim_progress_per_mille = roll_anim_progress_per_mille(),
    .confirm_clear_prompt = s_ctx.confirm_clear_prompt,
  };
  prv_set_hints(&view, "", "", "");

  switch (s_ctx.current_state) {
    case PICK_DIE:
      prv_set_hints(&view, HINT_ARROW_UP, HINT_SELECT_HOLD_ROLL, HINT_ARROW_DOWN);
      break;
    case PICK_COUNT:
      prv_set_hints(&view, HINT_PLUS, HINT_SELECT_HOLD_ROLL, HINT_MINUS);
      break;
    case ADD_GROUP_PROMPT:
      if (model_has_groups(&s_ctx.model)) {
        prv_set_hints(&view, HINT_ARROW_UP, HINT_SELECT_HOLD_ROLL, s_ctx.confirm_clear_prompt ? HINT_CONFIRM : HINT_BOTTOM_CLEAR);
      }
      break;
    case ROLLING:
      prv_set_hints(&view, HINT_REROLL, HINT_SELECT_SKIP, HINT_SCROLL);
      break;
    case RESULTS:
      prv_set_hints(&view, HINT_REROLL, HINT_SELECT_HOLD_ROLL, HINT_SCROLL);
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
  s_ctx.rolling_value = prv_normalize_roll_value(value);
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
  const int adjusted = prv_normalize_roll_value(value);
  prv_commit_result(adjusted);
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

// Core loop that animates one die at a time (or skips instantly when asked).
// Any changes to roll cadence (holding results longer, etc.) should happen here.
static void prv_start_next_die(void) {
  prv_cancel_result_hold_timer();

  if (!model_has_roll_remaining(&s_ctx.model)) {
    prv_finish_roll();
    return;
  }

  prv_prepare_roll_metadata();
  s_ctx.rolling_value = -1;

  if (s_ctx.skip_requested) {
    const int result = prv_random_result_value();
    prv_commit_result(result);
    prv_start_next_die();
    return;
  }

  const int range = (s_ctx.roll_range > 0) ? s_ctx.roll_range : 1;
  roll_anim_start(range);
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
  s_ctx.rolling_value = -1;

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

  const int selected_index = s_ctx.model.selected_die_index;
  const char *selected_label = model_get_selected_label(&s_ctx.model);
  const int selected_count = (s_ctx.current_state == PICK_COUNT) ? s_ctx.model.selected_count : 1;

  s_ctx.model.selected_die_index = selected_index;
  s_ctx.model.selected_count = selected_count;
  if (!model_commit_group(&s_ctx.model)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Quick roll setup failed");
    s_ctx.quick_roll_active = false;
    s_ctx.has_saved_model = false;
    s_ctx.model = s_ctx.saved_model;
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "Quick roll %s x%d", selected_label, selected_count);
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
  s_ctx.rolling_value = -1;
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

// ----- Input handlers -------------------------------------------------------
// Top-level input handlers stay grouped together so you can quickly reason
// about button mappings. Each switch simply translates the button press to
// model mutations + state transitions.
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
      if (model_has_groups(&s_ctx.model) && !s_ctx.confirm_clear_prompt) {
        s_ctx.confirm_clear_prompt = true;
        prv_render();
      }
      break;
    case ROLLING:
    case RESULTS:
      ui_scroll_step(1);
      break;
    default:
      break;
  }
}

void state_handle_down_long(void) {
  if (s_ctx.current_state == ROLLING || s_ctx.current_state == RESULTS) {
    ui_scroll_reset();
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
