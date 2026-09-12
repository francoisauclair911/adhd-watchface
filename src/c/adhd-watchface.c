#include <pebble.h>

#define KEY_API_TEXT 0
#define KEY_FETCH 1

#define API_TEXT_MAX 120

static Window *s_window;
static TextLayer *s_api_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_battery_layer;
static Layer *s_battery_icon_layer;

static GFont s_time_font;

static char s_api_text[API_TEXT_MAX + 1];
static char s_time_buffer[8];
static char s_date_buffer[16];
static int s_battery_percent = 100;
static bool s_battery_charging;

static const char *const s_weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const s_months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour12 = t->tm_hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  snprintf(s_time_buffer, sizeof(s_time_buffer), "%d:%02d", hour12, t->tm_min);
  text_layer_set_text(s_time_layer, s_time_buffer);

  snprintf(s_date_buffer, sizeof(s_date_buffer), "%s %s %d",
           s_weekdays[t->tm_wday], s_months[t->tm_mon], t->tm_mday);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void battery_icon_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // case outline + terminal nub
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_round_rect(ctx, GRect(0, 0, bounds.size.w - 4, bounds.size.h), 3);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(bounds.size.w - 3, 4, 3, bounds.size.h - 8), 0, GCornerNone);

  // charge level fill: yellow while charging, red when low, white otherwise
  GColor fill = GColorWhite;
  if (s_battery_charging) {
    fill = GColorChromeYellow;
  } else if (s_battery_percent <= 20) {
    fill = GColorRed;
  }
  graphics_context_set_fill_color(ctx, fill);
  const int inner_w = (bounds.size.w - 8) * s_battery_percent / 100;
  if (inner_w > 0) {
    graphics_fill_rect(ctx, GRect(2, 2, inner_w, bounds.size.h - 4), 0, GCornerNone);
  }
}

static void battery_update(BatteryChargeState state) {
  s_battery_percent = state.charge_percent;
  s_battery_charging = state.is_charging;

  static char s_battery_buffer[8];
  snprintf(s_battery_buffer, sizeof(s_battery_buffer), "%d%%", s_battery_percent);
  text_layer_set_text(s_battery_layer, s_battery_buffer);

  layer_mark_dirty(s_battery_icon_layer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, KEY_API_TEXT);
  if (t && t->type == TUPLE_CSTRING) {
    strncpy(s_api_text, t->value->cstring, API_TEXT_MAX);
    s_api_text[API_TEXT_MAX] = '\0';
    text_layer_set_text(s_api_layer, s_api_text);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const int w = bounds.size.w;
  const int h = bounds.size.h;

  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BEBAS_88));

  // API text line: top, up to two lines, one accent color
  s_api_layer = text_layer_create(GRect(8, 8, w - 16, 56));
  text_layer_set_background_color(s_api_layer, GColorClear);
  text_layer_set_text_color(s_api_layer, GColorChromeYellow);
  text_layer_set_font(s_api_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_api_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_api_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_api_layer, "...");
  layer_add_child(window_layer, text_layer_get_layer(s_api_layer));

  // huge time: center
  s_time_layer = text_layer_create(GRect(0, 64, w, 104));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text(s_time_layer, "0:00");
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  // date: below time
  s_date_layer = text_layer_create(GRect(0, h - 62, w, 30));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorLightGray);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  text_layer_set_text(s_date_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  // battery: bottom, drawn icon + percentage
  s_battery_layer = text_layer_create(GRect(w / 2 - 2, h - 36, 48, 26));
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentLeft);
  text_layer_set_text(s_battery_layer, "100%");
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));

  s_battery_icon_layer = layer_create(GRect(w / 2 - 36, h - 26, 32, 14));
  layer_set_update_proc(s_battery_icon_layer, battery_icon_update_proc);
  layer_add_child(window_layer, s_battery_icon_layer);

  battery_update(battery_state_service_peek());
  battery_state_service_subscribe(battery_update);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  update_time();
}

static void prv_window_unload(Window *window) {
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();

  layer_destroy(s_battery_icon_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_api_layer);
  fonts_unload_custom_font(s_time_font);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
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
