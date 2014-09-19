/*
 * Simplicity by CM
 *
 * Copyright (c) 2014 Christoffer Martinsson/Max Baeumle
 *
 * Simplicity by CM include code inspired by:
 * Max Baeumle https://github.com/maxbaeumle/smartwatchface
 * James Fowler https://github.com/JamesFowler42/diaryface20
 * Martin Norland https://github.com/cynorg/PebbleTimely
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "common.h"

#define TODAY "Today"
#define TOMORROW "Tomorrow"
#define ALL_DAY "All day"
	
// Window, Layer, and Bitmap declarations
static Window 		*window;
static TextLayer 	*text_date_layer;
static TextLayer 	*text_time_layer;
static TextLayer 	*text_week_layer;
static TextLayer 	*text_event_title_layer;
static TextLayer 	*text_event_start_date_layer;
static TextLayer 	*text_event_location_layer;
static Layer 			*line_layer;
static Layer 			*battery_layer;
static GBitmap 		*icon_battery;

// Connected info
//static bool bluetooth_state_changed = false;
static bool app_state_changed = true;
static bool bluetooth_connected = true;
static bool app_connected = true;
static int 	current_day_number = 0;

// Event array for storing two events
Event 			event[2];
Event 			last_event[2];
static int	event_count;
static char week_text[] = "W 00";
static int 	alarm_event = 0;
static char event_start_date_static[BASIC_SIZE];

// Itit battery status
BatteryStatus battery_status;
static CloseDay g_close[7];
static int g_last_tm_mday = -1;

// Build a cache of dates vs day names
static void ensure_close_day_cache() {
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);

  struct tm fiddle;

  if (now_tm->tm_mday == g_last_tm_mday)
    return;

  g_last_tm_mday = now_tm->tm_mday;

  for (int i = 0; i < 7; i++) {
    memcpy(&fiddle, now_tm, sizeof(fiddle));
    if (i > 0)
      time_plus_day(&fiddle, i);
    strftime(g_close[i].date, CLOSE_DATE_SIZE, "%m/%d", &fiddle);
    strftime(g_close[i].day_name, CLOSE_DAY_NAME_SIZE, "%A", &fiddle);
  }
  strcpy(g_close[0].day_name, TODAY);
  strcpy(g_close[1].day_name, TOMORROW);
}

static void modify_calendar_time(char *output, int outlen, char *date, bool all_day) {

  // When "Show next events" is turned off in the app:
  // MM/dd
  // When "Show next events" is turned on:
  // MM/dd/yy

  // If all_day is false, time is added like so:
  // MM/dd(/yy) H:mm
  // If clock style is 12h, AM/PM is added:
  // MM/dd(/yy) H:mm a
  // Build a list of dates and day names closest to the current date
  ensure_close_day_cache();

  int time_position = 9;
  if (date[5] != '/')
    time_position = 6;

  // Find the date in the list prepared
  char temp[12];
  bool found = false;
  for (int i = 0; i < 7; i++) {
    if (strncmp(g_close[i].date, date, 5) == 0) {
      strncpy(temp, g_close[i].day_name, sizeof(temp));
      found = true;
      break;
    }
  }

  // If not found then show the month and the day
  if (!found) {
    time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    struct tm fiddle;
    memcpy(&fiddle, now_tm, sizeof(fiddle));
    fiddle.tm_mday = a_to_i(&date[3], 2);
    fiddle.tm_mon = a_to_i(&date[0], 2) - 1;
    strftime(temp, sizeof(temp), "%b %e -", &fiddle);
  }
  // Change the format based on whether there is a timestamp
  if (all_day)
    snprintf(output, outlen, "%s %s", temp, ALL_DAY);
  else
    snprintf(output, outlen, "%s %s", temp, &date[time_position]);
}

// Vibe generator 
void generate_vibe(uint32_t vibe_pattern_number) {
  vibes_cancel();
  switch ( vibe_pattern_number ) {
		case 0: // No Vibration
			return;
		case 1: // Single short
			vibes_short_pulse();
			break;
		case 2: // Double short
			vibes_double_pulse();
			break;
		case 3: // Triple
			vibes_double_pulse();
		case 4: // Long
			vibes_double_pulse();
			break;
		case 5: // Subtle
			vibes_double_pulse();
			break;
		case 6: // Less Subtle
			vibes_double_pulse();
			break;
		case 7: // Not Subtle
			vibes_double_pulse();
			break;
	 case 8: // Not Subtle
			vibes_double_pulse();
			break;
		default: // No Vibration
			return;
  }
}

void line_layer_update_callback(Layer *layer, GContext *ctx) {
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx, GPoint(8, 94), GPoint(131, 94));
  graphics_draw_line(ctx, GPoint(8, 95), GPoint(131, 95));
}

void battery_layer_update_callback(Layer *layer, GContext *ctx) {
  graphics_context_set_compositing_mode(ctx, GCompOpAssignInverted);
  graphics_draw_bitmap_in_rect(ctx, icon_battery, GRect(35, 0, 24, 12));

  if (battery_status.state != 0 && battery_status.level >= 0 && battery_status.level <= 100) {
    int num = snprintf(NULL, 0, "%d %%", battery_status.level);
    char *str = malloc(num + 1);
    snprintf(str, num + 1, "%d %%", battery_status.level);

    graphics_draw_text(ctx, str, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(-2, -3, 35-3, 14), 0, GTextAlignmentRight, NULL);

    free(str);

    if (battery_status.level > 0) {
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(38, 3, (uint8_t)((battery_status.level / 100.0) * 16.0), 6), 0, GCornerNone);
    }
  }
}

void handle_request_battery_data(void *data) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  if (!iter) {
    app_timer_register(1000, &handle_request_battery_data, NULL);
    return;
  }

  dict_write_uint8(iter, REQUEST_BATTERY_KEY, 1);
  app_message_outbox_send();
}

void handle_request_calendar_data(void *data){
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  if (!iter) {
    app_timer_register(1000, &handle_request_calendar_data, NULL);
    return;
  }
  
  dict_write_int8(iter, REQUEST_CALENDAR_KEY, event_count);
  uint8_t clock_style = clock_is_24h_style() ? CLOCK_STYLE_24H : CLOCK_STYLE_12H;
  dict_write_uint8(iter, CLOCK_STYLE_KEY, clock_style);
  app_message_outbox_send();

  app_timer_register(1000, &handle_request_battery_data, NULL);
 
}

void update_connection() {
  // Only run when changed
 	if(app_state_changed && !app_connected) {
		app_state_changed = false;
		// Display text in calendar view
		text_layer_set_text(text_event_start_date_layer, "App disconnected");
		text_layer_set_text(text_event_title_layer, "WARNING!");
		text_layer_set_text(text_event_location_layer, "");
		// Vibrate hard 5 times
		generate_vibe(7);
	}
	if (!app_state_changed && app_connected) {
		app_state_changed = true;
		text_layer_set_text(text_event_title_layer, event[0].title);
		text_layer_set_text(text_event_start_date_layer, event_start_date_static);
		text_layer_set_text(text_event_location_layer, event[0].has_location ? event[0].location : "");
		handle_request_calendar_data(NULL);
	}
}

void handle_message_fail(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    if(reason == APP_MSG_NOT_CONNECTED){
        // Connection to smartwatch pro app NOT OK!
        app_connected = false;
        update_connection();
    }
}

// handle BT status change related events
void handle_bluetooth_connection(bool connected) {
  if(!bluetooth_connected && connected) {
		// Connection to BT OK
    bluetooth_connected = true;
		// Update event display
		text_layer_set_text(text_event_title_layer, event[0].title);
		text_layer_set_text(text_event_start_date_layer, event_start_date_static);
		text_layer_set_text(text_event_location_layer, event[0].has_location ? event[0].location : "");
		handle_request_calendar_data(NULL);
		// Vibrate 3 times
		generate_vibe(3);
	}
	if (bluetooth_connected && !connected) {
		// Connection to BT NOT OK!
		bluetooth_connected = false;
		// Display text in calendar view
		text_layer_set_text(text_event_start_date_layer, "BT disconnected");
		text_layer_set_text(text_event_title_layer, "WARNING!");
		text_layer_set_text(text_event_location_layer, "");
		// Vibrate hard 5 times
		generate_vibe(7);
  }
}

void set_partial_inverse(bool partial_inverse) {
  if (partial_inverse) {
    text_layer_set_background_color(text_date_layer, GColorWhite);
    text_layer_set_text_color(text_date_layer, GColorBlack);

    text_layer_set_background_color(text_time_layer, GColorClear);
    text_layer_set_text_color(text_time_layer, GColorBlack);
  } else {
    text_layer_set_background_color(text_date_layer, GColorClear);
    text_layer_set_text_color(text_date_layer, GColorWhite);

    text_layer_set_background_color(text_time_layer, GColorClear);
    text_layer_set_text_color(text_time_layer, GColorWhite);
  }
}

static void update_event_display() {
	char event_start_date[BASIC_SIZE];
	for (int i = 0; i <= event_count; i++) {
		modify_calendar_time(event_start_date, sizeof(event_start_date), event[i].start_date, event[i].all_day);
		strncpy(event_start_date_static, event_start_date, sizeof(event_start_date));
		text_layer_set_text(text_event_title_layer, event[i].title);
		text_layer_set_text(text_event_start_date_layer, event_start_date_static);
		text_layer_set_text(text_event_location_layer, event[i].has_location ? event[i].location : "");
		layer_mark_dirty(text_layer_get_layer(text_event_title_layer));
		layer_mark_dirty(text_layer_get_layer(text_event_start_date_layer));
		layer_mark_dirty(text_layer_get_layer(text_event_location_layer));
	}
}

void handle_message_receive(DictionaryIterator *received, void *context) {
	
  Tuple *tuple = dict_find(received, SETTINGS_RESPONSE_KEY);

  if (tuple) {
    tuple = dict_find(received, 100);

    if (tuple) {
      bool partial_inverse = tuple->value->uint8;
      persist_write_bool(100, partial_inverse);
      set_partial_inverse(partial_inverse);
    }

    return;
  }

  tuple = dict_find(received, RECONNECT_KEY);

  if (tuple) {
    app_timer_register(200, &handle_request_calendar_data, NULL);
  } else {
    Tuple *tuple = dict_find(received, CALENDAR_RESPONSE_KEY);

    if (tuple) {
        if (tuple->length % sizeof(Event) == 1) {
          uint8_t count = tuple->value->data[0];

          if (count == 1) {
            // Copy data from phone app to memory
            memcpy(&event[event_count], &tuple->value->data[1], sizeof(Event));
            // Check if message changed from last message
            int title_new = strcmp(event[event_count].title, last_event[event_count].title);
            int start_date_new = strcmp(event[event_count].start_date, last_event[event_count].start_date);
            memcpy(&last_event[event_count], &tuple->value->data[1], sizeof(Event));

            // Check if second event is received 
            if (event_count == 1){
              // Display event if first event is a "all_day"-event and this is not.
              if((event[0].all_day) && (!event[1].all_day)){
                if(title_new != 0 || start_date_new != 0){
                  // Display event on LCD
                	update_event_display();
                  // Set alarm on second event
                  alarm_event = 1;                
                }
              }
            }
						else {
              if(title_new != 0 || start_date_new != 0){
                // Display event on LCD
                update_event_display();
                // Set alarm on first event
                alarm_event = 0;                
              }
              // Get next event 
              event_count = 1;
              app_timer_register(200, &handle_request_calendar_data, NULL);
            }
          }
        }
    } else {
      Tuple *tuple = dict_find(received, BATTERY_RESPONSE_KEY);

      if (tuple) {
        memcpy(&battery_status, &tuple->value->data[0], sizeof(BatteryStatus));
        layer_mark_dirty(battery_layer);
      }
    }
  }
	
  // Connection to smartwatch pro app OK
  app_connected = true;
  update_connection();
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  // Need to be static because they're used by the system later.
  static char time_text[] = "00:00";
  static char date_text[] = "Xxxxxxxxx xxx 00 xxx";
  static char week_text[] = "W 00";
  static char alarm_text[] = "00/00 00:00 XX";
  static char alarm_date[] = "00/00 ";
  static char alarm_time[] = "00:00 XX";

  char *time_format;
  char *alarm_date_format;
  char *alarm_time_format;

  if (!tick_time) {
    time_t now = time(NULL);
    tick_time = localtime(&now);
  }

  // Only update if date changed
  if(tick_time->tm_mday != current_day_number){
    current_day_number = tick_time->tm_mday;
    // Update date and week
    strftime(week_text, sizeof(week_text), "W%V", tick_time);
    strftime(date_text, sizeof(date_text), "%A %b %e", tick_time);
    // Display date and week number on the LCD
    text_layer_set_text(text_date_layer, date_text);
    text_layer_set_text(text_week_layer, week_text);
  }

  // Set up date and time format
  if (clock_is_24h_style()) {
    time_format = "%R";
    alarm_time_format = "%R";
  } else {
    time_format = "%I:%M";
    alarm_time_format = "%I:%M %p";
  }
  alarm_date_format = "%m/%d ";

  strftime(time_text, sizeof(time_text), time_format, tick_time);
  strftime(alarm_time, sizeof(alarm_time), alarm_time_format, tick_time);
  strftime(alarm_date, sizeof(alarm_date), alarm_date_format, tick_time);

  // Kludge to handle lack of non-padded hour format string
  // for twelve hour clock.
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  // Build current time to match app alarm date/time 
  if ((alarm_time[0] == '0')) {
    memmove(alarm_time, &alarm_time[1], sizeof(alarm_time) - 1);
  }
  memcpy(&alarm_text, &alarm_date, sizeof(alarm_date));
  strcat(alarm_text, alarm_time);

  // Display new time on LCD
  text_layer_set_text(text_time_layer, time_text);

  if (strcmp(alarm_text, event[alarm_event].start_date) == 0){
    generate_vibe(8);
  }

  // Update calendar data
  if (tick_time->tm_min % 10 == 0) {
    event_count = 0; 
    app_timer_register(500, &handle_request_calendar_data, NULL);
  }
}

void init() {
  window = window_create();
  window_stack_push(window, true /* Animated */);
  window_set_background_color(window, GColorBlack);

 	text_date_layer = text_layer_create(GRect(0, 94, 144, 168-94));
  text_layer_set_font(text_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(text_date_layer, GTextAlignmentCenter);

  text_time_layer = text_layer_create(GRect(2, 112, 144-2, 168-112));
  text_layer_set_font(text_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(text_time_layer, GTextAlignmentCenter);

  set_partial_inverse(persist_read_bool(100));

  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_date_layer));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_time_layer));
  
	text_week_layer = text_layer_create(GRect(0, -5, 50, 28));
  text_layer_set_text_color(text_week_layer, GColorWhite);
  text_layer_set_background_color(text_week_layer, GColorClear);
  text_layer_set_font(text_week_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(text_week_layer, GTextAlignmentCenter);
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_week_layer));
	
  line_layer = layer_create(layer_get_bounds(window_get_root_layer(window)));
  layer_set_update_proc(line_layer, line_layer_update_callback);
  layer_add_child(window_get_root_layer(window), line_layer);

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

  // Event Location
  text_event_location_layer = text_layer_create(GRect(5, 22, layer_get_bounds(window_get_root_layer(window)).size.w - 5, 31));
  text_layer_set_text_color(text_event_location_layer, GColorWhite);
  text_layer_set_background_color(text_event_location_layer, GColorClear);
  text_layer_set_font(text_event_location_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_event_location_layer));

  // Event Date
  text_event_start_date_layer = text_layer_create(GRect(5, 64, layer_get_bounds(window_get_root_layer(window)).size.w - 5, 31));
  text_layer_set_text_color(text_event_start_date_layer, GColorWhite);
  text_layer_set_background_color(text_event_start_date_layer, GColorClear);
  text_layer_set_font(text_event_start_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_event_start_date_layer));

  // Event title
  text_event_title_layer = text_layer_create(GRect(5, 42, layer_get_bounds(window_get_root_layer(window)).size.w - 5, 31));
  text_layer_set_text_color(text_event_title_layer, GColorWhite);
  text_layer_set_background_color(text_event_title_layer, GColorClear);
  text_layer_set_font(text_event_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(text_event_title_layer));

  // Init bluetooth connection handles
  bluetooth_connection_service_subscribe(&handle_bluetooth_connection);
  //handle_bluetooth_connection(bluetooth_connection_service_peek());
	bluetooth_connected = bluetooth_connection_service_peek();
  
  icon_battery = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_ICON);

  GRect frame;
  frame.origin.x = layer_get_bounds(window_get_root_layer(window)).size.w - 66;
  frame.origin.y = 6;
  frame.size.w = 59;
  frame.size.h = 12;

  battery_layer = layer_create(frame);
  layer_set_update_proc(battery_layer, battery_layer_update_callback);
  layer_add_child(window_get_root_layer(window), battery_layer);

  battery_status.state = 0;
  battery_status.level = -1;

  app_message_open(124, 256);
  app_message_register_inbox_received(handle_message_receive);
  app_message_register_outbox_failed(handle_message_fail);

  event_count = 0; 
  app_timer_register(200, &handle_request_calendar_data, NULL);
  
}

void deinit() {
  app_message_deregister_callbacks();
  tick_timer_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  layer_destroy(battery_layer);
  layer_destroy(line_layer);
  text_layer_destroy(text_week_layer);
  text_layer_destroy(text_event_location_layer);
  text_layer_destroy(text_event_start_date_layer);
  text_layer_destroy(text_event_title_layer);
  text_layer_destroy(text_time_layer);
  text_layer_destroy(text_date_layer);
  gbitmap_destroy(icon_battery);
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}