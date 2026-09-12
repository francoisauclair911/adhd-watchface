#include <pebble.h>
#include <message_keys.auto.h>

#define API_TEXT_MAX 120

#define TOP_BLOCK_H      104
#define SEP_H            6
#define WHITE_BAND_Y     (TOP_BLOCK_H + SEP_H)
#define WHITE_BAND_H     68
#define BOTTOM_SEP_Y     (WHITE_BAND_Y + WHITE_BAND_H)
#define STRIP_Y          (BOTTOM_SEP_Y + SEP_H)
#define STRIP_H          (228 - STRIP_Y)

#define THEME_COLOR GColorFromRGB(230, 110, 107)

#define PERSIST_KEY_API_TEXT   1
#define DOUBLE_TAP_WINDOW_MS   500
#define STATUS_TIMEOUT_MS      10000

static const char *const s_weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const s_months[]   = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static Window    *s_window;
static Layer     *s_api_layer;
static Layer     *s_white_band_layer;
static Layer     *s_sep_layer;
static TextLayer *s_clock_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_date_layer;

static char s_api_text[API_TEXT_MAX + 1] = "";
static char s_time_buffer[8];
static char s_battery_buffer[8];
static char s_date_buffer[16];
static int  s_battery_percent = 100;

static uint32_t    s_first_tap_ms  = 0;
static uint32_t    s_last_touch_ms = 0;
static AppTimer   *s_tap_timer     = NULL;
static AppTimer   *s_status_timer  = NULL;
static char        s_prev_text[API_TEXT_MAX + 1] = "";

// ── helpers ──────────────────────────────────────────────────────────────────

#define TOUCH_SUPPRESS_MS 800

static uint32_t now_ms(void) {
  time_t   secs = 0;
  uint16_t ms   = 0;
  time_ms(&secs, &ms);
  return (uint32_t)secs * 1000u + ms;
}

static void set_status_text(const char *text) {
  strncpy(s_api_text, text, API_TEXT_MAX);
  s_api_text[API_TEXT_MAX] = '\0';
  layer_mark_dirty(s_api_layer);
}

// Watchdog: if the phone/JS side never answers after a tap shows a transient
// status ("..."/"done!"), restore the last good text so the display can't get
// stuck. Runs STATUS_TIMEOUT_MS after the status was shown.
static void status_timeout(void *context) {
  s_status_timer = NULL;
  APP_LOG(APP_LOG_LEVEL_WARNING, "status timeout — no reply from JS");
  set_status_text(s_prev_text[0] ? s_prev_text : "offline");
}

static void show_status(const char *text) {
  strncpy(s_prev_text, s_api_text, API_TEXT_MAX);
  s_prev_text[API_TEXT_MAX] = '\0';
  set_status_text(text);
  if (s_status_timer) { app_timer_cancel(s_status_timer); }
  s_status_timer = app_timer_register(STATUS_TIMEOUT_MS, status_timeout, NULL);
}

static void send_message(uint8_t key, uint8_t value) {
  DictionaryIterator *iter;
  AppMessageResult res = app_message_outbox_begin(&iter);
  if (res != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_begin failed: %d", (int)res);
    return;
  }
  dict_write_uint8(iter, key, value);
  res = app_message_outbox_send();
  if (res != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_send failed: %d", (int)res);
  }
}

// ── draw callbacks ────────────────────────────────────────────────────────────

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

static void api_text_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds    = layer_get_bounds(layer);
  const GFont font      = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  const GRect measure   = GRect(0, 0, bounds.size.w, bounds.size.h);
  GSize used = graphics_text_layout_get_content_size(
      s_api_text, font, measure, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);

  const int16_t max_h = 28 * 3;
  if (used.h > max_h) used.h = max_h;

  const int16_t top = (bounds.size.h - used.h < 0) ? 0 : (bounds.size.h - used.h) / 2;

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_api_text, font,
                     GRect(0, top, bounds.size.w, used.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ── time / battery ────────────────────────────────────────────────────────────

static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour12 = t->tm_hour % 12;
  if (hour12 == 0) { hour12 = 12; }
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

// ── gesture handling ──────────────────────────────────────────────────────────

static void on_single_tap(void *context) {
  s_tap_timer    = NULL;
  s_first_tap_ms = 0;
  show_status("...");
  vibes_short_pulse();
  send_message(MESSAGE_KEY_fetch, 1);
}

static void on_double_tap(void) {
  if (s_tap_timer) {
    app_timer_cancel(s_tap_timer);
    s_tap_timer = NULL;
  }
  s_first_tap_ms = 0;
  show_status("done!");
  vibes_double_pulse();
  send_message(MESSAGE_KEY_completeTask, 1);
}

static void handle_gesture(void) {
  const uint32_t now = now_ms();
  if (s_first_tap_ms != 0 && (now - s_first_tap_ms) < DOUBLE_TAP_WINDOW_MS) {
    on_double_tap();
  } else {
    s_first_tap_ms = now;
    if (s_tap_timer) { app_timer_cancel(s_tap_timer); }
    s_tap_timer = app_timer_register(DOUBLE_TAP_WINDOW_MS, on_single_tap, NULL);
  }
}

// Touch handler (emery touchscreen) — whole screen
static void touch_handler(const TouchEvent *event, void *context) {
  if (event->type != TouchEvent_Touchdown) { return; }
  // non_navigational == unarmed contact on the idle watchface: exactly the taps we want
  if (!event->non_navigational) { return; }
  s_last_touch_ms = now_ms();
  handle_gesture();
}

// Accel tap fallback (emulator + wrist flick on device).
// Suppressed right after a screen tap so one physical tap isn't counted twice.
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  const uint32_t now = now_ms();
  if (s_last_touch_ms != 0 && (now - s_last_touch_ms) < TOUCH_SUPPRESS_MS) { return; }
  handle_gesture();
}

