#include <string.h>
#include <shiftregister_gpio.h>
#include <semaphore.h>
#include <cJSON.h>
#include <esp_littlefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "bus/dgx_spi_esp32.h"
#include "drivers/ili9341.h"
#include "dgx_bits.h"
#include "dgx_colors.h"
#include "dgx_draw.h"
#include "dgx_font.h"
#include "fonts/DroidSansRegular9.h"
#include "main.h"
#include "drivers/vscreen.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_netif_sntp.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

//*********************************************************************************************************************

uint16_t fast_blend(uint8_t alpha, uint32_t fgc, uint32_t bgc) {
  // Split out and blend 5-bit red and blue channels
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  // Split out and blend 6-bit green channel
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  // Recombine channels
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

void fill_rect_v_gradient(int16_t x, int16_t y, int w, int h, uint32_t color_1, uint32_t color_2) {
    float delta = -255.0 / h;
    float alpha = 255.0;
    uint32_t color = color_1;

    while (h--) {
        dgx_draw_line(tft, x, y, x + w, y, color);
        alpha += delta;
        color = fast_blend((uint8_t) alpha, color_1, color_2);
        y++;
    }
}

//*********************************************************************************************************************

void add_image_to_fs(const uint16_t *image_data, const char *file_name, unsigned int size) {
        ESP_LOGI(TAG_INFO, "Opening file %s", file_name);
        
        char path_buffer[FILE_PATH_BUFFER_SIZE];
        snprintf(path_buffer, FILE_PATH_BUFFER_SIZE, "%s/%s", BASE_PATH, file_name);
        FILE *f = fopen(path_buffer, "wb");
        if (f == NULL) {
            ESP_LOGE(TAG_ERROR, "Failed to open file for writing");
            return;
        }

        if (fwrite(image_data, sizeof(uint16_t), size, f) < size) {
            ESP_LOGI(TAG_ERROR, "Error during fwrite().");
        } else {
            ESP_LOGI(TAG_INFO, "File written");
        }
        
        fclose(f);
}

uint16_t *get_image_data_from_fs(const char *file_name, size_t size) {
    char file_path_buffer[FILE_PATH_BUFFER_SIZE];
    snprintf(file_path_buffer, FILE_PATH_BUFFER_SIZE, "%s/%s", BASE_PATH, file_name);
    FILE *f = fopen(file_path_buffer, "rb");
    if (f == NULL) {
        error_bits = ERROR_FS_LOAD;
        ESP_LOGE(TAG_ERROR, "Error reading \"%s\".", file_name);
        return NULL;
    }
    uint16_t *bitmap_buffer = (uint16_t*) malloc(size * sizeof(uint16_t));
    if (bitmap_buffer == NULL) {
        error_bits = ERROR_FS_LOAD;
        ESP_LOGE(TAG_ERROR, "Error mallocing \"%s\".", file_name);
        return NULL;
    }
    fread(bitmap_buffer, sizeof(uint16_t), size, f);
    fclose(f);

    return bitmap_buffer;
}

//*********************************************************************************************************************

char* string_to_lower(const char *str) {
	size_t len = strlen(str) + 1;
	char *lower_str = (char*) malloc(len);
	memset(lower_str, 0, len);

	unsigned int i;
	for (i = 0; i < len; i++) {
		lower_str[i] = tolower(str[i]);
	}
	
	return lower_str;
}

bool compare_strings(const char *str_1, const char *str_2, int usr_comp_size) {
	char *lower_str_1 = string_to_lower(str_1);
	char *lower_str_2 = string_to_lower(str_2);

	// strncmp will prevent a buffer overflow, but we also want to make 
	// sure we compare the entire string and not just part of it, so 
	// use the longest string as the comparison size.
    bool result;
    if (usr_comp_size == -1) {
        size_t size_1 = strlen(lower_str_1);
	    size_t size_2 = strlen(lower_str_2);
	    size_t comp_size = size_1 > size_2 ? size_1 : size_2;
	    result = strncmp(lower_str_1, lower_str_2, comp_size) == 0;
    } else {
        result = strncmp(lower_str_1, lower_str_2, usr_comp_size) == 0;
    }
	
	free(lower_str_1);
	free(lower_str_2);

	return result;
}

bool string_contains(const char *str_1, const char *str_2, bool case_sensitive) {
    if (!case_sensitive) {
	    char *lower_str_1 = string_to_lower(str_1);
	    char *lower_str_2 = string_to_lower(str_2);

        char *result = strstr(lower_str_1, lower_str_2);

	    free(lower_str_1);
	    free(lower_str_2);

        return result != NULL;
    }

	return strstr(str_1, str_2) != NULL;
}

//*********************************************************************************************************************

time_t get_utc_time_t_from_str(const char *datetime_str, const char *format) {
    struct tm tm;
    strptime(datetime_str, format, &tm);
    return mktime(&tm);
}

//*********************************************************************************************************************

int divide_round_up(int a, int b) {
    return (a / b) + (a % b != 0);
}

//*********************************************************************************************************************

struct MenuNode* find_grandparent_node(struct MenuNode *node, struct MenuNode *grandparent_node) {
    if (node == NULL) return NULL;
	if (node == curr_parent_node) return grandparent_node;
	
	// Recurse through the path of the first child.
	struct MenuNode *node_1 = find_grandparent_node(node->first_child, node);
	if (node_1 != NULL) return node_1;

	struct MenuNode *node_2 = find_grandparent_node(node->next_sibling, grandparent_node);
	if (node_2 != NULL) return node_2;

	return NULL;
}

void exit_menu() {
    curr_parent_node = root_menu_node;
    curr_section_lead_node = root_menu_node->first_child;
    curr_menu_node = curr_section_lead_node;
    curr_menu_pos = 0;

    // Return to the main state cc view and reset the main state cycle timer.
    curr_state = STATE_MAIN;
    main_state_list.curr_node = main_state_list.root_node;
    ESP_LOGI(TAG_INFO, "Resetting main_view_cycle_timer ...");
    xTimerReset(main_view_cycle_timer, 0);
    xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
}

void perform_menu_action() {
	// Back (go back to the previous menu (parent node's parent))
	if (compare_strings(MENU_OPTION_BACK, curr_menu_node->data, 4)) {
		curr_parent_node = find_grandparent_node(root_menu_node, NULL);
		if (curr_parent_node != NULL) {
			curr_section_lead_node = curr_parent_node->first_child;
			curr_menu_node = curr_section_lead_node;
			curr_menu_pos = 0;
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
		} else {
            exit_menu();
        }
    } else if (compare_strings(MENU_OPTION_SYSTEM_INFO, curr_menu_node->data, -1)) {
        curr_state = STATE_SI;
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
	} else if (compare_strings(MENU_OPTION_CLEAR_ERROR_CODE, curr_menu_node->data, -1)) {
        error_bits = 0x00;
        xTaskNotifyGiveIndexed(error_bits_task_handle, 0);
    } else if (compare_strings(MENU_OPTION_SYSTEM_INFO, curr_menu_node->data, -1)) {
        curr_state = STATE_SI;
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
    } else if (compare_strings(MENU_OPTION_UPDATE_CC, curr_menu_node->data, -1)) {
        xTaskNotifyGiveIndexed(cc_task_handle, 0);
        xTimerReset(cc_refresh_timer, 0);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_UPDATE_FORECASTS, curr_menu_node->data, -1)) {
        xTaskNotifyGiveIndexed(forecast_task_handle, 0);
        xTimerReset(forecast_refresh_timer, 0);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_CLEAR, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_CLEAR);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_CLOUDY, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_CLOUDY);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_FOG, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_FOG);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_PARTLY_CLOUDY, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_PARTLY_CLOUDY);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_SNOW, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_SNOW);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_SCATTERED_SHOWERS, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_SCATTERED_SHOWERS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_THUNDERSTORM, curr_menu_node->data, -1)) {
        memset(main_state_list.root_node->cc.desc, 0, DESC_BUFFER_SIZE);
        snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_THUNDERSTORM);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_TORNADO_WARNING, curr_menu_node->data, -1)) {
        add_test_alert(MENU_OPTION_TORNADO_WARNING, JSON_EXTREME);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_TORNADO_WATCH, curr_menu_node->data, -1)) {
        add_test_alert(MENU_OPTION_TORNADO_WATCH, JSON_MODERATE);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_WRAPPED_TEXT, curr_menu_node->data, -1)) {
        curr_state = STATE_WRAPPED_TEXT;
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
    }
}

struct MenuNode* create_menu_nodes(cJSON *curr_json, struct MenuNode *parent_node, bool part_of_array) {
    // Create menu node.
    struct MenuNode *new_node = (struct MenuNode*) calloc(1, sizeof(struct MenuNode));
    snprintf(new_node->data, MENU_NODE_DATA_BUFFER_SIZE, "%s", 
        part_of_array ? curr_json->valuestring : curr_json->string);
    if (parent_node != NULL && parent_node->first_child == NULL) {
        parent_node->first_child = new_node;
    }

    // If this is an array or object, we need to iterate through it and create nodes.
    bool next_parts_in_array = cJSON_IsArray(curr_json);
    if (part_of_array || !(next_parts_in_array || cJSON_IsObject(curr_json))) {
        return new_node;
    }

    cJSON *curr_arr_item = curr_json->child;
    struct MenuNode *prev_node = NULL;
    while (curr_arr_item != NULL) {
        struct MenuNode *new_obj_arr_node = create_menu_nodes(curr_arr_item, new_node, next_parts_in_array);
        if (prev_node != NULL) {
            prev_node->next_sibling = new_obj_arr_node;
            new_obj_arr_node->prev_sibling = prev_node;
        }
        prev_node = new_obj_arr_node;
        curr_arr_item = curr_arr_item->next;
    }

    return new_node;
}

//*********************************************************************************************************************

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        gpio_set_level(PIN_INC_DATA_LED, 1);
        int copy = evt->data_len;
        if (http_response_buffer_len + copy >= HTTP_GET_RESPONSE_BUFFER_SIZE) {
            copy = HTTP_GET_RESPONSE_BUFFER_SIZE - http_response_buffer_len - 1;
        }
        memcpy(http_response_buffer + http_response_buffer_len, evt->data, copy);
        http_response_buffer_len += copy;
        http_response_buffer[http_response_buffer_len] = '\0';
        gpio_set_level(PIN_INC_DATA_LED, 0);
    }
    return ESP_OK;
}

//*********************************************************************************************************************

