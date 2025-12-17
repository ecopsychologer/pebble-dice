#include "ui.h"

#include <pebble.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// UI MODULE
// -----------------------------------------------------------------------------
// Renders text, dice icons, and the scrollable slot grid. All layout constants
// live near the top so you can safely tweak them without digging. The module
// never deals with button logic—that comes from state.c via UiRenderData.
//
// Safe tweaks:
// - Adjust `#define`d measurements to move layers around.
// - Update slot colors or fonts inside the helper functions below.
// - Replace `prv_draw_group_icons/prv_format_slot_value` when adding richer UI.

#define SLOT_COLUMNS 3
#define SLOT_SPACING 4
#define SLOT_CORNER_RADIUS 3
#define SLOT_HEIGHT 34
#define BUTTON_HINT_WIDTH 32
#define BUTTON_HINT_MARGIN 2
#define ICON_MAX_SIZE 48
#define ICON_MIN_SIZE 18
#define TITLE_TOP 2
#define TITLE_HEIGHT 20
#define SUMMARY_TOP (TITLE_TOP + TITLE_HEIGHT)
#define SUMMARY_HEIGHT 32
#define SUMMARY_BOTTOM (SUMMARY_TOP + SUMMARY_HEIGHT)
#define PICKER_ICON_TOP (SUMMARY_BOTTOM + 6)
#define PICKER_ICON_SIZE 56
#define MAIN_LAYER_TOP (PICKER_ICON_TOP + PICKER_ICON_SIZE + 6)
#define SLOTS_LAYER_TOP (MAIN_LAYER_TOP + 48)
#define SLOTS_TOP_WIDE SLOTS_LAYER_TOP
#define SLOTS_TOP_COMPACT (SUMMARY_BOTTOM + 4)

#ifndef CLAMP
#define CLAMP(value, min_value, max_value) ((value) < (min_value) ? (min_value) : ((value) > (max_value) ? (max_value) : (value)))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// ----- Layer handles -----
static TextLayer *s_title_layer;
static TextLayer *s_summary_layer;
static TextLayer *s_main_layer;
static BitmapLayer *s_picker_icon_layer;
static Layer *s_slots_layer;
static Layer *s_hint_layer;

static char s_title_buffer[32];
static char s_summary_buffer[64];
static char s_main_buffer[48];
static char s_hint_top_text[UI_HINT_TEXT_LENGTH];
static char s_hint_middle_text[UI_HINT_TEXT_LENGTH];
static char s_hint_bottom_text[UI_HINT_TEXT_LENGTH];

static UiRenderData s_active_view;
static const DiceModel *s_active_model;
static int16_t s_content_width;
static int16_t s_slots_view_height;
static int16_t s_scroll_offset;
static int16_t s_scroll_content_height;
static GRect s_root_bounds;
static AppState s_last_state = PICK_DIE;

static void prv_set_slots_frame(int16_t top_offset);


static GBitmap *s_die_bitmaps[DICE_KIND_COUNT];
static const uint32_t s_die_bitmap_ids[DICE_KIND_COUNT] = {
  [DICE_KIND_D4] = RESOURCE_ID_IMAGE_D4,
  [DICE_KIND_D6] = RESOURCE_ID_IMAGE_D6,
  [DICE_KIND_D8] = RESOURCE_ID_IMAGE_D8,
  [DICE_KIND_D10] = RESOURCE_ID_IMAGE_D10,
  [DICE_KIND_D12] = RESOURCE_ID_IMAGE_D12,
  [DICE_KIND_D20] = RESOURCE_ID_IMAGE_D20,
  [DICE_KIND_D100] = RESOURCE_ID_IMAGE_D100,
  [DICE_KIND_PERCENTILE] = RESOURCE_ID_IMAGE_D10,
};

static void prv_configure_text_layer(TextLayer *layer, GTextAlignment alignment, const char *font_key) {
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorBlack);
  text_layer_set_text_alignment(layer, alignment);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_overflow_mode(layer, GTextOverflowModeWordWrap);
}

