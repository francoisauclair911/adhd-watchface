#include <pebble.h>
#include <message_keys.auto.h>

#define API_TEXT_MAX 120

// Aura Essential layout for the Pebble Time 2 (200x228):
//   top color block (API text) / black sep / white band (clock) / black sep / color strip
#define TOP_BLOCK_H      76                                     // smaller top block
#define SEP_H            6
#define WHITE_BAND_Y     (TOP_BLOCK_H + SEP_H)                 // 82
#define WHITE_BAND_H     68                                    // spans 82..149
#define BOTTOM_SEP_Y     (WHITE_BAND_Y + WHITE_BAND_H)         // 150
#define STRIP_Y          (BOTTOM_SEP_Y + SEP_H)                // 156
#define STRIP_H          (228 - STRIP_Y)                       // 72

#define THEME_COLOR GColorFromRGB(230, 110, 107)  // aura coral

static const char *const s_weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const s_months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static Window *s_window;
static Layer *s_api_layer;
static Layer *s_white_band_layer;
static Layer *s_sep_layer;
static TextLayer *s_clock_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_date_layer;

static char s_api_text[API_TEXT_MAX + 1] = "laboriosam mollitia et enim quasi adipisci quia provident illum";
static char s_time_buffer[8];
static char s_battery_buffer[8];
static char s_date_buffer[16];
static int s_battery_percent = 100;

static void white_band_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void sep_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, SEP_H), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, SEP_H + WHITE_BAND_H, bounds.size.w, SEP_H), 0, GCornerNone);
}

// API text: white with a 1px black outline (matching the icon style), vertically centered.
static void api_text_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const GRect measure_box = GRect(0, 0, bounds.size.w, bounds.size.h);
  const GSize used = graphics_text_layout_get_content_size(
      s_api_text, font, measure_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);

  const int16_t top = (bounds.size.h - used.h < 0) ? 0 : (bounds.size.h - used.h) / 2;

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(0, top - 1, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(0, top + 1, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(-1, top, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(1, top, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(0, top, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour12 = t->tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  snprintf(s_time_buffer, sizeof(s_time_buffer), "%d:%02d", hour12, t->tm_min);
  text_layer_set_text(s_clock_layer, s_time_buffer);

  snprintf(s_date_buffer, sizeof(s_date_buffer), "%s %s %d",
           s_weekdays[t->tm_wday], s_months[t->tm_mon], t->tm_mday);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void battery_update(BatteryChargeState state) {
  s_battery_percent = state.charge_percent;
  snprintf(s_battery_buffer, sizeof(s_battery_buffer), "%d%%", s_battery_percent);
  text_layer_set_text(s_battery_layer, s_battery_buffer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_apiText);
  if (t && t->type == TUPLE_CSTRING) {
    strncpy(s_api_text, t->value->cstring, API_TEXT_MAX);
    s_api_text[API_TEXT_MAX] = '\0';
    layer_mark_dirty(s_api_layer);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);
  const int w = bounds.size.w;

  // top color block (window background) + API text, outlined in black
  s_api_layer = layer_create(GRect(12, 6, w - 24, TOP_BLOCK_H - 12));
  layer_set_update_proc(s_api_layer, api_text_update_proc);
  layer_add_child(window_layer, s_api_layer);

  // black separators: one layer draws both the top and bottom separator line
  s_sep_layer = layer_create(GRect(0, TOP_BLOCK_H, w, STRIP_Y - TOP_BLOCK_H));
  layer_set_update_proc(s_sep_layer, sep_update_proc);
  layer_add_child(window_layer, s_sep_layer);

  // white band
  s_white_band_layer = layer_create(GRect(0, WHITE_BAND_Y, w, WHITE_BAND_H));
  layer_set_update_proc(s_white_band_layer, white_band_update_proc);
  layer_add_child(window_layer, s_white_band_layer);

  // segmented clock in the white band, in the theme color
  s_clock_layer = text_layer_create(GRect(0, WHITE_BAND_Y - 4, w, WHITE_BAND_H + 8));
  text_layer_set_background_color(s_clock_layer, GColorClear);
  text_layer_set_text_color(s_clock_layer, THEME_COLOR);
  text_layer_set_font(s_clock_layer,
                      fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM));
  text_layer_set_text_alignment(s_clock_layer, GTextAlignmentCenter);
  text_layer_set_text(s_clock_layer, "0:00");
  layer_add_child(window_layer, text_layer_get_layer(s_clock_layer));

  // battery indicator: bottom right of color strip, white text
  s_battery_layer = text_layer_create(GRect(w - 60, STRIP_Y, 58, STRIP_H));
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
  text_layer_set_text(s_battery_layer, "100%");
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));

  // date indicator: bottom left of color strip, white text
  s_date_layer = text_layer_create(GRect(8, STRIP_Y, 110, STRIP_H));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentLeft);
  text_layer_set_text(s_date_layer, "SUN JAN 1");
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  battery_state_service_subscribe(battery_update);
  battery_update(battery_state_service_peek());

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  update_time();
}

static void prv_window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();

  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_clock_layer);
  layer_destroy(s_white_band_layer);
  layer_destroy(s_sep_layer);
  layer_destroy(s_api_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, THEME_COLOR);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_window_load,
                                           .unload = prv_window_unload,
                                       });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), 64);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}