void add_main_state(const uint8_t state, const char *local_forecast, struct ExtForecastNode *ext_forecasts_root_node) {
    struct MainStateNode *new_node = (struct MainStateNode *) calloc(1, sizeof(struct MainStateNode));
    new_node->state = state;
    new_node->ext_forecasts_root_node = ext_forecasts_root_node;
    snprintf(new_node->local_forecast, LOCAL_FORECAST_BUFFER_SIZE, "%s", local_forecast);
    
    if (main_state_list.root_node->next_node == NULL) {
        main_state_list.root_node->next_node = new_node;
    } else {
        struct MainStateNode *temp_node = main_state_list.root_node;
        while (temp_node->next_node != main_state_list.root_node) {
            temp_node = temp_node->next_node;
        }
        temp_node->next_node = new_node;
    }
    new_node->next_node = main_state_list.root_node;
}

void remove_forecast_main_states() {
    struct MainStateNode *temp_node = main_state_list.root_node->next_node;
    struct MainStateNode *placeholder_node;
    while (temp_node != main_state_list.root_node && temp_node != NULL) {
        placeholder_node = temp_node->next_node;
        clear_ext_forecasts_list(temp_node->ext_forecasts_root_node);
        free(temp_node);
        temp_node = placeholder_node;
    }
    main_state_list.root_node->next_node = NULL;
    main_state_list.curr_node = main_state_list.root_node;
}

//*********************************************************************************************************************

struct ExtForecastNode* create_ext_forecast_node(const char *desc, const char *day, uint8_t high_temp, uint8_t low_temp,
        const char *start_time_str, const char *end_time_str) {
    struct ExtForecastNode *new_node = (struct ExtForecastNode*) calloc(1, sizeof(struct ExtForecastNode));
    memset(new_node->desc, 0, DESC_BUFFER_SIZE);
    snprintf(new_node->desc, DESC_BUFFER_SIZE, "%s", desc);
    snprintf(new_node->day, DAY_BUFFER_SIZE, "%s", day);
    new_node->high_temp = high_temp;
    new_node->low_temp = low_temp;
    new_node->start_time = get_utc_time_t_from_str(start_time_str, DATE_TIME_STRING_FORMAT);
    new_node->end_time = get_utc_time_t_from_str(end_time_str, DATE_TIME_STRING_FORMAT);

    return new_node;
}

void clear_ext_forecasts_list(struct ExtForecastNode *root_node) {
    struct ExtForecastNode *temp_node = root_node;
    struct ExtForecastNode *placeholder_node;
    while (temp_node != NULL) {
        placeholder_node = temp_node->next_node;
        free(temp_node);
        temp_node = placeholder_node;
    }
}

//*********************************************************************************************************************

void draw_header_rect() {
    fill_rect_v_gradient(HEADER_RECT_X_POS, HEADER_RECT_Y_POS, HEADER_RECT_W, HEADER_RECT_H, 
        DGX_COPPER(dgx_rgb_to_16), DGX_PURPLE(dgx_rgb_to_16));
}

void draw_header_string(const char *str) {
    draw_string(str, HEADER_TEXT_X_POS, HEADER_TEXT_Y_POS, DGX_YELLOW(dgx_rgb_to_16), 2);
}