// Format helpers keep summary/picker UI logic lightweight and in one place.
static void prv_format_group_line(const DiceModel *model, char *buffer, size_t size) {
  buffer[0] = '\0';
  const int group_total = model_group_count(model);
  if (group_total == 0) {
    snprintf(buffer, size, "%d%s", model_get_selected_count(model), model_get_selected_label(model));
    return;
  }

  size_t used = 0;
  for (int i = 0; i < group_total; ++i) {
    const DiceGroup *group = model_get_group(model, i);
    if (!group) {
      continue;
    }
    const char *prefix = (used == 0) ? "" : ", ";
    used += snprintf(buffer + used, (used < size) ? size - used : 0, "%s%dd%d", prefix, group->count, group->sides);
    if (used >= size) {
      break;
    }
  }
}

static void prv_build_summary_text(const DiceModel *model, char *buffer, size_t size) {
  char line[48];
  prv_format_group_line(model, line, sizeof(line));
  if (model_group_count(model) == 0) {
    snprintf(buffer, size, "Next: %s", line);
  } else {
    snprintf(buffer, size, "Dice: %s", line);
  }
}

static GBitmap *prv_get_die_bitmap(DiceKind kind) {
  if (kind >= DICE_KIND_COUNT) {
    return NULL;
  }
  if (!s_die_bitmaps[kind]) {
    const uint32_t resource_id = s_die_bitmap_ids[kind];
    if (!resource_id) {
      return NULL;
    }
    s_die_bitmaps[kind] = gbitmap_create_with_resource(resource_id);
  }
  return s_die_bitmaps[kind];
}

// Toggle the picker icon layer. This keeps the “artful” picker contained so it
// never interferes with result rendering.
static void prv_update_picker_icon(bool show, DiceKind kind) {
  if (!s_picker_icon_layer) {
    return;
  }
  Layer *layer = bitmap_layer_get_layer(s_picker_icon_layer);
  if (!show) {
    layer_set_hidden(layer, true);
    return;
  }
  GBitmap *bitmap = prv_get_die_bitmap(kind);
  if (!bitmap) {
    layer_set_hidden(layer, true);
    return;
  }
  bitmap_layer_set_bitmap(s_picker_icon_layer, bitmap);
  layer_set_hidden(layer, false);
}

// ----- Button hint rendering ------------------------------------------------
static void prv_draw_hint_box(GContext *ctx, GRect rect, const char *text) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, rect, 2);
  graphics_draw_text(ctx,
                     text,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     rect,
                     GTextOverflowModeWordWrap,
                     GTextAlignmentCenter,
                     NULL);
}

static void prv_hint_layer_update(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const int box_height = (bounds.size.h - BUTTON_HINT_MARGIN * 4) / 3;

  GRect top = GRect(BUTTON_HINT_MARGIN,
                    BUTTON_HINT_MARGIN,
                    bounds.size.w - BUTTON_HINT_MARGIN * 2,
                    box_height);
  GRect middle = GRect(BUTTON_HINT_MARGIN,
                       top.origin.y + box_height + BUTTON_HINT_MARGIN,
                       bounds.size.w - BUTTON_HINT_MARGIN * 2,
                       box_height);
  GRect bottom = GRect(BUTTON_HINT_MARGIN,
                       middle.origin.y + box_height + BUTTON_HINT_MARGIN,
                       bounds.size.w - BUTTON_HINT_MARGIN * 2,
                       box_height);

  prv_draw_hint_box(ctx, top, s_hint_top_text);
  prv_draw_hint_box(ctx, middle, s_hint_middle_text);
  prv_draw_hint_box(ctx, bottom, s_hint_bottom_text);
}

static GColor prv_color_pending(void) {
  return PBL_IF_COLOR_ELSE(GColorImperialPurple, GColorBlack);
}

static GColor prv_color_done(void) {
  return PBL_IF_COLOR_ELSE(GColorImperialPurple, GColorBlack);
}

static GColor prv_color_anim_text(int progress_per_mille) {
#if PBL_COLOR
  if (progress_per_mille < 350) {
    return GColorRed;
  } else if (progress_per_mille < 700) {
    return GColorChromeYellow;
  }
  return GColorPastelYellow;
#else
  return (progress_per_mille < 700) ? GColorWhite : GColorBlack;
#endif
}

static GColor prv_color_done_text(void) {
  return PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite);
}

