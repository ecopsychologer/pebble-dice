#include "model.h"

#include <stdbool.h>
#include <string.h>

// -----------------------------------------------------------------------------
// MODEL MODULE
// -----------------------------------------------------------------------------
// Keeps all dice configuration and roll results in one place. Other modules
// interact with the model exclusively through the functions declared in
// model.h, so they never need to touch DiceGroup internals directly.
//
// Safe tweaks:
// - Update s_die_defs if you add/remove a die type.
// - Raise MAX_* constants in model.h if you need more storage.
// - Extend helper functions at the bottom when adding new metadata.

// Each die kind carries metadata so other modules can react without special
// cases (zero-based dice, tens dice, etc.).
typedef struct {
  int display_sides;
  int roll_sides;
  const char *label;
  bool zero_based;
  bool tens_mode;
} DieDefinition;

static const DieDefinition s_die_defs[DICE_KIND_COUNT] = {
  [DICE_KIND_D4] = {.display_sides = 4, .roll_sides = 4, .label = "d4", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D6] = {.display_sides = 6, .roll_sides = 6, .label = "d6", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D8] = {.display_sides = 8, .roll_sides = 8, .label = "d8", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D10] = {.display_sides = 10, .roll_sides = 10, .label = "d10", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D12] = {.display_sides = 12, .roll_sides = 12, .label = "d12", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D20] = {.display_sides = 20, .roll_sides = 20, .label = "d20", .zero_based = false, .tens_mode = false},
  [DICE_KIND_D100] = {.display_sides = 100, .roll_sides = 10, .label = "d100", .zero_based = true, .tens_mode = true},
  [DICE_KIND_PERCENTILE] = {.display_sides = 100, .roll_sides = 100, .label = "d%", .zero_based = true, .tens_mode = false},
};

static const DieDefinition *prv_die_def_at_index(int index) {
  if (index < 0 || index >= DICE_KIND_COUNT) {
    return NULL;
  }
  return &s_die_defs[index];
}

static void prv_clamp_selection(DiceModel *model) {
  if (model->selected_die_index < 0) {
    model->selected_die_index = 0;
  } else if (model->selected_die_index >= DICE_KIND_COUNT) {
    model->selected_die_index = DICE_KIND_COUNT - 1;
  }

  if (model->selected_count < 1) {
    model->selected_count = 1;
  } else if (model->selected_count > MAX_DICE_PER_GROUP) {
    model->selected_count = MAX_DICE_PER_GROUP;
  }
}

// ----- Selection helpers ----------------------------------------------------
// Provide a trivial interface for the UI/state machine to change the user's
// selection. App code never touches DiceModel internals directly.
void model_init(DiceModel *model) {
  memset(model, 0, sizeof(*model));
  model->selected_die_index = DICE_KIND_D6;
  model->selected_count = 1;
}

int model_increment_selected_die(DiceModel *model, int delta) {
  const int count = DICE_KIND_COUNT;
  model->selected_die_index = (model->selected_die_index + count + delta) % count;
  const DieDefinition *def = prv_die_def_at_index(model->selected_die_index);
  return def ? def->display_sides : 0;
}

int model_increment_selected_count(DiceModel *model, int delta) {
  model->selected_count += delta;
  prv_clamp_selection(model);
  return model->selected_count;
}

int model_get_selected_sides(const DiceModel *model) {
  if (!model) {
    return 0;
  }
  const DieDefinition *def = prv_die_def_at_index(model->selected_die_index);
  return def ? def->display_sides : 0;
}

int model_get_selected_count(const DiceModel *model) {
  return model->selected_count;
}

const char *model_get_selected_label(const DiceModel *model) {
  if (!model) {
    return "";
  }
  const DieDefinition *def = prv_die_def_at_index(model->selected_die_index);
  if (!def) {
    return "d?";
  }
  return def->label;
}

int model_get_selected_die_index(const DiceModel *model) {
  if (!model) {
    return 0;
  }
  return model->selected_die_index;
}