void draw_string(const char *str, int16_t x, int16_t y, uint32_t color, int scale) {
    dgx_font_string_utf8_screen(tft, x + SHADOW_OFFSET, y + SHADOW_OFFSET, str, DGX_BLACK(dgx_rgb_to_16), 
        DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
    dgx_font_string_utf8_screen(tft, x, y, str, color, DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
}

void draw_string_wrapped(const char *str, int16_t x, int16_t y, uint32_t color, int scale, int16_t bound_width, 
        int16_t bound_height) {
    size_t str_len;            
    if (str == NULL || (str_len = strlen(str)) < 1) {
        ESP_LOGI(TAG_INFO, "Did not write wrapped string since str was NULL or empty.");
        return;
    }            

    // Calculate the text width, text height, and top offset in pixels.
    int16_t top_offset, text_height;
    int text_width = dgx_font_string_bounds(str, DroidSansRegular9(), &top_offset, &text_height) * scale;
    top_offset *= scale;
    text_height *= scale;
    int row_advance = WRAPPED_TEXT_ROW_OFFEST * scale;

    // If the text width is less than or equal to the bound width, we can just draw the string normally.
    if (text_width <= bound_width) {
        draw_string(str, x, y - top_offset + row_advance, color, scale);
    } else {
        // Allocate heap space for the rows.
        char **row_contents = (char**) malloc((WRAPPED_TEXT_MAX_NUM_ROWS * sizeof(char *)));
        for (int i = 0; i < WRAPPED_TEXT_MAX_NUM_ROWS; i++) {
            row_contents[i] = (char *) malloc(WRAPPED_TEXT_TEMP_BUFFER_SIZE * sizeof(char));
        }

        char row_buffer[WRAPPED_TEXT_TEMP_BUFFER_SIZE] = {0};
        const char *p = str;
        size_t row_len = 0;
        int num_rows = 0;
        while (*p) {
            // Get the next word in the string.
            while (*p == ' ') p++;  // Advance p past any spaces at the current position. *p means "the character/byte that p poitns to."
            if (*p == '\0') break;  // If p now points to '\0'—the string’s end—there’s no word left, so stop.
            
            const char *word = p;  // Remember where the word starts. word points into the original string; it doesn’t copy the word.
            while (*p != '\0' && *p != ' ') p++;  // Advance p until it reaches a space or the end of the string. Now p points just past the word.
            size_t word_bytes = (size_t) (p - word);  // Subtract the starting address from the ending address to get the word's length in bytes.

            // If the word is too large for the buffer, just break and don't render it.
            if (word_bytes + 1 > sizeof(row_buffer)) {
                break;
            }

            size_t separator_bytes = (row_len > 0) ? 1 : 0;
            size_t needed = row_len + separator_bytes + word_bytes + 1;
            if (needed > sizeof row_buffer && row_len > 0) {
                if (num_rows >= WRAPPED_TEXT_MAX_NUM_ROWS) {
                    break;  // If there's too many rows, just stop.
                }
                memcpy(row_contents[num_rows++], row_buffer, row_len + 1);
                memset(row_buffer, 0, WRAPPED_TEXT_TEMP_BUFFER_SIZE);
                separator_bytes = 0;
                row_len = 0;
            }

            // Keep track of the previous length.
            size_t old_len = row_len;

            // Add the separator to the buffer if necessary before adding the word.
            if (separator_bytes) {
                row_buffer[row_len++] = ' ';
            }
            memcpy(row_buffer + row_len, word, word_bytes);
            row_len += word_bytes;
            row_buffer[row_len] = '\0';

            // Calculate the width the text will be rendered at. Check if the row will fit. If it doesn't, then remove 
            // the word we just added and put it on a new row instead.
            int16_t unused_top, unused_height;
            int candidate_width = dgx_font_string_bounds(row_buffer, DroidSansRegular9(), &unused_top, &unused_height) 
                * scale;
            if (candidate_width > bound_width && old_len > 0) {
                // Add row without the new word.
                row_buffer[old_len] = '\0';
                row_len = old_len;
                memcpy(row_contents[num_rows++], row_buffer, row_len + 1);

                // Start a new row with the new word.
                memset(row_buffer, 0, WRAPPED_TEXT_TEMP_BUFFER_SIZE);
                memcpy(row_buffer, word, word_bytes);
                row_len = word_bytes;
                row_buffer[row_len] = '\0';
            }
        }
        if (row_len > 0 && num_rows < WRAPPED_TEXT_MAX_NUM_ROWS) {  // Save the last non-empty row.
            memcpy(row_contents[num_rows++], row_buffer, row_len + 1);
        }

        // If the bound_height is defined, check if the text will fit within the height according to the scale.
        // If it doesn't, then we'll need to force the scale down until it does.
        int total_height = text_height + row_advance * (num_rows - 1);
        if (bound_height != -1 && scale > 1 && ((total_height) > bound_height)) {
            // Free heap space.
            for (int i = 0; i < WRAPPED_TEXT_MAX_NUM_ROWS; i++) {
                free(row_contents[i]);
            }
            draw_string_wrapped(str, x, y, color, scale - 1, bound_width, bound_height);
        } else {
            // Finally, write each row to the screen.
            for (int i = 0; i < num_rows; i++) {
                draw_string(row_contents[i], x, y - top_offset + row_advance * i, color, scale);
            }
            // Free heap space.
            for (int i = 0; i < WRAPPED_TEXT_MAX_NUM_ROWS; i++) {
                free(row_contents[i]);
            }
        }
    }
}

void draw_alert(struct AlertNode *alert) {
    // Determine the banner color.
    uint32_t color;
    if (compare_strings(JSON_EXTREME, alert->severity, -1)) {
        color = DGX_RED(dgx_rgb_to_16);
    } else if (compare_strings(JSON_SEVERE, alert->severity, -1)) {
        color = DGX_ORANGE(dgx_rgb_to_16);
    } else {
        color = DGX_YELLOW(dgx_rgb_to_16);
    }

    // We don't need to clear the screen since we want the banner to display on top of anything on screen.
    dgx_fill_rectangle(tft, ALERT_RECT_X_POS, ALERT_RECT_Y_POS, tft->width, ALERT_RECT_H, color);
    draw_string(alert->data, ALERT_TEXT_X_POS, ALERT_TEXT_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);
}

void draw_date_time() {
    draw_string(date, DATE_X_POS, DATE_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);
    draw_string(time_, TIME_X_POS, TIME_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);
}

void draw_bitmap(const uint16_t *bitmap, int size, int x, int y, uint32_t scale) {
    if (bitmap == NULL) {
        return;
    }

    // Calculate the base image by converting it to a high-byte first order.
    int total_size = size * size;
    uint8_t image_be[total_size * 2];
    int i;
    for (i = 0; i < total_size; ++i) {
        uint16_t pixel = bitmap[i];
        image_be[2 * i] = pixel >> 8;
        image_be[2 * i + 1] = pixel & 0xFF;
    }

    // Calculate the shadow image.
    uint8_t shadow_image[sizeof(image_be)];
    memcpy(shadow_image, image_be, sizeof(image_be));

    uint16_t repl = SHADOW_IMG_REPLACEMENT_COLOR;
    uint8_t repl_high = (repl >> 8) & 0xFF;
    uint8_t repl_low = repl & 0xFF;
    int byte_index;
    for (i = 0; i < total_size; ++i) {
        byte_index = 2 * i;
        if (image_be[byte_index] != 0x00 || image_be[byte_index + 1] != 0x00) {
            shadow_image[byte_index] = repl_high;
            shadow_image[byte_index + 1] = repl_low;
        }
    }

    // Draw both images.
    dgx_screen_t *image = dgx_vscreen_from_array(shadow_image, size, size, 16, DgxScreenRGB);
    dgx_vscreen_to_screen(tft, x + SHADOW_OFFSET, y + SHADOW_OFFSET, image, 
        DGX_VSCREEN_SCALE(scale) | DGX_VSCREEN_TRANSPARENT);
    free(image);

    image = dgx_vscreen_from_array(image_be, size, size, 16, DgxScreenRGB);
    dgx_vscreen_to_screen(tft, x, y, image, DGX_VSCREEN_SCALE(scale) | DGX_VSCREEN_TRANSPARENT);
    free(image);
}

void draw_low_arrow_bitmap(int size, int x, int y, uint32_t scale) {
    uint16_t *image_data = get_image_data_from_fs(FILE_LOW_BIN, ICON_IMG_SIZE);
    draw_bitmap(image_data, size, x, y, scale);
    free(image_data);
}

void draw_high_arrow_bitmap(int size, int x, int y, uint32_t scale) {
    uint16_t *image_data = get_image_data_from_fs(FILE_HIGH_BIN, ICON_IMG_SIZE);
    draw_bitmap(image_data, size, x, y, scale);
    free(image_data);
}

void draw_dew_point_bitmap(int size, int x, int y, uint32_t scale) {
    uint16_t *image_data = get_image_data_from_fs(FILE_DEWPOINT_BIN, 1024);
    draw_bitmap(image_data, size, x, y, scale);
    free(image_data);
}

void draw_logo_bitmap(int size, int x, int y, uint32_t scale) {
    uint16_t *image_data = get_image_data_from_fs(FILE_LOGO_BIN, LOGO_IMG_SIZE);
    draw_bitmap(image_data, size, x, y, scale);
    free(image_data);
}

void draw_noaa_logo_bitmap(int size, int x, int y, uint32_t scale) {
    uint16_t *image_data = get_image_data_from_fs(FILE_NOAA_BIN, ICON_IMG_SIZE);
    draw_bitmap(image_data, size, x, y, scale);
    free(image_data);
}

void draw_ext_forecast_condition_bitmap(const char *condition, uint8_t day) {
    int img_x_pos = day == 0 ? EF_DAY_1_IMG_X_POS : EF_DAY_2_IMG_X_POS;

    if (string_contains(condition, CC_CLEAR, false) || string_contains(condition, JSON_SUNNY, false)) {
        draw_condition_clear(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else if (string_contains(condition, CC_PARTLY, false)) {
        draw_condition_partly_cloudy(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else if (string_contains(condition, CC_CLOUDY, false)) {
        draw_condition_cloudy(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else if (string_contains(condition, CC_SNOW, false)) {
        draw_condition_snow(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else if (string_contains(condition, CC_THUNDER, false)) {
        draw_condition_storm(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else if (string_contains(condition, CC_RAIN, false) || string_contains(condition, CC_SHOWERS, false)) {
        draw_condition_rain(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    } else {
        draw_condition_unknown(EF_IMG_SIZE, img_x_pos, EF_IMG_Y_POS, EF_IMG_SCALE);
    }
}

void draw_condition_bitmap(const char *condition) {
    if (string_contains(condition, CC_CLEAR, false) || string_contains(condition, JSON_SUNNY, false)) {
        draw_condition_clear(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_PARTLY, false)) {
        draw_condition_partly_cloudy(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_CLOUDY, false)) {
        draw_condition_cloudy(CC_CONDITION_IMG_SIZE, 35, 85, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_FOG, false) || string_contains(condition, CC_HAZE, false)) {
        draw_condition_fog(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_SNOW, false)) {
        draw_condition_snow(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_THUNDER, false)) {
        draw_condition_storm(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else if (string_contains(condition, CC_RAIN, false) || string_contains(condition, CC_SHOWERS, false)) {
        draw_condition_rain(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    } else {
        draw_condition_unknown(CC_CONDITION_IMG_SIZE, 60, 80, CC_CONDITION_IMG_SCALE);
    }
}

void draw_condition_clear(int size, int x, int y, uint32_t scale) {
    uint16_t *bitmap = get_image_data_from_fs(is_night ? FILE_MOON_BIN : FILE_SUN_BIN, ICON_IMG_SIZE);
    draw_bitmap(bitmap, size, x, y, scale);
    free(bitmap);
}

void draw_condition_cloudy(int size, int x, int y, uint32_t scale) {
    uint16_t *bitmap = get_image_data_from_fs(FILE_CLOUD_BIN, ICON_IMG_SIZE);
    draw_bitmap(bitmap, size, x, y, scale);
    draw_bitmap(bitmap, size, x + 30, y - 20, scale);
    draw_bitmap(bitmap, size, x + 60, y - 5, scale);
    free(bitmap);
}

void draw_condition_fog(int size, int x, int y, uint32_t scale) {
    uint16_t *bitmap = get_image_data_from_fs(FILE_FOG_BIN, ICON_IMG_SIZE);
    draw_bitmap(bitmap, size, x, y, scale);
    free(bitmap);
}

void draw_condition_partly_cloudy(int size, int x, int y, uint32_t scale) {
    uint16_t *sun_bitmap = get_image_data_from_fs(is_night ? FILE_MOON_BIN : FILE_SUN_BIN, ICON_IMG_SIZE);
    uint16_t *cloud_bitmap = get_image_data_from_fs(FILE_CLOUD_BIN, ICON_IMG_SIZE);
    draw_bitmap(sun_bitmap, size, x, y, scale);
    draw_bitmap(cloud_bitmap, size, x - 20, y + 10, scale);
    free(sun_bitmap);
    free(cloud_bitmap);
}

void draw_condition_rain(int size, int x, int y, uint32_t scale) {
    uint16_t *rain_bitmap = get_image_data_from_fs(FILE_RAIN_BIN, ICON_IMG_SIZE);
    draw_bitmap(rain_bitmap, size, x, y, scale);
    free(rain_bitmap);
}

void draw_condition_snow(int size, int x, int y, uint32_t scale) {
    uint16_t *snow_bitmap = get_image_data_from_fs(FILE_SNOW_BIN, ICON_IMG_SIZE);
    draw_bitmap(snow_bitmap, size, x, y, scale);
    free(snow_bitmap);
}

void draw_condition_storm(int size, int x, int y, uint32_t scale) {
    uint16_t *storm_bitmap = get_image_data_from_fs(FILE_STORM_BIN, ICON_IMG_SIZE);
    draw_bitmap(storm_bitmap, size, x, y, scale);
    free(storm_bitmap);
}

void draw_condition_unknown(int size, int x, int y, uint32_t scale) {
    uint16_t *unknown_bitmap = get_image_data_from_fs(FILE_UNKNOWN_BIN, ICON_IMG_SIZE);
    draw_bitmap(unknown_bitmap, size, x, y, scale);
    free(unknown_bitmap);
}

void clear_screen() {
    fill_rect_v_gradient(0, 0, tft->width, tft->height, DGX_NAVY(dgx_rgb_to_16), DGX_COPPER(dgx_rgb_to_16));
}

//*********************************************************************************************************************

void reset_response_buffer(char *response_buf, int *response_len) {
    memset(response_buf, 0, HTTP_GET_RESPONSE_BUFFER_SIZE);
    *response_len = 0;
}

bool get_bit_val_at_pos(uint8_t num, uint8_t bit_pos) {
    // Cast the first operation result as uint8_t because C converts to int during shift operations.
    return ((uint8_t) num << bit_pos) >> 7; 
}

//*********************************************************************************************************************

bool add_test_alert(const char *event, const char *severity) {
    // Get the current local timestamp.
    time_t timestamp;
    time(&timestamp);
    struct tm utc_date_time_tm = {0};
    if (gmtime_r(&timestamp, &utc_date_time_tm) == NULL) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_ERROR, "Error setting UTC time.");
        return false;
    }
    time_t time_utc = mktime(&utc_date_time_tm);
    time_utc += 1;
    if (gmtime_r(&time_utc, &utc_date_time_tm) == NULL) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_ERROR, "Error setting UTC time.");
        return false;
    }
    char date_time[DATE_TIME_STRING_BUFFER_SIZE];
    strftime(date_time, DATE_TIME_STRING_BUFFER_SIZE, DATE_TIME_STRING_FORMAT, &utc_date_time_tm);

    // Add the alert and reset the alert cycle timer so that we can see the new alert in full time.
    bool success = add_alert(event, date_time, severity);
    xTimerReset(alert_cycle_timer, 0);

    // Play EAS attention tone.
    xTaskNotifyGiveIndexed(eas_task_handle_1, 0);
    xTaskNotifyGiveIndexed(eas_task_handle_2, 0);

    return success;
}

bool add_alert(const char *event, const char *alert_exp_date_time, const char *severity) {
    struct AlertNode *new_node = (struct AlertNode*) calloc(1, sizeof(struct AlertNode));
    snprintf(new_node->data, MENU_NODE_DATA_BUFFER_SIZE, "%s", event);
    snprintf(new_node->severity, SEVERITY_BUFFER_SIZE, "%s", severity);
    new_node->expire_time = get_utc_time_t_from_str(alert_exp_date_time, DATE_TIME_STRING_FORMAT);
    
    if (alert_list.root_node == NULL) {
        alert_list.root_node = new_node;
    } else {
        new_node->next_node = alert_list.root_node;
        alert_list.root_node = new_node;
    }
    alert_list.curr_node = alert_list.root_node;

    ESP_LOGI(TAG_INFO, "Added alert %s", new_node->data);

    return true;
}

bool remove_expired_alerts() {
    if (alert_list.root_node == NULL) {
        return false;
    }

    struct AlertNode *temp_node = alert_list.root_node;
    struct AlertNode *placeholder_node, *prev_node = NULL;
    while (temp_node != NULL) {
        if (curr_time_utc <= temp_node->expire_time) {
            prev_node = temp_node;
            temp_node = temp_node->next_node;
            continue;
        }
        placeholder_node = temp_node->next_node;
        if (alert_list.root_node == temp_node) {
            free(temp_node);
            temp_node = NULL;
            if (alert_list.curr_node == alert_list.root_node) {
                alert_list.curr_node = placeholder_node;
            }
            alert_list.root_node = placeholder_node;
        } else {
            prev_node->next_node = placeholder_node;
            free(temp_node);
            temp_node = NULL;
        }
        temp_node = placeholder_node;
    }

    return true;
}

//*********************************************************************************************************************

void set_int_temperature_val(int *temperature_val) {
    if (DEG_F) {
        *temperature_val = (*temperature_val * 1.8) + 32;
    }
    ESP_LOGI(TAG_INFO, "Int: %i", *temperature_val);
}

void set_float_temperature_val(float *temperature_val) {
    if (DEG_F) {
        *temperature_val = (*temperature_val * 1.8) + 32;
    }
    ESP_LOGI(TAG_INFO, "Float: %.f", *temperature_val);
}

bool update_time() {
    ESP_LOGI(TAG_INFO, "Updating time ...");

    // Get the current local timestamp.
    time_t local_timestamp;
    time(&local_timestamp);
    struct tm local_date_time_tm = {0};
    if (localtime_r(&local_timestamp, &local_date_time_tm) == NULL) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_ERROR, "Error setting local time.");
        return false;
    }

    // Check if it's currently past 6pm.
    is_night = local_date_time_tm.tm_hour >= PM_THRESHOLD;

    // Now get the UTC timestamp so we can keep track of it for the alerts.
    struct tm utc_date_time_tm = {0};
    if (gmtime_r(&local_timestamp, &utc_date_time_tm) == NULL) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_ERROR, "Error setting UTC time.");
        return false;
    }
    curr_time_utc = mktime(&utc_date_time_tm);

    // Update the local date and time strings to display.
    strftime(date, DATE_BUFFER_SIZE, "%m/%d/%Y", &local_date_time_tm);
    strftime(time_, TIME_BUFFER_SIZE, "%H:%M", &local_date_time_tm);

    ESP_LOGI(TAG_INFO, "Got time.");
    
    // Cleanup HTTP client
    esp_http_client_cleanup(client);

    return true;
}

bool update_alerts() {
    ESP_LOGI(TAG_INFO, "Deleting any expired alerts ...");
    if (remove_expired_alerts()) {
        ESP_LOGI(TAG_INFO, "Removed expired alerts.");
    }

    ESP_LOGI(TAG_INFO, "Getting alerts ...");

	size_t request_url_size = strlen(API_HOST) + strlen(NWS_API_ALERTS_URL_PART) + strlen(PARAM_START) 
        + strlen(NWS_API_URL_POINT_PARAM) + strlen(LATITUDE) + strlen(LAT_LONG_DELIM) + strlen(LONGITUDE) + 1;
    if (request_url_size > REQUEST_URL_BUFFER_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for init exceeds the allowed number of bytes.");
    }

    char request_url[REQUEST_URL_BUFFER_SIZE];
    memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s%s%s%s%s%s%s", API_HOST, NWS_API_ALERTS_URL_PART, PARAM_START, 
        NWS_API_URL_POINT_PARAM, LATITUDE, LAT_LONG_DELIM, LONGITUDE);

    // Configure HTTP client50000
    config.url = request_url;

    // Initialize HTTP client and perform the HTTP GET request
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);

    cJSON *cjson_root = cJSON_Parse(http_response_buffer);
    if (cjson_root == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing alerts JSON.");
        cJSON_Delete(cjson_root);        
        return false;
    }

    // Get the features array that contains the alerts and parse them into alerts.
    cJSON *cjson_features = cJSON_GetObjectItem(cjson_root, JSON_FEATURES);
    if (cjson_features == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing features from alerts JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }

    bool alerts_added = false;
    char severity[SEVERITY_BUFFER_SIZE];
    char alert[MENU_NODE_DATA_BUFFER_SIZE];
    char alert_exp_date_time[DATE_TIME_STRING_BUFFER_SIZE];
    cJSON *cjson_arr_item_obj = NULL;
    cJSON *cjson_obj_item;
    int i;
    int count = cJSON_GetArraySize(cjson_features);
    for (i = 0; i < count ; i++) {
        // Get the severity.
        cjson_arr_item_obj = cJSON_GetArrayItem(cjson_features, i);
        if (cjson_arr_item_obj == NULL) {
            error_bits = ERROR_P_JSON;
            ESP_LOGE(TAG_ERROR, "Error parsing features array element from alert JSON.");
            continue;
        }
        cjson_obj_item = cJSON_GetObjectItem(cjson_arr_item_obj, JSON_SEVERITY);
        if (cjson_obj_item == NULL) {
            error_bits = ERROR_P_JSON;
            ESP_LOGE(TAG_ERROR, "Error parsing features array element from alert JSON.");
            continue;
        }
        memset(severity, 0, SEVERITY_BUFFER_SIZE);
        snprintf(severity, SEVERITY_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_obj_item));

        // Determine what to do depending on the severity of the alert.
        if (compare_strings(JSON_EXTREME, severity, -1) || compare_strings(JSON_SEVERE, severity, -1) 
            || compare_strings(JSON_MODERATE, severity, -1)) {
            // Get the event.
            cjson_obj_item = cJSON_GetObjectItem(cjson_arr_item_obj, JSON_EVENT);

            memset(alert, 0, MENU_NODE_DATA_BUFFER_SIZE);
            if (cjson_obj_item == NULL) {
                // In this case, there's an alert but we failed to parse it. Alerts are important, so lets add a 
                // placeholder value here instead.
                snprintf(alert, MENU_NODE_DATA_BUFFER_SIZE, "%s", GENERIC_ALERT_EVENT);
            } else {
                snprintf(alert, MENU_NODE_DATA_BUFFER_SIZE, cJSON_GetStringValue(cjson_obj_item), 
                    MENU_NODE_DATA_BUFFER_SIZE);
            }

            // Get the expiration date and time.
            cjson_obj_item = cJSON_GetObjectItem(cjson_arr_item_obj, JSON_DATE_TIME);
            if (cjson_obj_item == NULL) {
                error_bits = ERROR_P_JSON;
                ESP_LOGE(TAG_ERROR, "Error parsing features array element from alert JSON.");
                continue;
            }
            memset(alert_exp_date_time, 0, DATE_TIME_STRING_BUFFER_SIZE);
            snprintf(alert_exp_date_time, DATE_TIME_STRING_BUFFER_SIZE, cJSON_GetStringValue(cjson_obj_item), 
                DATE_TIME_STRING_BUFFER_SIZE);

            // Finally, add the alert to the alert list.
            add_alert(alert, alert_exp_date_time, severity);
            alerts_added = true;
        }
    }

    cJSON_Delete(cjson_root);

    ESP_LOGI(TAG_INFO, "Finished updating alerts: %lu B", esp_get_free_heap_size());

    return alerts_added;
}

bool update_curr_conditions() {
    ESP_LOGI(TAG_INFO, "Getting current conditions ...");

    // Set the temperature char.
    temperature_char = DEG_F ? 'F' : 'C';

    // Set up the request URL.
	size_t request_url_size = strlen(API_HOST) + strlen(NWS_API_POINTS_URL_PART) + strlen(LATITUDE) 
        + strlen(LAT_LONG_DELIM) + strlen(LONGITUDE) + 1;
    if (request_url_size > REQUEST_URL_BUFFER_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for init exceeds the allowed number of bytes.");
    }
	char request_url[REQUEST_URL_BUFFER_SIZE];
	memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s%s%s%s%s", API_HOST, NWS_API_POINTS_URL_PART, LATITUDE, 
        LAT_LONG_DELIM, LONGITUDE);

    // Configure HTTP client
    config.url = request_url;

    // Initialize HTTP client and perform the HTTP GET request
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);

    // Get the properties object and it's value.
    cJSON *cjson_root = cJSON_Parse(http_response_buffer);
    
    char ob_stations_url_raw[OB_STN_BUFFER_SIZE];
    memset(ob_stations_url_raw, 0, OB_STN_BUFFER_SIZE);
    cJSON *cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing initial properties JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }
    char *val = cJSON_GetStringValue(cJSON_GetObjectItem(cjson_properties, JSON_OBSERVATION_STATIONS)); 
    if (val == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing observation stations JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }
    snprintf(ob_stations_url_raw, OB_STN_BUFFER_SIZE, "%s", val);

    cJSON_Delete(cjson_root);

	// Make another request to actually get the current observations now that we have the URL.
	size_t ob_stations_url_size = OB_STN_BUFFER_SIZE + strlen(PARAM_START) + strlen(NWS_API_URL_PART_LIMIT_PARAM) + 
        strlen(NWS_API_ITEM_LIMIT) + strlen(NWS_API_ITEM_LIMIT) + 1;
    if (ob_stations_url_size > REQUEST_URL_BUFFER_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for ob stations exceeds the allowed number of bytes.");
        return false;
    }
	memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s%s%s%s", ob_stations_url_raw, PARAM_START, 
        NWS_API_URL_PART_LIMIT_PARAM, NWS_API_ITEM_LIMIT);
    
    // Initialize HTTP client and perform the HTTP GET request
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);
    
    // Get the observation stations.
    cjson_root = cJSON_Parse(http_response_buffer);
    
    char stations_request_str_raw[DESC_BUFFER_SIZE];
    memset(stations_request_str_raw, 0, DESC_BUFFER_SIZE);
    cJSON *cjson_ob_stations_array = cJSON_GetObjectItem(cjson_root, JSON_OBSERVATION_STATIONS);
    if (cjson_ob_stations_array == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing observation station properties JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON *cjson_ob_station = cJSON_GetArrayItem(cjson_ob_stations_array, 0);
    if (cjson_ob_station == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing observation station arrays JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }
    snprintf(stations_request_str_raw, DESC_BUFFER_SIZE, "%s", cjson_ob_station->valuestring);
    
    cJSON_Delete(cjson_root);

    // Finally, make a request to get the latest observations from the station.
	size_t stations_request_url_size = strlen(stations_request_str_raw) + strlen(NWS_API_OBSERVATIONS_LATEST_URL_PART) 
        + 1;
    if (stations_request_url_size > REQUEST_URL_BUFFER_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for stations exceeds the allowed number of bytes.");
        return false;
    }
	memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s%s", stations_request_str_raw, 
        NWS_API_OBSERVATIONS_LATEST_URL_PART);

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);

    // Initialize HTTP client and perform the HTTP GET request
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG_INFO, "Parsing current conditions ...");

    // Get data for the current conditions.
    cjson_root = cJSON_Parse(http_response_buffer);

    cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing post properties JSON.");
        cJSON_Delete(cjson_root);
        return false;
    }

    // Temperature
    int temperature = main_state_list.root_node->cc.temperature;
    cJSON *cjson_temperature = cJSON_GetObjectItem(cjson_properties, JSON_TEMPERATURE);
    if (cjson_temperature == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing temperature from JSON.");
    } else {
        cJSON *value_obj = cJSON_GetObjectItem(cjson_temperature, JSON_VALUE);
        if (cJSON_IsNumber(value_obj)) {
            temperature = cJSON_GetNumberValue(value_obj);
            set_int_temperature_val(&temperature);
        }
    }

    // Description
    char desc_buffer[DESC_BUFFER_SIZE];
    cJSON *cjson_desc = cJSON_GetObjectItem(cjson_properties, JSON_TEXT_DESCRIPTION);
    if (cjson_desc == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing description from JSON.");
    } else {
        memset(desc_buffer, 0, DESC_BUFFER_SIZE);
        snprintf(desc_buffer, DESC_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_desc));
        ESP_LOGI(TAG_INFO, "Set description");
    }

    // Wind speed
    float wind_speed = main_state_list.root_node->cc.wind_speed;
    cJSON *cjson_wind_speed = cJSON_GetObjectItem(cjson_properties, JSON_WIND_SPEED);
    if (cjson_wind_speed == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing wind speed from JSON.");
    } else {
        wind_speed = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_wind_speed, JSON_VALUE));
        ESP_LOGI(TAG_INFO, "Set wind speed");
    }

    // Wind direction
    float wind_dir_f = main_state_list.root_node->cc.wind_dir_f;
    char wind_dir_buffer[WIND_DIR_BUFFER_SIZE];
    cJSON *cjson_wind_dir = cJSON_GetObjectItem(cjson_properties, JSON_WIND_DIR);
    if (cjson_wind_dir == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing wind direction from JSON.");
    } else {
        wind_dir_f = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_wind_dir, JSON_VALUE));
        ESP_LOGI(TAG_INFO, "Wind direction");
    }

    // Dew point
    float dew_point = main_state_list.root_node->cc.dew_point;
    cJSON *cjson_dew_point = cJSON_GetObjectItem(cjson_properties, JSON_DEWPOINT);
    if (cjson_dew_point == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing dew point from JSON.");
    } else {
        dew_point = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_dew_point, JSON_VALUE));
        set_float_temperature_val(&dew_point);
        ESP_LOGI(TAG_INFO, "Set dew point");
    }

    // Visibility
    int visibility = main_state_list.root_node->cc.visibility;
    cJSON *cjson_visibility = cJSON_GetObjectItem(cjson_properties, JSON_VISIBILITY);
    if (cjson_visibility == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing visibility from JSON.");
    } else {
        visibility = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_visibility, JSON_VALUE)) / 1609;
        ESP_LOGI(TAG_INFO, "Set visibility.");
    }

    cJSON_Delete(cjson_root);

    // Calculate the wind direction string
    memset(wind_dir_buffer, 0, WIND_DIR_BUFFER_SIZE);

    if (wind_dir_f <= 15.0f || wind_dir_f > 345.0f) {           // North
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_N);
    } else if (wind_dir_f > 15.0f && wind_dir_f <= 75.0f) {     // North-west
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_NW);
    } else if (wind_dir_f > 75.0f && wind_dir_f <= 105.0f) {    // West
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_W);
    } else if (wind_dir_f > 105.0f && wind_dir_f <= 165.0f) {   // South-west
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_SW);
    } else if (wind_dir_f > 165.0f && wind_dir_f <= 195.0f) {   // South
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_S);
    } else if (wind_dir_f > 195.0f && wind_dir_f <= 255.0f) {   // South-east
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_SE);
    } else if (wind_dir_f > 255.0f && wind_dir_f <= 285.0f) {   // East
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_E);
    } else {                                                            // North-east
        snprintf(wind_dir_buffer, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_NE);
    }

    // Update the current conditions main state node with the retrieved data.
    snprintf(main_state_list.root_node->cc.desc, DESC_BUFFER_SIZE, "%s", desc_buffer);
    main_state_list.root_node->cc.dew_point = dew_point;
    main_state_list.root_node->cc.temperature = temperature;
    main_state_list.root_node->cc.visibility = visibility;
    snprintf(main_state_list.root_node->cc.wind_dir, WIND_DIR_BUFFER_SIZE, "%s", wind_dir_buffer);
    main_state_list.root_node->cc.wind_speed = wind_speed;

    ESP_LOGI(TAG_INFO, "Finished updating current conditions: %lu B", esp_get_free_heap_size());

    return true;
}