// Converts raw result integers into human-readable slot labels.
static void prv_format_slot_value(const DiceGroup *group, int value, char *buffer, size_t size) {
  if (!group || value < 0) {
    snprintf(buffer, size, "?");
    return;
  }
  const DiceKind kind = (DiceKind)group->die_def_index;
  if (model_kind_zero_based(kind)) {
    snprintf(buffer, size, "%02d", value);
  } else if (value <= 0) {
    snprintf(buffer, size, "-");
  } else {
    snprintf(buffer, size, "%d", value);
  }
}

static void prv_draw_slot(GContext *ctx, GRect rect, const char *text, GColor fill, GColor text_color) {
  const int radius = SLOT_CORNER_RADIUS;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, rect, radius, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, rect, radius);

  GRect text_rect = GRect(rect.origin.x + 2, rect.origin.y + 2, rect.size.w - 4, rect.size.h - 4);
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx,
                     text,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     text_rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter,
                     NULL);
}

static int prv_group_high(const DiceGroup *group) {
  int high = 0;
  for (int i = 0; i < group->count; ++i) {
    if (group->results[i] > high) {
      high = group->results[i];
    }
  }
  return high;
}

static int prv_group_total(const DiceGroup *group) {
  int total = 0;
  for (int i = 0; i < group->count; ++i) {
    total += group->results[i];
  }
  return total;
}

static void prv_draw_bitmap_centered(GContext *ctx, GBitmap *bitmap, GRect rect) {
  if (!bitmap) {
    return;
  }
  const GSize bmp_size = gbitmap_get_bounds(bitmap).size;
  GRect target = rect;
  if (bmp_size.w < rect.size.w) {
    target.origin.x += (rect.size.w - bmp_size.w) / 2;
    target.size.w = bmp_size.w;
  }
  if (bmp_size.h < rect.size.h) {
    target.origin.y += (rect.size.h - bmp_size.h) / 2;
    target.size.h = bmp_size.h;
  }
  graphics_draw_bitmap_in_rect(ctx, bitmap, target);
}

static int prv_draw_group_icons(GContext *ctx, const DiceGroup *group, int y_start, int width) {
  if (!group) {
    return y_start;
  }
  const int dice = group->count;
  if (dice <= 0) {
    return y_start;
  }
  const int columns = dice < SLOT_COLUMNS ? dice : SLOT_COLUMNS;
  const int icon_width = (width - (columns + 1) * SLOT_SPACING) / columns;
  const int size = CLAMP(icon_width, ICON_MIN_SIZE, ICON_MAX_SIZE);
  int y = y_start;

  for (int d = 0; d < dice; ++d) {
    const int column = d % columns;
    const int row = d / columns;
    const int slot_x = SLOT_SPACING + column * (size + SLOT_SPACING);
    const int slot_y = y + row * (size + SLOT_SPACING);
    GRect slot_rect = GRect(slot_x, slot_y, size, size);
    GBitmap *bmp = prv_get_die_bitmap(group->die_def_index);
    prv_draw_bitmap_centered(ctx, bmp, slot_rect);
  }

  const int rows = (dice + columns - 1) / columns;
  return y + rows * (size + SLOT_SPACING) + SLOT_SPACING;
}