// ----- Configuration model --------------------------------------------------
// Handles adding/clearing dice groups and exposes read-only accessors that the
// UI can consume when it needs to render a summary of configured dice.
bool model_commit_group(DiceModel *model) {
  if (model->group_count >= MAX_DICE_GROUPS) {
    return false;
  }

  DiceGroup *group = &model->groups[model->group_count++];
  group->die_def_index = model->selected_die_index;
  group->sides = model_get_selected_sides(model);
  group->count = model->selected_count;
  memset(group->results, 0, sizeof(group->results));
  return true;
}

void model_clear_groups(DiceModel *model) {
  memset(model->groups, 0, sizeof(model->groups));
  model->group_count = 0;
  model->roll_group_index = 0;
  model->roll_die_index = 0;
}

bool model_has_groups(const DiceModel *model) {
  return model->group_count > 0;
}

// ----- Rolling helpers ------------------------------------------------------
// Manage the cursor (group+die index) while rolling so state.c can simply ask
// "what die is next?" without touching internal counters.
void model_begin_roll(DiceModel *model) {
  for (int g = 0; g < model->group_count; ++g) {
    memset(model->groups[g].results, 0, sizeof(model->groups[g].results));
  }
  model->roll_group_index = 0;
  model->roll_die_index = 0;
}

bool model_has_roll_remaining(const DiceModel *model) {
  return model->roll_group_index < model->group_count;
}

int model_current_roll_sides(const DiceModel *model) {
  if (!model_has_roll_remaining(model)) {
    return 0;
  }
  return model_group_sides(&model->groups[model->roll_group_index]);
}

int model_current_roll_range(const DiceModel *model) {
  if (!model_has_roll_remaining(model)) {
    return 0;
  }
  DiceKind kind = model_current_roll_kind(model);
  return model_kind_roll_sides(kind);
}

void model_commit_roll_result(DiceModel *model, int value) {
  if (!model_has_roll_remaining(model)) {
    return;
  }

  DiceGroup *group = &model->groups[model->roll_group_index];
  if (model->roll_die_index < group->count) {
    group->results[model->roll_die_index] = value;
  }
  model->roll_die_index++;
  if (model->roll_die_index >= group->count) {
    model->roll_group_index++;
    model->roll_die_index = 0;
  }
}

int model_roll_completed_dice(const DiceModel *model) {
  int completed = 0;
  for (int g = 0; g < model->roll_group_index; ++g) {
    completed += model->groups[g].count;
  }
  return completed + model->roll_die_index;
}

int model_roll_total_dice(const DiceModel *model) {
  int total = 0;
  for (int g = 0; g < model->group_count; ++g) {
    total += model->groups[g].count;
  }
  return total;
}

int model_group_count(const DiceModel *model) {
  return model->group_count;
}

const DiceGroup *model_get_group(const DiceModel *model, int index) {
  if (index < 0 || index >= model->group_count) {
    return NULL;
  }
  return &model->groups[index];
}

void model_reset_selection_count(DiceModel *model) {
  model->selected_count = 1;
  prv_clamp_selection(model);
}

const char *model_group_label(const DiceGroup *group) {
  if (!group) {
    return "";
  }
  const DieDefinition *def = prv_die_def_at_index(group->die_def_index);
  if (!def) {
    return "d?";
  }
  return def->label;
}

int model_group_sides(const DiceGroup *group) {
  if (!group) {
    return 0;
  }
  return group->sides;
}

const char *model_current_roll_label(const DiceModel *model) {
  if (!model_has_roll_remaining(model)) {
    return "";
  }
  return model_group_label(&model->groups[model->roll_group_index]);
}

DiceKind model_current_roll_kind(const DiceModel *model) {
  if (!model || !model_has_roll_remaining(model)) {
    return DICE_KIND_D6;
  }
  const DiceGroup *group = &model->groups[model->roll_group_index];
  if (!group) {
    return DICE_KIND_D6;
  }
  return (DiceKind)group->die_def_index;
}

int model_kind_roll_sides(DiceKind kind) {
  const DieDefinition *def = prv_die_def_at_index(kind);
  return def ? def->roll_sides : 0;
}

bool model_kind_zero_based(DiceKind kind) {
  const DieDefinition *def = prv_die_def_at_index(kind);
  return def ? def->zero_based : false;
}

bool model_kind_tens_mode(DiceKind kind) {
  const DieDefinition *def = prv_die_def_at_index(kind);
  return def ? def->tens_mode : false;
}