bool update_forecast() {
    // Clear the forecasts we currently have stored.
    ESP_LOGI(TAG_INFO, "Removing existing forecast main states ...");
    remove_forecast_main_states();

    // Configure the HTTP client.c *
    ESP_LOGI(TAG_INFO, "Getting new forecasts ...");

	size_t request_url_size = strlen(API_HOST) + strlen(NWS_API_POINTS_URL_PART) + strlen(LATITUDE) 
        + strlen(LAT_LONG_DELIM) + strlen(LONGITUDE) + 1;
    if (request_url_size > REQUEST_URL_BUFFER_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for init exceeds the allowed number of bytes.");
        return false;
    }

	char request_url[REQUEST_URL_BUFFER_SIZE];
	memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s%s%s%s%s", API_HOST, NWS_API_POINTS_URL_PART, LATITUDE, 
        LAT_LONG_DELIM, LONGITUDE);

    // Configure the HTTP client.
    config.url = request_url;

    // Initialize HTTP client and perform the HTTP GET request
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);

    // Get the forecasts URL.
    cJSON *cjson_root = cJSON_Parse(http_response_buffer);
    if (cjson_root == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON *cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON *cjson_forecast = cJSON_GetObjectItem(cjson_properties, JSON_FORECAST);
    if (cjson_forecast == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast item from forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }
    memset(request_url, 0, REQUEST_URL_BUFFER_SIZE);
    snprintf(request_url, REQUEST_URL_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_forecast));
    
    cJSON_Delete(cjson_root);

    // Initialize HTTP client and perform the HTTP GET request
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_cleanup(client);

    cjson_root = cJSON_Parse(http_response_buffer);
    if (cjson_root == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast response from forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }
    cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast response properties from forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON *cjson_periods_array = cJSON_GetObjectItem(cjson_properties, JSON_PERIODS);
    if (cjson_periods_array == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse periods array from forecast JSON");
        cJSON_Delete(cjson_root);
        return false;
    }

    // Iterate through the periods so we can get our extended forecasts.
    char desc_buffer[DESC_BUFFER_SIZE], day_buffer[DAY_BUFFER_SIZE + 16], temp_day_buffer[DAY_BUFFER_SIZE + 16],
        start_time_buffer[DATE_TIME_STRING_BUFFER_SIZE], end_time_buffer[DATE_TIME_STRING_BUFFER_SIZE];
    uint8_t high_temp, low_temp = 0;
    struct ExtForecastNode *ext_forecasts_root_node = NULL;

    cJSON *cjson_array_item = NULL;
    cJSON *cjson_temp_array_item = NULL;
    cJSON *cjson_array_obj_item = NULL;
    int arr_count = cJSON_GetArraySize(cjson_periods_array);
    int count = arr_count > MAX_EF_COUNT ? MAX_EF_COUNT : arr_count;  // No more than 5
    int i;
    uint8_t itr_count = 0;
    for (i = 0; i < count; i++) {
        if (i == arr_count) {
            break;
        }
        
        // Get the object at array index i.
        cjson_array_item = cJSON_GetArrayItem(cjson_periods_array, i);
        if (cjson_array_item == NULL) {
            error_bits = ERROR_P_JSON;
            ESP_LOGI(TAG_ERROR, "Failed parsing periods array from object at index %d.", i);
            continue;
        }

        // If this is the first period, grab the forecast and add a main state node.
        if (i == 0) {
            cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_DETAILED_FORECAST);
            add_main_state(STATE_MAIN_LOCAL_FORECAST, cJSON_GetStringValue(cjson_array_obj_item), NULL);
        }

        // Get the day. If it's "Tonight," then we need to grab the temperature, add it to the current condition's
        // low_temp field, and move on.
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_NAME);
        snprintf(day_buffer, DAY_BUFFER_SIZE + 16, "%s", cJSON_GetStringValue(cjson_array_obj_item));
        if (compare_strings(day_buffer, JSON_TONIGHT, -1)) {
            cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_TEMPERATURE);
            main_state_list.root_node->cc.low_temp = cJSON_GetNumberValue(cjson_array_obj_item);
            count++;
            continue;
        }

        // If this is the first period, we need to add the high temp ir low temp to the current conditions, then
        // move on.
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_IS_DAYTIME);
        bool is_day_time = cJSON_IsTrue(cjson_array_obj_item);
        if (i == 0) {
            cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_TEMPERATURE);
            if (is_day_time) {
                main_state_list.root_node->cc.high_temp = cJSON_GetNumberValue(cjson_array_obj_item);
            } else {
                main_state_list.root_node->cc.low_temp = cJSON_GetNumberValue(cjson_array_obj_item);
            }
            count++;
            continue;
        } else if (!is_day_time) {  // Skip iterating over night periods, we'll do this ourselves.
            count++;
            continue;
        }

        // High temp
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_TEMPERATURE);
        high_temp = cJSON_GetNumberValue(cjson_array_obj_item);

        // Short forecast
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_SHORT_FORECAST);
        snprintf(desc_buffer, DESC_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_array_obj_item));

        // Start time (local)
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_START_TIME);
        snprintf(start_time_buffer, DATE_TIME_STRING_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_array_obj_item));

        // End time (local) 
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_END_TIME);
        snprintf(end_time_buffer, DATE_TIME_STRING_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_array_obj_item));

        // We now need to iterate through until we get to the corresponding night time period for this day. We need to
        // grab the low tempearture from it.
        int j;
        for (j = i + 1; j < count; j++) {
            cjson_temp_array_item = cJSON_GetArrayItem(cjson_periods_array, j);
            cjson_array_obj_item = cJSON_GetObjectItem(cjson_temp_array_item, JSON_NAME);
            snprintf(temp_day_buffer, DAY_BUFFER_SIZE + 16, "%s", cJSON_GetStringValue(cjson_array_obj_item));
            if (string_contains(temp_day_buffer, JSON_NIGHT, true) && 
                    string_contains(temp_day_buffer, day_buffer, true)) {
                cjson_array_obj_item = cJSON_GetObjectItem(cjson_temp_array_item, JSON_TEMPERATURE);
                low_temp = cJSON_GetNumberValue(cjson_array_obj_item);
                break;
            }
        }

        // Add a new extended forecast node
        struct ExtForecastNode *new_node = create_ext_forecast_node(desc_buffer, day_buffer, high_temp, low_temp, 
            start_time_buffer, end_time_buffer);
        if (ext_forecasts_root_node == NULL) {
            ext_forecasts_root_node = new_node;
        } else {
            ext_forecasts_root_node->next_node = new_node;
        }

        // Add a forecast main state node every 2 forecasts or if we were only able to retrieve 1.
        if (++itr_count == 2 || count == 1) {
            add_main_state(STATE_MAIN_FORECAST, NULL, ext_forecasts_root_node);
            ext_forecasts_root_node = NULL;
            itr_count = 0;
        }
    }

    cJSON_Delete(cjson_root);

    ESP_LOGI(TAG_INFO, "Finished updating forecasts: %lu B", esp_get_free_heap_size());

    return true;
}