static void prv_draw_result_slots(GContext *ctx, const DiceGroup *group, int g_index, int *y_ref, int width) {
  if (!group) {
    return;
  }

  int y = *y_ref;

  char label[48];
  if (group->count > 3) {
    const int high = prv_group_high(group);
    const int total = prv_group_total(group);
    snprintf(label, sizeof(label), "%d%s | H:%d | T:%d", group->count, model_group_label(group), high, total);
  } else {
    snprintf(label, sizeof(label), "%d%s", group->count, model_group_label(group));
  }

  GRect label_rect = GRect(SLOT_SPACING, y, width - SLOT_SPACING * 2, 18);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx,
                     label,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     label_rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft,
                     NULL);
  y += 18 + SLOT_SPACING;

  if (group->count <= 0) {
    *y_ref = y;
    return;
  }

  const int columns = (group->count < SLOT_COLUMNS) ? group->count : SLOT_COLUMNS;
  const int column_width = (width - ((columns + 1) * SLOT_SPACING)) / columns;

  for (int d = 0; d < group->count; ++d) {
    const int column = d % columns;
    const int row = d / columns;
    const int slot_x = SLOT_SPACING + column * (column_width + SLOT_SPACING);
    const int slot_y = y + row * (SLOT_HEIGHT + SLOT_SPACING);
    GRect slot_rect = GRect(slot_x, slot_y, column_width, SLOT_HEIGHT);

    const bool is_done = (s_active_view.state == RESULTS) ||
                         (g_index < s_active_model->roll_group_index) ||
                         (g_index == s_active_model->roll_group_index && d < s_active_model->roll_die_index);
    const bool is_current = (s_active_view.state == ROLLING) &&
                            model_has_roll_remaining(s_active_model) &&
                            (g_index == s_active_model->roll_group_index && d == s_active_model->roll_die_index);

    GColor fill = prv_color_pending();
    GColor text_color = GColorWhite;
    char value[8];
    snprintf(value, sizeof(value), "?");

    if (is_done) {
      fill = prv_color_done();
      text_color = prv_color_done_text();
      const int result_value = group->results[d];
      prv_format_slot_value(group, result_value, value, sizeof(value));
    } else if (is_current) {
      fill = prv_color_pending();
      text_color = prv_color_anim_text(s_active_view.anim_progress_per_mille);
      if (s_active_view.rolling_value >= 0) {
        prv_format_slot_value(group, s_active_view.rolling_value, value, sizeof(value));
      }
    }

    prv_draw_slot(ctx, slot_rect, value, fill, text_color);
  }

  const int rows = (group->count + columns - 1) / columns;
  y += rows * (SLOT_HEIGHT + SLOT_SPACING) + SLOT_SPACING;
  *y_ref = y;
}

static void prv_slots_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  if (!s_active_model) {
    return;
  }

  const int width = layer_get_bounds(layer).size.w;
  int y = SLOT_SPACING - s_scroll_offset;

  if (s_active_view.state == ADD_GROUP_PROMPT) {
    for (int g = 0; g < model_group_count(s_active_model); ++g) {
      const DiceGroup *group = model_get_group(s_active_model, g);
      if (!group) {
        continue;
      }
      char label[32];
      snprintf(label, sizeof(label), "%d%s", group->count), model_group_label(group);
      GRect label_rect = GRect(SLOT_SPACING, y, width - SLOT_SPACING * 2, 18);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx,
                         label,
                         fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                         label_rect,
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft,
                         NULL);
      y += 18 + SLOT_SPACING;
      y = prv_draw_group_icons(ctx, group, y, width);
    }
  } else if (s_active_view.state == ROLLING || s_active_view.state == RESULTS) {
    for (int g = 0; g < model_group_count(s_active_model); ++g) {
      const DiceGroup *group = model_get_group(s_active_model, g);
      prv_draw_result_slots(ctx, group, g, &y, width);
    }
  }

  s_scroll_content_height = y + s_scroll_offset;
  if (s_scroll_content_height < layer_get_bounds(layer).size.h) {
    s_scroll_content_height = layer_get_bounds(layer).size.h;
  }
}

static void prv_render_pick_die(const DiceModel *model) {
  snprintf(s_title_buffer, sizeof(s_title_buffer), "Pick Die");
  snprintf(s_main_buffer, sizeof(s_main_buffer), "%s", model_get_selected_label(model));
}

static void prv_render_pick_count(const DiceModel *model) {
  snprintf(s_title_buffer, sizeof(s_title_buffer), "How Many");
  snprintf(s_main_buffer, sizeof(s_main_buffer), "x%d", model_get_selected_count(model));
}

static void prv_render_add_prompt(const DiceModel *model, const UiRenderData *data) {
  if (data->confirm_clear_prompt) {
    snprintf(s_title_buffer, sizeof(s_title_buffer), "Clear dice?");
  } else {
    s_title_buffer[0] = '\0';
  }
  s_main_buffer[0] = '\0';
}

static void prv_render_rolling(const DiceModel *model, const UiRenderData *data) {
  snprintf(s_title_buffer, sizeof(s_title_buffer), "Rolling");
  s_main_buffer[0] = '\0';
}

