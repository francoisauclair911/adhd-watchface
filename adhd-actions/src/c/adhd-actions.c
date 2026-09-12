#include <pebble.h>
#include <message_keys.auto.h>

#define ACTION_REFRESH  1
#define ACTION_COMPLETE 2

static Window    *s_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_status_layer;
static char       s_status_text[121] = "Loading...";

static void set_status_text(const char *text) {
  strncpy(s_status_text, text, sizeof(s_status_text) - 1);
  s_status_text[sizeof(s_status_text) - 1] = '\0';
  if (s_status_layer) {
    text_layer_set_text(s_status_layer, s_status_text);
  }
}

static void send_action(uint8_t action) {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    set_status_text("Phone unavailable");
    APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_begin failed: %d", (int)result);
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_action, action);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    set_status_text("Phone unavailable");
    APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_send failed: %d", (int)result);
  }
}

static uint16_t menu_get_num_sections(MenuLayer *menu_layer, void *context) {
  return 1;
}

static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section,
                                  void *context) {
  return 2;
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer,
                          MenuIndex *cell_index, void *context) {
  const char *title = cell_index->row == 0 ? "Complete" : "Refresh";
  menu_cell_title_draw(ctx, cell_layer, title);
}

static void menu_select(MenuLayer *menu_layer, MenuIndex *cell_index,
                        void *context) {
  if (cell_index->row == 0) {
    set_status_text("Completing...");
    send_action(ACTION_COMPLETE);
  } else {
    set_status_text("Refreshing...");
    send_action(ACTION_REFRESH);
  }
}

static MenuLayerCallbacks s_menu_callbacks = {
  .get_num_sections = menu_get_num_sections,
  .get_num_rows = menu_get_num_rows,
  .draw_row = menu_draw_row,
  .select_click = menu_select,
};

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_apiText);
  if (tuple && tuple->type == TUPLE_CSTRING) {
    set_status_text(tuple->value->cstring);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_status_layer = text_layer_create(GRect(8, 4, bounds.size.w - 16, 48));
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, GColorBlack);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_status_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_status_layer, s_status_text);
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  s_menu_layer = menu_layer_create(GRect(0, 52, bounds.size.w, bounds.size.h - 52));
  menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_callbacks);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorBlack, GColorWhite);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void prv_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_status_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