bool update_seven_seg() {
    // Get the bits from the current error code.
    bool error_arr[4];
    uint8_t i = 0;
    for (i = 0; i < 4; i++) {
        error_arr[i] = get_bit_val_at_pos(error_bits, 4 + i);
    }

    // We need to output the current error code serially to the register.
    uint8_t register_val = 0x00;
	
	uint8_t seven_seg_a = (!error_arr[1] && !error_arr[3]) || (!error_arr[0] && error_arr[2]) 
		|| (error_arr[1] && error_arr[2]) || (error_arr[0] && !error_arr[3]) 
		|| (!error_arr[0] && error_arr[1] && error_arr[3]) || (error_arr[0] && !error_arr[1] && !error_arr[2]);
	uint8_t seven_seg_b = (!error_arr[0] && !error_arr[1]) || (!error_arr[1] && !error_arr[3])
		|| (!error_arr[0] && !error_arr[2] && !error_arr[3]) || (!error_arr[0] && error_arr[2] && error_arr[3]) 
		|| (error_arr[0] && !error_arr[2] && error_arr[3]);
	uint8_t seven_seg_c = (!error_arr[0] && !error_arr[2]) || (!error_arr[0] && error_arr[3]) 
		|| (!error_arr[2] && error_arr[3]) || (!error_arr[0] && error_arr[1]) || (error_arr[0] && !error_arr[1]);
	uint8_t seven_seg_d = (error_arr[0] && !error_arr[2]) 
		|| (!error_arr[0] && !error_arr[1] && !error_arr[3]) || (!error_arr[1] && error_arr[2] && error_arr[3])
		|| (error_arr[1] && !error_arr[2] && error_arr[3]) || (error_arr[1] && error_arr[2] && !error_arr[3]);
	uint8_t seven_seg_e = (!error_arr[1] && !error_arr[3]) || (error_arr[2] && !error_arr[3]) 
		|| (error_arr[0] && error_arr[2]) || (error_arr[0] && error_arr[1]);
	uint8_t seven_seg_f =  (!error_arr[2] && !error_arr[3]) || (error_arr[1] && !error_arr[3]) 
		|| (error_arr[0] && !error_arr[1]) || (error_arr[0] && error_arr[2]) 
		|| (!error_arr[0] && error_arr[1] && !error_arr[2]);
	uint8_t seven_seg_g = (!error_arr[1] && error_arr[2]) || (error_arr[2] && !error_arr[3]) 
		|| (error_arr[0] && !error_arr[1]) || (error_arr[0] && error_arr[3]) 
		|| (!error_arr[0] && error_arr[1] && !error_arr[2]);
	
	register_val |= seven_seg_g;
	register_val |= (seven_seg_f << 1);
	register_val |= (seven_seg_a << 2);
	register_val |= (seven_seg_b << 3);
	register_val |= (seven_seg_e << 4);
	register_val |= (seven_seg_d << 5);
	register_val |= (seven_seg_c << 6);

    uint8_t reg_vals[1] = {register_val};

    ESP_ERROR_CHECK(shiftregister_gpio_transfer(&reg_config, reg_vals, sizeof(reg_vals)));

    return true;
}