static void prv_render_results(const DiceModel *model, const UiRenderData *data) {
  snprintf(s_title_buffer, sizeof(s_title_buffer), "Results");
  s_main_buffer[0] = '\0';
}

static void prv_toggle_slots_visibility(bool show_slots) {
  if (s_slots_layer) {
    layer_set_hidden(s_slots_layer, !show_slots);
  }
}

void ui_init(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_root_bounds = layer_get_bounds(root);

  s_content_width = s_root_bounds.size.w - BUTTON_HINT_WIDTH;
  s_slots_view_height = s_root_bounds.size.h - SLOTS_LAYER_TOP;
  if (s_slots_view_height < 0) {
    s_slots_view_height = 0;
  }

  s_title_layer = text_layer_create(GRect(4, TITLE_TOP, s_content_width - 8, TITLE_HEIGHT));
  s_summary_layer = text_layer_create(GRect(4, SUMMARY_TOP, s_content_width - 8, SUMMARY_HEIGHT));
  s_picker_icon_layer = bitmap_layer_create(GRect((s_content_width - PICKER_ICON_SIZE) / 2,
                                                  PICKER_ICON_TOP,
                                                  PICKER_ICON_SIZE,
                                                  PICKER_ICON_SIZE));
  s_main_layer = text_layer_create(GRect(0, MAIN_LAYER_TOP, s_content_width, 42));
  s_slots_layer = layer_create(GRect(0, SLOTS_TOP_WIDE, s_content_width, s_slots_view_height));
  s_hint_layer = layer_create(GRect(s_content_width, 0, BUTTON_HINT_WIDTH, s_root_bounds.size.h));

  prv_configure_text_layer(s_title_layer, GTextAlignmentLeft, FONT_KEY_GOTHIC_18_BOLD);
  prv_configure_text_layer(s_summary_layer, GTextAlignmentLeft, FONT_KEY_GOTHIC_14);
  prv_configure_text_layer(s_main_layer, GTextAlignmentCenter, FONT_KEY_GOTHIC_28_BOLD);
  text_layer_set_overflow_mode(s_summary_layer, GTextOverflowModeTrailingEllipsis);
  bitmap_layer_set_background_color(s_picker_icon_layer, GColorClear);
  bitmap_layer_set_compositing_mode(s_picker_icon_layer, GCompOpSet);

  layer_set_update_proc(s_slots_layer, prv_slots_update_proc);
  layer_set_update_proc(s_hint_layer, prv_hint_layer_update);

  layer_add_child(root, text_layer_get_layer(s_title_layer));
  layer_add_child(root, text_layer_get_layer(s_summary_layer));
  layer_add_child(root, bitmap_layer_get_layer(s_picker_icon_layer));
  layer_add_child(root, text_layer_get_layer(s_main_layer));
  layer_add_child(root, s_slots_layer);
  layer_add_child(root, s_hint_layer);

  layer_set_hidden(s_slots_layer, true);

  for (int i = 0; i < DICE_KIND_COUNT; ++i) {
    s_die_bitmaps[i] = NULL;
  }
}

void ui_deinit(void) {
  for (int i = 0; i < DICE_KIND_COUNT; ++i) {
    if (s_die_bitmaps[i]) {
      gbitmap_destroy(s_die_bitmaps[i]);
      s_die_bitmaps[i] = NULL;
    }
  }

  if (s_hint_layer) {
    layer_destroy(s_hint_layer);
    s_hint_layer = NULL;
  }
  if (s_slots_layer) {
    layer_destroy(s_slots_layer);
    s_slots_layer = NULL;
  }
  if (s_picker_icon_layer) {
    bitmap_layer_destroy(s_picker_icon_layer);
    s_picker_icon_layer = NULL;
  }
  if (s_main_layer) {
    text_layer_destroy(s_main_layer);
    s_main_layer = NULL;
  }
  if (s_summary_layer) {
    text_layer_destroy(s_summary_layer);
    s_summary_layer = NULL;
  }
  if (s_title_layer) {
    text_layer_destroy(s_title_layer);
    s_title_layer = NULL;
  }
}

