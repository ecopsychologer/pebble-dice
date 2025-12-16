#pragma once

#include <pebble.h>

#define MAX_DICE_GROUPS 8
#define MAX_DICE_PER_GROUP 10
#define MAX_RESULTS_PER_GROUP MAX_DICE_PER_GROUP

typedef enum {
  DICE_KIND_D4,
  DICE_KIND_D6,
  DICE_KIND_D8,
  DICE_KIND_D10,
  DICE_KIND_D12,
  DICE_KIND_D20,
  DICE_KIND_D100,
  DICE_KIND_PERCENTILE,
  DICE_KIND_COUNT
} DiceKind;

typedef struct {
  int sides;
  int count;
  int results[MAX_RESULTS_PER_GROUP];
  int die_def_index;
} DiceGroup;

typedef struct {
  DiceGroup groups[MAX_DICE_GROUPS];
  int group_count;

  int selected_die_index;
  int selected_count;

  int roll_group_index;
  int roll_die_index;
} DiceModel;

void model_init(DiceModel *model);

int model_increment_selected_die(DiceModel *model, int delta);
int model_increment_selected_count(DiceModel *model, int delta);
int model_get_selected_sides(const DiceModel *model);
int model_get_selected_count(const DiceModel *model);
const char *model_get_selected_label(const DiceModel *model);
int model_get_selected_die_index(const DiceModel *model);

bool model_commit_group(DiceModel *model);
void model_clear_groups(DiceModel *model);
bool model_has_groups(const DiceModel *model);

void model_begin_roll(DiceModel *model);
bool model_has_roll_remaining(const DiceModel *model);
int model_current_roll_sides(const DiceModel *model);
void model_commit_roll_result(DiceModel *model, int value);
int model_roll_completed_dice(const DiceModel *model);
int model_roll_total_dice(const DiceModel *model);

int model_group_count(const DiceModel *model);
const DiceGroup *model_get_group(const DiceModel *model, int index);
const char *model_group_label(const DiceGroup *group);
int model_group_sides(const DiceGroup *group);

void model_reset_selection_count(DiceModel *model);
const char *model_current_roll_label(const DiceModel *model);