void update_menu(bool menu, bool menu_up, bool menu_down) {
	// Move the cursor and check if we need to display a new section of the menu.
	if (menu_up && (curr_menu_node->prev_sibling != NULL)) {
		curr_menu_node = curr_menu_node->prev_sibling;
		curr_menu_pos--;
		if (curr_menu_pos < 0) {
			// Get the node 4 spots behind the current node and start the section with it.
            uint8_t starting_index = MENU_MAX_OPTIONS_DISPLAYED - 1;
			uint8_t i = starting_index;
			struct MenuNode *temp_node = curr_menu_node;
			while (i > 0 && temp_node->prev_sibling != NULL) {
				temp_node = temp_node->prev_sibling;
				i--;
			}				
			curr_section_lead_node = temp_node;
			curr_menu_pos = starting_index;
            ESP_LOGI(TAG_INFO, "curr_section_lead_node: %s", curr_section_lead_node->data);
		}
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
	} else if (menu_down && (curr_menu_node->next_sibling != NULL)) {
		curr_menu_node = curr_menu_node->next_sibling;
		curr_menu_pos++;
		if (curr_menu_pos == MENU_MAX_OPTIONS_DISPLAYED) {
			curr_section_lead_node = curr_menu_node;
            curr_menu_pos = 0;
		}
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
	} else if (menu) {
		if (curr_menu_node->first_child != NULL) {
			curr_parent_node = curr_menu_node;
			curr_menu_node = curr_menu_node->first_child;
			curr_section_lead_node = curr_menu_node;
			curr_menu_pos = 0;
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
		} else {
			perform_menu_action();
		}
	}
}

void update_curr_alert() {
    if (alert_list.root_node == NULL) {
        return;
    }
    alert_list.curr_node = alert_list.curr_node->next_node;
    if (alert_list.curr_node == NULL) {
        alert_list.curr_node = alert_list.root_node;
    }
    
    xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
}

//*********************************************************************************************************************

void draw_cc_view() {
    // Clear the screen.
    clear_screen();

    // Draw the header rectangle
    draw_header_rect();

    // Draw the header text
    draw_header_string(HEADER_CURR_CONDITIONS);

    // Draw container rectangle
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS, CC_CONTAINER_Y_POS, CC_CONTAINER_W, CC_CONTAINER_H, 
        DGX_DARKCYAN(dgx_rgb_to_16));
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS + 2, CC_CONTAINER_Y_POS + 2, CC_CONTAINER_W - 4, CC_CONTAINER_H - 4, 
        CC_CONTAINER_COLOR);

    // Write the date and time.
    draw_date_time();

    // Write the high.
    char stat_buff[CC_STAT_BUFFER_SIZE];
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%d", main_state_list.root_node->cc.high_temp);
    draw_high_arrow_bitmap(32, CC_HIGH_ARR_X_POS, CC_ARR_Y_POS, 1);
    draw_string(stat_buff, CC_HIGH_TEMP_X_POS, CC_HIGH_LOW_TEMP_Y_POS, DGX_WHITE(dgx_rgb_to_16), 
        CC_HIGH_LOW_TEMP_FONT_SCALE);

    // Write the low.
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%d", main_state_list.root_node->cc.low_temp);
    draw_low_arrow_bitmap(32, CC_LOW_ARR_X_POS, CC_ARR_Y_POS, 1);
    draw_string(stat_buff, CC_LOW_TEMP_X_POS, CC_HIGH_LOW_TEMP_Y_POS, DGX_WHITE(dgx_rgb_to_16), 
        CC_HIGH_LOW_TEMP_FONT_SCALE);

    // Write the temperature.
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%d %c", main_state_list.root_node->cc.temperature, temperature_char);
    draw_string(stat_buff, CC_TEMP_X_POS, CC_TEMP_Y_POS, DGX_WHITE(dgx_rgb_to_16), 3);

    // Write the wind data.
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%s %.1f mph", main_state_list.root_node->cc.wind_dir, 
        main_state_list.root_node->cc.wind_speed);
    draw_string(stat_buff, CC_WIND_X_POS, CC_WIND_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the dew point
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%.1f %c", main_state_list.root_node->cc.dew_point, temperature_char);
    draw_string(stat_buff, CC_DEW_POINT_X_POS, CC_DEW_POINT_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);
    draw_dew_point_bitmap(CC_DEW_POINT_IMG_SIZE, CC_DEW_POINT_X_POS - 20, CC_DEW_POINT_IMG_Y_POS, 1);

    // Write the visibility.
    memset(stat_buff, 0, CC_STAT_BUFFER_SIZE);
    snprintf(stat_buff, CC_STAT_BUFFER_SIZE, "%d mi", main_state_list.root_node->cc.visibility);
    draw_string(stat_buff, CC_VISIBILITY_X_POS, CC_VISIBILITY_Y_POS, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the current description.
    draw_string(main_state_list.root_node->cc.desc, CC_DESC_X_POS, CC_DESC_Y_POS, DGX_WHITE(dgx_rgb_to_16), 3);

    // Determine what condition to draw based on the description.
    draw_condition_bitmap(main_state_list.root_node->cc.desc);

    ESP_LOGI(TAG_INFO, "Finished drawing CC: %lu B", esp_get_free_heap_size());
}

void draw_local_forecast_view() {
    // Clear the screen.
    clear_screen();

    // Draw the header rectangle
    draw_header_rect();

    // Draw the header text
    draw_header_string(HEADER_LOCAL_FORECAST);
    
    // Draw the NOAA logo. 
    draw_noaa_logo_bitmap(CC_CONDITION_IMG_SIZE, NOAA_LOGO_IMG_X_POS, NOAA_LOGO_IMG_Y_POS, 1);

    // Write the date and time.
    draw_date_time();

    // Draw the container.
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS, CC_CONTAINER_Y_POS, CC_CONTAINER_W, CC_CONTAINER_H, 
        DGX_DARKCYAN(dgx_rgb_to_16));
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS + 2, CC_CONTAINER_Y_POS + 2, CC_CONTAINER_W - 4, CC_CONTAINER_H - 4, 
        CC_CONTAINER_COLOR);

    // Draw the forecast data.
    draw_string_wrapped(main_state_list.curr_node->local_forecast, LF_TEXT_X_POS, LF_TEXT_Y_POS, 
        DGX_WHITE(dgx_rgb_to_16), LF_TEXT_FONT_SCALE, LF_TEXT_BOUND_W, CC_CONTAINER_H);
}

void draw_ext_forecast_view() {
    // Clear the screen.
    clear_screen();

    // Draw the header rectangle
    draw_header_rect();

    // Draw the header text
    draw_header_string(HEADER_EXT_FORECAST);

    // Write the date and time.
    draw_date_time();

    // Draw the forecast data.
    uint8_t i = 0;
    int day_x_pos, container_x_pos, high_temp_x_pos, low_temp_x_pos;
    struct ExtForecastNode *temp_node = main_state_list.curr_node->ext_forecasts_root_node;
    char temperature_buffer[EF_TEMP_BUFFER_SIZE];
    while (temp_node != NULL) {
        // Draw container rectangle
        container_x_pos = i == 0 ? EF_DAY_1_CONTAINER_X_POS : EF_DAY_2_CONTAINER_X_POS;
        dgx_fill_rectangle(tft, container_x_pos, EF_CONTAINER_Y_POS, EF_CONTAINER_W + 4, EF_CONTAINER_H + 4, 
            DGX_DARKCYAN(dgx_rgb_to_16));
        dgx_fill_rectangle(tft, container_x_pos + 2, EF_CONTAINER_Y_POS + 2, EF_CONTAINER_W, EF_CONTAINER_H, 
            CC_CONTAINER_COLOR);

        // Write the day.
        day_x_pos = i == 0 ? EF_DAY_1_DAY_X_POS : EF_DAY_2_DAY_X_POS;
        draw_string(temp_node->day, day_x_pos, EF_DAY_Y_POS, DGX_WHITE(dgx_rgb_to_16), 3);
        
        // Draw the icon.
        draw_ext_forecast_condition_bitmap(temp_node->desc, i);

        // Write the high temp.
        high_temp_x_pos = i == 0 ? EF_DAY_1_HIGH_TEMP_X_POS : EF_DAY_2_HIGH_TEMP_X_POS;
        snprintf(temperature_buffer, EF_TEMP_BUFFER_SIZE, "%d", temp_node->high_temp);
        draw_string(temperature_buffer, high_temp_x_pos, EF_TEMP_Y_POS, DGX_WHITE(dgx_rgb_to_16), EF_TEMP_FONT_SCALE);

        // Write the low temp.
        low_temp_x_pos = i == 0 ? EF_DAY_1_LOW_TEMP_X_POS : EF_DAY_2_LOW_TEMP_X_POS;
        snprintf(temperature_buffer, EF_TEMP_BUFFER_SIZE, "%d", temp_node->low_temp);
        draw_string(temperature_buffer, low_temp_x_pos, EF_TEMP_Y_POS, DGX_WHITE(dgx_rgb_to_16), EF_TEMP_FONT_SCALE);

        // Draw the high and low arrows.
        draw_low_arrow_bitmap(32, low_temp_x_pos - 35, EF_HIGH_LOW_ARR_Y_POS, 1);
        draw_high_arrow_bitmap(32, high_temp_x_pos - 35, EF_HIGH_LOW_ARR_Y_POS, 1);

        temp_node = temp_node->next_node;
        i += 1;
    }

    ESP_LOGI(TAG_INFO, "Finished drawing forecasts: %lu B", esp_get_free_heap_size());
}