// ── app message ───────────────────────────────────────────────────────────────

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_apiText);
  if (t && t->type == TUPLE_CSTRING) {
    if (s_status_timer) {
      app_timer_cancel(s_status_timer);
      s_status_timer = NULL;
    }
    strncpy(s_api_text, t->value->cstring, API_TEXT_MAX);
    s_api_text[API_TEXT_MAX] = '\0';
    persist_write_string(PERSIST_KEY_API_TEXT, s_api_text);
    layer_mark_dirty(s_api_layer);
    APP_LOG(APP_LOG_LEVEL_INFO, "apiText received (%d chars)", (int)strlen(s_api_text));
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

// ── window ────────────────────────────────────────────────────────────────────

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds  = layer_get_bounds(window_layer);
  const int w         = bounds.size.w;

  s_api_layer = layer_create(GRect(12, 6, w - 24, TOP_BLOCK_H - 12));
  layer_set_update_proc(s_api_layer, api_text_update_proc);
  layer_add_child(window_layer, s_api_layer);

  s_sep_layer = layer_create(GRect(0, TOP_BLOCK_H, w, STRIP_Y - TOP_BLOCK_H));
  layer_set_update_proc(s_sep_layer, sep_update_proc);
  layer_add_child(window_layer, s_sep_layer);

  s_white_band_layer = layer_create(GRect(0, WHITE_BAND_Y, w, WHITE_BAND_H));
  layer_set_update_proc(s_white_band_layer, white_band_update_proc);
  layer_add_child(window_layer, s_white_band_layer);

  s_clock_layer = text_layer_create(GRect(0, WHITE_BAND_Y - 4, w, WHITE_BAND_H + 8));
  text_layer_set_background_color(s_clock_layer, GColorClear);
  text_layer_set_text_color(s_clock_layer, THEME_COLOR);
  text_layer_set_font(s_clock_layer, fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM));
  text_layer_set_text_alignment(s_clock_layer, GTextAlignmentCenter);
  text_layer_set_text(s_clock_layer, "0:00");
  layer_add_child(window_layer, text_layer_get_layer(s_clock_layer));

  s_battery_layer = text_layer_create(GRect(w - 60, 228 - 26, 58, 22));
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
  text_layer_set_text(s_battery_layer, "100%");
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));

  s_date_layer = text_layer_create(GRect(8, 228 - 26, 110, 22));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentLeft);
  text_layer_set_text(s_date_layer, "SUN JAN 1");
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  battery_state_service_subscribe(battery_update);
  battery_update(battery_state_service_peek());
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  touch_service_subscribe(touch_handler, NULL);
  accel_tap_service_subscribe(accel_tap_handler);

  update_time();
}

static void prv_window_unload(Window *window) {
  if (s_tap_timer) {
    app_timer_cancel(s_tap_timer);
    s_tap_timer = NULL;
  }
  if (s_status_timer) {
    app_timer_cancel(s_status_timer);
    s_status_timer = NULL;
  }
  touch_service_unsubscribe();
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();

  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_clock_layer);
  layer_destroy(s_white_band_layer);
  layer_destroy(s_sep_layer);
  layer_destroy(s_api_layer);
}

// ── init / main ───────────────────────────────────────────────────────────────

static void prv_init(void) {
  if (persist_exists(PERSIST_KEY_API_TEXT)) {
    persist_read_string(PERSIST_KEY_API_TEXT, s_api_text, sizeof(s_api_text));
  }

  s_window = window_create();
  window_set_background_color(s_window, THEME_COLOR);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
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