void ui_scroll_reset(void) {
  s_scroll_offset = 0;
  if (s_slots_layer) {
    layer_mark_dirty(s_slots_layer);
  }
}

bool ui_scroll_step(int direction) {
  if (!s_slots_layer || (s_active_view.state != ROLLING && s_active_view.state != RESULTS)) {
    return false;
  }

  const int max_offset = s_scroll_content_height - s_slots_view_height;
  if (max_offset <= 0) {
    return false;
  }

  if (direction > 0) {
    if (s_scroll_offset >= max_offset) {
      s_scroll_offset = 0;
    } else {
      s_scroll_offset = MIN(max_offset, s_scroll_offset + SLOT_HEIGHT + SLOT_SPACING);
    }
  } else {
    if (s_scroll_offset <= 0) {
      s_scroll_offset = max_offset;
    } else {
      s_scroll_offset = MAX(0, s_scroll_offset - (SLOT_HEIGHT + SLOT_SPACING));
    }
  }
  layer_mark_dirty(s_slots_layer);
  return true;
}

// Main render entry point. State machine passes render data, UI decides which
// layers are visible and which buffers to populate.
void ui_render(const UiRenderData *data, const DiceModel *model) {
  if (!data || !model || !s_title_layer) {
    return;
  }

  if (data->state != s_last_state) {
    ui_scroll_reset();
    s_last_state = data->state;
  }

  s_active_view = *data;
  s_active_model = model;

  prv_build_summary_text(model, s_summary_buffer, sizeof(s_summary_buffer));
  text_layer_set_text(s_summary_layer, s_summary_buffer);

  bool show_main_text = true;
  bool show_picker_icon = false;
  int16_t slots_top = SLOTS_TOP_WIDE;

  switch (data->state) {
    case PICK_DIE:
      prv_toggle_slots_visibility(false);
      prv_render_pick_die(model);
      show_main_text = true;
      show_picker_icon = true;
      break;
    case PICK_COUNT:
      prv_toggle_slots_visibility(false);
      prv_render_pick_count(model);
      show_main_text = true;
      break;
    case ADD_GROUP_PROMPT:
      prv_toggle_slots_visibility(true);
      prv_render_add_prompt(model, data);
      show_main_text = false;
      slots_top = SLOTS_TOP_COMPACT;
      break;
    case ROLLING:
      prv_toggle_slots_visibility(true);
      prv_render_rolling(model, data);
      show_main_text = false;
      slots_top = SLOTS_TOP_COMPACT;
      break;
    case RESULTS:
      prv_toggle_slots_visibility(true);
      prv_render_results(model, data);
      show_main_text = false;
      slots_top = SLOTS_TOP_COMPACT;
      break;
  }

  const DiceKind selected_kind = (DiceKind)model_get_selected_die_index(model);
  prv_update_picker_icon(show_picker_icon, selected_kind);
  layer_set_hidden(text_layer_get_layer(s_main_layer), !show_main_text);

  text_layer_set_text(s_title_layer, s_title_buffer);
  text_layer_set_text(s_main_layer, s_main_buffer);
  prv_set_slots_frame(slots_top);

  strncpy(s_hint_top_text, data->hint_top, sizeof(s_hint_top_text));
  s_hint_top_text[sizeof(s_hint_top_text) - 1] = '\0';
  strncpy(s_hint_middle_text, data->hint_middle, sizeof(s_hint_middle_text));
  s_hint_middle_text[sizeof(s_hint_middle_text) - 1] = '\0';
  strncpy(s_hint_bottom_text, data->hint_bottom, sizeof(s_hint_bottom_text));
  s_hint_bottom_text[sizeof(s_hint_bottom_text) - 1] = '\0';

  layer_mark_dirty(s_hint_layer);
  layer_mark_dirty(s_slots_layer);
}
static void prv_set_slots_frame(int16_t top_offset) {
  if (!s_slots_layer) {
    return;
  }
  if (top_offset < SUMMARY_BOTTOM) {
    top_offset = SUMMARY_BOTTOM;
  }
  const int16_t height = (int16_t)MAX(0, s_root_bounds.size.h - top_offset);
  s_slots_view_height = height;
  layer_set_frame(s_slots_layer, GRect(0, top_offset, s_content_width, height));
}