void draw_menu_view() {
    // Always clear the screen before writing the menu
	clear_screen();

    // Draw the header rect.
    draw_header_rect();

	// Display menu header text
    draw_header_string(root_menu_node->data);

	// Show each of the options by traversing through the children of the current parent menu node.
	const char *cursor = " <";
	size_t cursor_size = strlen(cursor);
	size_t option_size = MENU_NODE_DATA_BUFFER_SIZE + cursor_size;
	char option[option_size];
	memset(option, 0, option_size);

	struct MenuNode *temp_curr_menu_node = curr_section_lead_node;
	uint8_t i = 0;
    uint32_t color;
	while (temp_curr_menu_node != NULL && i < MENU_MAX_OPTIONS_DISPLAYED) {
        snprintf(option, option_size, "%s", temp_curr_menu_node->data);
		if (temp_curr_menu_node == curr_menu_node) {
            strcat(option, cursor);
            color = DGX_ORANGE(dgx_rgb_to_16);
        } else {
            color = DGX_WHITE(dgx_rgb_to_16);
        }
        draw_string(option, MENU_OPTIONS_X, 60 + (MENU_OPTIONS_OFFSET * (i + 1)), color, 2);

		memset(option, 0, option_size);
		temp_curr_menu_node = temp_curr_menu_node->next_sibling;
		i++;
	}
}

void draw_si_view() {
    // Always clear the screen before drawing system info.
    clear_screen();

    // Draw header container
    draw_header_rect();

    // Draw header text
    draw_header_string(MENU_OPTION_SYSTEM_INFO);

    // Draw system info
    char heap_size[HEAP_SIZE_BUFFER_SIZE];
    uint32_t rem_heap_size = esp_get_free_heap_size();
    snprintf(heap_size, HEAP_SIZE_BUFFER_SIZE, "Free Heap Size: %lu B", rem_heap_size);
    draw_string(heap_size, SI_X_POS, SI_FREE_HEAP_SIZE_Y_POS, DGX_WHITE(dgx_rgb_to_16), SI_FONT_SCALE);

    memset(heap_size, 0, HEAP_SIZE_BUFFER_SIZE);
    uint32_t min_free_heap_size = esp_get_minimum_free_heap_size();
    snprintf(heap_size, HEAP_SIZE_BUFFER_SIZE, "Min Free Heap Size: %lu B", min_free_heap_size);
    draw_string(heap_size, SI_X_POS, SI_MIN_FREE_HEAP_SIZE_Y_POS, DGX_WHITE(dgx_rgb_to_16), SI_FONT_SCALE);
}

void draw_test_wrapped_text_view() {
    draw_header_rect();
    draw_header_string("Wrapped Text");
    
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS, CC_CONTAINER_Y_POS, CC_CONTAINER_W, CC_CONTAINER_H, 
        DGX_DARKCYAN(dgx_rgb_to_16));
    dgx_fill_rectangle(tft, CC_CONTAINER_X_POS + 2, CC_CONTAINER_Y_POS + 2, CC_CONTAINER_W - 4, CC_CONTAINER_H - 4, 
        CC_CONTAINER_COLOR);

    draw_string_wrapped(TEST_WRAPPED_TEXT, CC_CONTAINER_X_POS + 5, 60, DGX_WHITE(dgx_rgb_to_16), 2, 
        CC_CONTAINER_W - 4, CC_CONTAINER_H);
}

void draw_curr_alert() {
    draw_alert(alert_list.curr_node);
}

//*********************************************************************************************************************

