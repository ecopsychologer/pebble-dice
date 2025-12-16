#include "model.h"

#include <string.h>

typedef struct {
  int sides;
  const char *label;
} DieDefinition;

static const DieDefinition s_die_defs[] = {
  {4, "d4"},
  {6, "d6"},
  {8, "d8"},
  {10, "d10"},
  {12, "d12"},
  {20, "d20"},
  {100, "d100"},
  {100, "d%"},
};
static const int s_die_defs_count = ARRAY_LENGTH(s_die_defs);

static void prv_clamp_selection(DiceModel *model) {
  if (model->selected_die_index < 0) {
    model->selected_die_index = 0;
  } else if (model->selected_die_index >= s_die_defs_count) {
    model->selected_die_index = s_die_defs_count - 1;
  }

  if (model->selected_count < 1) {
    model->selected_count = 1;
  } else if (model->selected_count > MAX_DICE_PER_GROUP) {
    model->selected_count = MAX_DICE_PER_GROUP;
  }
}

void model_init(DiceModel *model) {
  memset(model, 0, sizeof(*model));
  model->selected_die_index = DICE_KIND_D6;
  model->selected_count = 1;
}

int model_increment_selected_die(DiceModel *model, int delta) {
  model->selected_die_index = (model->selected_die_index + s_die_defs_count + delta) % s_die_defs_count;
  return s_die_defs[model->selected_die_index].sides;
}

int model_increment_selected_count(DiceModel *model, int delta) {
  model->selected_count += delta;
  prv_clamp_selection(model);
  return model->selected_count;
}

int model_get_selected_sides(const DiceModel *model) {
  return s_die_defs[model->selected_die_index].sides;
}

int model_get_selected_count(const DiceModel *model) {
  return model->selected_count;
}

const char *model_get_selected_label(const DiceModel *model) {
  if (!model) {
    return "";
  }
  if (model->selected_die_index < 0 || model->selected_die_index >= s_die_defs_count) {
    return "d?";
  }
  return s_die_defs[model->selected_die_index].label;
}

int model_get_selected_die_index(const DiceModel *model) {
  if (!model) {
    return 0;
  }
  return model->selected_die_index;
}

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
  if (group->die_def_index < 0 || group->die_def_index >= s_die_defs_count) {
    return "d?";
  }
  return s_die_defs[group->die_def_index].label;
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