void http_get_curr_conditions(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        int semaphore_taken = xSemaphoreTake(mutex, 0);
        while(semaphore_taken != pdTRUE) {
            semaphore_taken = xSemaphoreTake(mutex, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (update_curr_conditions() && curr_state == STATE_MAIN && main_state_list.curr_node->state == STATE_MAIN_CC) {
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
        }
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void http_get_forecast(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        int semaphore_taken = xSemaphoreTake(mutex, 0);
        while(semaphore_taken != pdTRUE) {
            semaphore_taken = xSemaphoreTake(mutex, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (update_forecast() && curr_state == STATE_MAIN) {
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
        }
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void http_get_alerts(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        int semaphore_taken = xSemaphoreTake(mutex, 0);
        while(semaphore_taken != pdTRUE) {
            semaphore_taken = xSemaphoreTake(mutex, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (update_alerts()) {
            // Reset the alert cycle timer so that we can see the new alert(s).
            xTimerReset(alert_cycle_timer, 0);
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
            
            // Play EAS attention tone.
            xTaskNotifyGiveIndexed(eas_task_handle_1, 0);
            xTaskNotifyGiveIndexed(eas_task_handle_2, 0);
        }
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void get_time(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        if (update_time() && curr_state == STATE_MAIN) {
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void check_error_bits(void *arg) { 
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        update_seven_seg();
        vTaskDelay(pdMS_TO_TICKS(10));
    }   
}

void check_and_update_display(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        if (curr_state == STATE_MAIN) {
            if (main_state_list.curr_node->state == STATE_MAIN_CC) {
                draw_cc_view();
            } else if (main_state_list.curr_node->state == STATE_MAIN_LOCAL_FORECAST) {
                draw_local_forecast_view();
            } else if (main_state_list.curr_node->state == STATE_MAIN_FORECAST) {
                draw_ext_forecast_view();
            }
        } else if (curr_state == STATE_MENU) {
            draw_menu_view();
        } else if (curr_state == STATE_SI) {
            draw_si_view();
        } else if (curr_state == STATE_WRAPPED_TEXT) {
            draw_test_wrapped_text_view();
        }
        if (alert_list.curr_node != NULL) {
            draw_curr_alert();  // We always want to show alerts on top of whatever screen we're on.
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void menu_btn_event(void *arg, void *usr_data) {
    if (curr_state == STATE_MENU) {
        update_menu(true, false, false);
    } else if (curr_state == STATE_SI || curr_state == STATE_WRAPPED_TEXT || curr_state == STATE_MAIN) {
        curr_state = STATE_MENU;
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
    }
}

void menu_up_btn_event(void *arg, void *usr_data) {
    if (curr_state == STATE_MENU) {
        update_menu(false, true, false);
    }
}

void menu_down_btn_event(void *arg, void *usr_data) {
    if (curr_state == STATE_MENU) {
        update_menu(false, false, true);
    }
}

void play_eas_tone_buzzer_1(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(EAS_TONE_DURATION_MS));

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void play_eas_tone_buzzer_2(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, BUZZER_DUTY);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);
        vTaskDelay(pdMS_TO_TICKS(EAS_TONE_DURATION_MS));

        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cycle_alerts(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        if (alert_list.root_node != NULL) {
            alert_list.curr_node = alert_list.curr_node->next_node;
            if (alert_list.curr_node == NULL) {
                alert_list.curr_node = alert_list.root_node;
            }
            xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cycle_main_states(void *arg) {
    while (true) {
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        main_state_list.curr_node = main_state_list.curr_node->next_node;
        xTaskNotifyGiveIndexed(refresh_display_task_handle, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void alert_cycle_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(cycle_alert_task_handler, 0);
}

void main_view_cycle_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(cycle_main_state_task_handle, 0);
}

void cc_refresh_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(cc_task_handle, 0);
}

void forecast_refresh_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(forecast_task_handle, 0);
}

void alert_refresh_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(alert_task_handle, 0);
}

void time_refresh_timer_callback(TimerHandle_t xTimer) {
    xTaskNotifyGiveIndexed(time_task_handle, 0);
}

//*********************************************************************************************************************

void init_pins() {
    // Seven segment register
    gpio_config_t reg_oe_config = {
        .pin_bit_mask = (1ULL << PIN_REG_OE),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    ESP_ERROR_CHECK(gpio_config(&reg_oe_config));

    gpio_config_t reg_rclk_config = {
        .pin_bit_mask = (1ULL << PIN_REG_RCLK),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    ESP_ERROR_CHECK(gpio_config(&reg_rclk_config));

    gpio_config_t reg_ser_config = {
        .pin_bit_mask = (1ULL << PIN_REG_SER),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    ESP_ERROR_CHECK(gpio_config(&reg_ser_config));

    ESP_ERROR_CHECK(shiftregister_gpio_init(&reg_config));

    // Incoming data LED
    gpio_config_t inc_led_config = {
        .pin_bit_mask = (1ULL << PIN_INC_DATA_LED),
        .mode = GPIO_MODE_OUTPUT,            // Set as input
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    ESP_ERROR_CHECK(gpio_config(&inc_led_config));

    // Buttons are configured differently than normal GPIO pins.
    menu_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_btn_gpio_config.gpio_num = PIN_MENU;
    menu_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_btn_gpio_config.disable_pull = true;
    button_handle_t menu_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&menu_btn_config, &menu_btn_gpio_config, &menu_btn));

    menu_up_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_up_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_up_btn_gpio_config.gpio_num = PIN_MENU_UP;
    menu_up_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_up_btn_gpio_config.disable_pull = true;

    button_handle_t menu_up_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&menu_up_btn_config, &menu_up_btn_gpio_config, &menu_up_btn));

    menu_down_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_down_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_down_btn_gpio_config.gpio_num = PIN_MENU_DOWN;
    menu_down_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_down_btn_gpio_config.disable_pull = true;
    button_handle_t menu_down_btn;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&menu_down_btn_config, &menu_down_btn_gpio_config, &menu_down_btn));

    ESP_ERROR_CHECK(iot_button_register_cb(menu_btn, BUTTON_SINGLE_CLICK, NULL, menu_btn_event, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(menu_up_btn, BUTTON_SINGLE_CLICK, NULL, menu_up_btn_event, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(menu_down_btn, BUTTON_SINGLE_CLICK, NULL, menu_down_btn_event, NULL));
}

bool init_display() {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t) GPIO_NUM_23,  // replace with your wiring
        .sclk_io_num = (gpio_num_t) GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    
    bus = dgx_spi_init_on_bus(SPI3_HOST, (gpio_num_t) GPIO_NUM_5, (gpio_num_t) GPIO_NUM_2, 40000000, 0);
    if (bus == NULL) {
        ESP_LOGE(TAG_ERROR, "dgx_spi_init failed.");
        return false;
    }
    tft = dgx_ili9341_init(bus, GPIO_NUM_4, 0, 16, DgxScreenRGB);
    if (tft == NULL) {
        ESP_LOGE(TAG_ERROR, "dgx_ili9341_init failed");
        bus->dispose(bus);
        return false;
    }

    dgx_ili9341_orientation(tft, DgxScreenLeftRight, DgxScreenBottomTop, true);
    
    clear_screen();

    return true;
}

bool init_menu() {
    // Parse the JSON string and create the menu nodes from it.
    cJSON *json = cJSON_Parse(MENU_JSON_STR);
    if (json == NULL) {
        ESP_LOGE(TAG_ERROR, "cJSON_Parse() was NULL.");
        return false;    
    }

    json = json->child;
    if (json == NULL) {
        ESP_LOGE(TAG_ERROR, "json->child was NULL.");
        return false;    
    }
    ESP_LOGI(TAG_INFO, "Successfully parsed menu JSON.");

    root_menu_node = create_menu_nodes(json, NULL, false);
    ESP_LOGI(TAG_INFO, "Successfully created menu nodes.");

	curr_parent_node = root_menu_node;
	curr_section_lead_node = curr_parent_node->first_child;
	curr_menu_node = curr_section_lead_node;
	curr_menu_pos = 0;

    return true;
}

void init_http_client_config() {
    // Initialize the HTTP client config.
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;
}

void init_buzzers() {
    // Buzzer 1
    ledc_timer_config_t buzzer_1_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = EAS_TONE_FREQ_1,
    };
    ledc_channel_config_t buzzer_1_channel = {
        .gpio_num = PIN_BUZZER_1,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&buzzer_1_config));
    ESP_ERROR_CHECK(ledc_channel_config(&buzzer_1_channel));

    // Buzzer 2
    ledc_timer_config_t buzzer_2_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = EAS_TONE_FREQ_2,
    };
    ledc_channel_config_t buzzer_2_channel = {
        .gpio_num = PIN_BUZZER_2,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&buzzer_2_config));
    ESP_ERROR_CHECK(ledc_channel_config(&buzzer_2_channel));
}

void init_clock() {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");

    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG_ERROR, "SNTP sync timed out\n");
        return;
    }

    setenv("TZ", TIME_ZONE, 1);
    tzset();
}

void init_fs() {
    conf.base_path = BASE_PATH;
    conf.partition_label = PARTITION_NAME;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
}

void init_main_states() {
    struct MainStateNode *cc_node = (struct MainStateNode*) calloc(1, sizeof(struct MainStateNode));
    struct CurrentConditions cc = {0};
    cc_node->cc = cc;
    cc_node->ext_forecasts_root_node = NULL;
    cc_node->next_node = NULL;
    cc_node->state = STATE_MAIN_CC;

    main_state_list.root_node = cc_node;
    main_state_list.curr_node = main_state_list.root_node;
}

//*********************************************************************************************************************

void handle_init_error(const char *error_msg) {
    ESP_LOGE(TAG_ERROR, "%s", error_msg);
    esp_restart();
}

void app_main(void) {
    // Initialize GPIO pins
    init_pins();

    // Initialize buzzers
    init_buzzers();

    // Create and initialize the display bus
    if (!init_display()) {
        handle_init_error("Failed to initialize display.");
    }

    // Draw the footer string.
    draw_string(FOOTER_WS32, 240, 220, DGX_YELLOW(dgx_rgb_to_16), 1);
    draw_string(FOOTER_BY, 240, 230, DGX_WHITE(dgx_rgb_to_16), 1);

    // Init alert struct
    alert_list.root_node = NULL;
    alert_list.curr_node = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize network connection
    draw_string("Establishing network connection ...", 0, 20, DGX_WHITE(dgx_rgb_to_16), 2);
    ESP_ERROR_CHECK(example_connect());

    // Set timezone
    draw_string("Syncing system clock ...", 0, 50, DGX_WHITE(dgx_rgb_to_16), 2);
    init_clock();

    // Initialize HTTP client
    draw_string("Initializing HTTP client ...", 0, 80, DGX_WHITE(dgx_rgb_to_16), 2);
    init_http_client_config();

    // Initialize the menu nodes
    draw_string("Initializing menu ...", 0, 110, DGX_WHITE(dgx_rgb_to_16), 2);
    if (!init_menu()) {
        handle_init_error("Error initializing menu.");
    }

    // Initialize the file system
    draw_string("Initializing file system ...", 0, 140, DGX_WHITE(dgx_rgb_to_16), 2);
    init_fs();

    // Initialize the main states
    draw_string("Initializing main states ...", 0, 170, DGX_WHITE(dgx_rgb_to_16), 2);
    init_main_states();

    // Create mutex for HTTP response buffer.
    mutex = xSemaphoreCreateMutex();

    // Create tasks.
    BaseType_t result;
    if ((result = xTaskCreate(http_get_alerts, "http_get_alerts", 8192, NULL, 1, &alert_task_handle)) != pdPASS) {
        handle_init_error("Error creating http_get_alerts task.");
    }
    if ((result = xTaskCreate(http_get_curr_conditions, "http_get_curr_conditions", 8192, NULL, 2, &cc_task_handle)) 
            != pdPASS) {
        handle_init_error("Error creating http_get_curr_conditions task.");
    }
    if ((result = xTaskCreate(http_get_forecast, "http_get_forecast", 8192, NULL, 3, &forecast_task_handle)) 
            != pdPASS) {
        handle_init_error("Error creating http_get_forecast task.");
    }
    if ((result = xTaskCreate(cycle_alerts, "cycle_alerts", 4028, NULL, 4, 
            &cycle_alert_task_handler)) != pdPASS) {
        handle_init_error("Error creating cycle_alerts task.");
    }
    if ((result = xTaskCreate(cycle_main_states, "cycle_main_states", 4028, NULL, 5, 
            &cycle_main_state_task_handle)) != pdPASS) {
        handle_init_error("Error creating cycle_main_states task.");
    }
    if ((result = xTaskCreate(check_and_update_display, "check_and_update_display", 8192, NULL, 6, 
            &refresh_display_task_handle)) != pdPASS) {
        handle_init_error("Error creating check_and_update_display task.");
    }
    if ((result = xTaskCreate(get_time, "get_time", 4028, NULL, 7, &time_task_handle)) != pdPASS) {
        handle_init_error("Error creating get_time task.");
    }
    if ((result = xTaskCreate(check_error_bits, "check_error_bits", 2048, NULL, 8, &error_bits_task_handle)) != pdPASS) {
        handle_init_error("Error creating check_error_bits task.");
    }
    if ((result = xTaskCreate(play_eas_tone_buzzer_1, "play_eas_tone_buzzer_1", 1024, NULL, 9, &eas_task_handle_1)) 
            != pdPASS) {
        handle_init_error("Error creating play_eas_tone_buzzer_1 task.");
    }
    if ((result = xTaskCreate(play_eas_tone_buzzer_2, "play_eas_tone_buzzer_2", 1024, NULL, 10, &eas_task_handle_2)) 
            != pdPASS) {
        handle_init_error("Error creating play_eas_tone_buzzer_2_task.");
    }

    // Trigger the initial data retrieval tasks
    curr_state = STATE_MAIN;

    xTaskNotifyGiveIndexed(cc_task_handle, 0);
    xTaskNotifyGiveIndexed(forecast_task_handle, 0);
    xTaskNotifyGiveIndexed(alert_task_handle, 0);
    xTaskNotifyGiveIndexed(time_task_handle, 0);

    // Create timers.
    alert_cycle_timer = xTimerCreate("alert-cycle-timer", pdMS_TO_TICKS(ALERT_CYCLE_MS), pdTRUE, (void*) 0, 
        alert_cycle_timer_callback);
    if (alert_cycle_timer == NULL) {
        handle_init_error("Error creating alert-cycle-timer timer.");
    } else {
        xTimerStart(alert_cycle_timer, 0);
    }
    main_view_cycle_timer = xTimerCreate("main-view-cycle-timer", pdMS_TO_TICKS(MAIN_STATE_CYCLE_MS), pdTRUE, (void*) 1, 
        main_view_cycle_timer_callback);
    if (main_view_cycle_timer == NULL) {
        handle_init_error("Error creating main_view-cycle-timer timer.");
    } else {
        xTimerStart(main_view_cycle_timer, 0);
    }
    cc_refresh_timer = xTimerCreate("cc-refresh-timer", pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS), pdTRUE, 
        (void*) 2, cc_refresh_timer_callback);
    if (cc_refresh_timer == NULL) {
        handle_init_error("Error creating cc-refresh-timer timer.");    
    } else {
        xTimerStart(cc_refresh_timer, 0);
    }
    forecast_refresh_timer = xTimerCreate("forecast-refresh-timer", pdMS_TO_TICKS(FORECAST_REFRESH_TIME_MS), pdTRUE, 
        (void*) 3, forecast_refresh_timer_callback);
    if (forecast_refresh_timer == NULL) {
        handle_init_error("Error creating forecast-refresh-timer timer.");
    } else {
        xTimerStart(forecast_refresh_timer, 0);
    }
    alert_refresh_timer = xTimerCreate("alert-refresh-timer", pdMS_TO_TICKS(ALERTS_TIME_REFRESH_MS), pdTRUE, (void*) 4,
        alert_refresh_timer_callback);
    if (alert_refresh_timer == NULL) {
        handle_init_error("Error creating alert-refresh-timer timer.");
    } else {
        xTimerStart(alert_refresh_timer, 0);
    }
    time_refresh_timer = xTimerCreate("time-refresh-timer", pdMS_TO_TICKS(TIME_REFRESH_TIME_MS), pdTRUE, (void*) 5,
        time_refresh_timer_callback);
    if (time_refresh_timer == NULL) {
        handle_init_error("Error creating time-refresh-timer timer.");
    } else {
        xTimerStart(time_refresh_timer, 0);
    }
}