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

char* string_to_lower(const char *str) {
	size_t len = strlen(str) + 1;
	char *lower_str = (char*) malloc(len);
	memset(lower_str, 0, len);

	int i;
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

bool string_contains(const char *str_1, const char *str_2) {
	char *lower_str_1 = string_to_lower(str_1);
	char *lower_str_2 = string_to_lower(str_2);

	char *result = strstr(lower_str_1, lower_str_2);

	free(lower_str_1);
	free(lower_str_2);

	return result != NULL;
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
    time_utc += 1200;
    if (gmtime_r(&time_utc, &utc_date_time_tm) == NULL) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_ERROR, "Error setting UTC time.");
        return false;
    }

    char date_time[DATE_TIME_STRING_BUFFER_SIZE];
    strftime(date_time, DATE_TIME_STRING_BUFFER_SIZE, DATE_TIME_STRING_FORMAT, &utc_date_time_tm);

    return add_alert(event, date_time, severity);
}

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
    curr_state = STATE_CC;
    refresh_display = true;
}

void perform_menu_action() {
	// Back (go back to the previous menu (parent node's parent))
	if (compare_strings(MENU_OPTION_BACK, curr_menu_node->data, 4)) {
		curr_parent_node = find_grandparent_node(root_menu_node, NULL);
		if (curr_parent_node != NULL) {
			curr_section_lead_node = curr_parent_node->first_child;
			curr_menu_node = curr_section_lead_node;
			curr_menu_pos = 0;
            refresh_display = true;
		} else {
            exit_menu();
        }
    } else if (compare_strings(MENU_OPTION_SYSTEM_INFO, curr_menu_node->data, -1)) {
        curr_state = STATE_SI;
        refresh_display = true;
	} else if (compare_strings(MENU_OPTION_CLEAR_ERROR_CODE, curr_menu_node->data, -1)) {
        error_bits = 0b00000000;
    } else if (compare_strings(MENU_OPTION_SYSTEM_INFO, curr_menu_node->data, -1)) {
        curr_state = STATE_SI;
        refresh_display = true; 
    } else if (compare_strings(MENU_OPTION_CLEAR, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_CLEAR);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_CLOUDY, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_CLOUDY);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_PARTLY_CLOUDY, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_PARTLY_CLOUDY);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_SCATTERED_SHOWERS, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_SCATTERED_SHOWERS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_THUNDERSTORM, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", MENU_OPTION_THUNDERSTORM);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_TORNADO_WARNING, curr_menu_node->data, -1)) {
        add_test_alert(MENU_OPTION_TORNADO_WARNING, JSON_EXTREME);
        exit_menu();
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

void draw_header_rect() {
    fill_rect_v_gradient(0, 10, 210, 40, DGX_COPPER(dgx_rgb_to_16), DGX_NAVY(dgx_rgb_to_16));
}

void draw_string(const char *str, int16_t x, int16_t y, uint32_t color, int scale) {
    dgx_font_string_utf8_screen(tft, x + SHADOW_OFFSET, y + SHADOW_OFFSET, str, DGX_BLACK(dgx_rgb_to_16), 
        DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
    dgx_font_string_utf8_screen(tft, x, y, str, color, DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
}

void draw_header_string(const char *str) {
    draw_string(str, 10, 35, DGX_YELLOW(dgx_rgb_to_16), 2);
}

void draw_bitmap(const uint16_t *bitmap, int size, int x, int y, uint32_t scale) {
    if (bitmap == NULL) {
        return;
    }

    uint8_t image_be[size * size * 2];

    for (int i = 0; i < size * size; ++i) {
        uint16_t pixel = bitmap[i];
        image_be[2 * i] = pixel >> 8;
        image_be[2 * i + 1] = pixel & 0xFF;
    }

    dgx_screen_t *image = dgx_vscreen_from_array(image_be, size, size, 16, DgxScreenRGB);
    dgx_vscreen_to_screen(tft, x, y, image, DGX_VSCREEN_SCALE(scale) | DGX_VSCREEN_TRANSPARENT);
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
    dgx_fill_rectangle(tft, 0, 0, tft->width, 30, color);
    draw_string(alert->data, 0, 20, DGX_WHITE(dgx_rgb_to_16), 2);
}

void draw_condition_clear() {
    uint16_t *bitmap = get_image_data_from_fs(FILE_SUN_BIN);
    draw_bitmap(bitmap, 32, 70, 85, 3);
    free(bitmap);
}

void draw_condition_cloudy() {
    uint16_t *bitmap = get_image_data_from_fs(FILE_CLOUD_BIN);
    draw_bitmap(bitmap, 32, 50, 110, 2);
    draw_bitmap(bitmap, 32, 80, 90, 2);
    draw_bitmap(bitmap, 32, 110, 105, 2);
    free(bitmap);
}

void draw_condition_partly_cloudy() {
    uint16_t *sun_bitmap = get_image_data_from_fs(FILE_SUN_BIN);
    uint16_t *cloud_bitmap = get_image_data_from_fs(FILE_CLOUD_BIN);
    draw_bitmap(sun_bitmap, 32, 70, 85, 3);
    draw_bitmap(cloud_bitmap, 32, 75, 85, 3);
    free(sun_bitmap);
    free(cloud_bitmap);
}

void draw_condition_rain() {
    uint16_t *rain_bitmap = get_image_data_from_fs(FILE_RAIN_BIN);
    draw_bitmap(rain_bitmap, 32, 70, 85, 3);
    free(rain_bitmap);
}

void draw_condition_storm() {
    uint16_t *storm_bitmap = get_image_data_from_fs(FILE_STORM_BIN);
    draw_bitmap(storm_bitmap, 32, 70, 85, 3);
    free(storm_bitmap);
}

void draw_condition_unknown() {
    uint16_t *unknown_bitmap = get_image_data_from_fs(FILE_UNKNOWN_BIN);
    draw_bitmap(unknown_bitmap, 32, 70, 85, 3);
    free(unknown_bitmap);
}

uint16_t *get_image_data_from_fs(const char *file_name) {
    char file_path_buffer[FILE_PATH_BUFFER_SIZE];
    snprintf(file_path_buffer, FILE_PATH_BUFFER_SIZE, "%s/%s", BASE_PATH, file_name);
    FILE *f = fopen(file_path_buffer, "rb");
    if (f == NULL) {
        error_bits = ERROR_FS_LOAD;
        ESP_LOGE(TAG_ERROR, "Error reading \"%s\".", file_name);
        return NULL;
    }
    uint16_t *bitmap_buffer = (uint16_t*) malloc(1024 * sizeof(uint16_t));
    fread(bitmap_buffer, 1024, sizeof(uint16_t), f);
    fclose(f);

    ESP_LOGI(TAG_INFO, "Successfully loaded \"%s\".", file_name);

    return bitmap_buffer;
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

bool add_alert(const char *event, const char *alert_exp_date_time, const char *severity) {
    struct tm tm;
    strptime(alert_exp_date_time, DATE_TIME_STRING_FORMAT, &tm);

    struct AlertNode *new_node = (struct AlertNode*) calloc(1, sizeof(struct AlertNode));
    snprintf(new_node->data, MENU_NODE_DATA_BUFFER_SIZE, "%s", event);
    snprintf(new_node->severity, SEVERITY_BUFFER_SIZE, "%s", severity);
    new_node->expire_time = timegm(&tm);
    
    if (alert_list.root_node == NULL) {
        alert_list.root_node = new_node;
        alert_list.curr_node = new_node;
    } else {
        struct AlertNode *temp_node = alert_list.root_node;
        while (temp_node->next_node != NULL) {
            temp_node = temp_node->next_node;
        }
        temp_node->next_node = new_node;
    }

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
        if (curr_time_utc <= alert_list.root_node->expire_time) {
            prev_node = temp_node;
            temp_node = temp_node->next_node;
            continue;
        }
        placeholder_node = temp_node->next_node;
        if (prev_node == NULL) {
            alert_list.root_node = placeholder_node;
        } else {
            prev_node->next_node = placeholder_node;
        }
        free(temp_node);
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
    uint32_t i;
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
    cJSON *cjson_temperature = cJSON_GetObjectItem(cjson_properties, JSON_TEMPERATURE);
    if (cjson_temperature == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing temperature from JSON.");
    } else {
        temperature = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_temperature, JSON_VALUE));
        set_int_temperature_val(&temperature);
        ESP_LOGI(TAG_INFO, "Set temperature.");
    }

    // Description
    cJSON *cjson_desc = cJSON_GetObjectItem(cjson_properties, JSON_TEXT_DESCRIPTION);
    if (cjson_desc == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing description from JSON.");
    } else {
        memset(cc_desc, 0, DESC_BUFFER_SIZE);
        snprintf(cc_desc, DESC_BUFFER_SIZE, "%s", cJSON_GetStringValue(cjson_desc));
        ESP_LOGI(TAG_INFO, "Set description");
    }

    // Wind speed
    cJSON *cjson_wind_speed = cJSON_GetObjectItem(cjson_properties, JSON_WIND_SPEED);
    if (cjson_wind_speed == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing wind speed from JSON.");
    } else {
        wind_speed = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_wind_speed, JSON_VALUE));
        ESP_LOGI(TAG_INFO, "Set wind speed");
    }

    // Wind direction
    cJSON *cjson_wind_dir = cJSON_GetObjectItem(cjson_properties, JSON_WIND_DIR);
    if (cjson_wind_dir == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing wind direction from JSON.");
    } else {
        wind_direction = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_wind_dir, JSON_VALUE));
        ESP_LOGI(TAG_INFO, "Wind direction");
    }

    // Dew point
    cJSON *cjson_dew_point = cJSON_GetObjectItem(cjson_properties, JSON_DEWPOINT);
    if (cjson_dew_point == NULL) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing dew point from JSON.");
    } else {
        dew_point = cJSON_GetNumberValue(cJSON_GetObjectItem(cjson_dew_point, JSON_VALUE));
        set_float_temperature_val(&dew_point);
        ESP_LOGI(TAG_INFO, "Set dew point");
    }

    cJSON_Delete(cjson_root);

    // Calculate the wind direction string
    memset(wind_dir, 0, WIND_DIR_BUFFER_SIZE);

    if (wind_direction <= 15.0f || wind_direction > 345.0f) {           // North
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_N);
    } else if (wind_direction > 15.0f && wind_direction <= 75.0f) {     // North-west
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_NW);
    } else if (wind_direction > 75.0f && wind_direction <= 105.0f) {    // West
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_W);
    } else if (wind_direction > 105.0f && wind_direction <= 165.0f) {   // South-west
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_SW);
    } else if (wind_direction > 165.0f && wind_direction <= 195.0f) {   // South
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_S);
    } else if (wind_direction > 195.0f && wind_direction <= 255.0f) {   // South-east
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_SE);
    } else if (wind_direction > 255.0f && wind_direction <= 285.0f) {   // East
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_E);
    } else {                                                            // North-east
        snprintf(wind_dir, WIND_DIR_BUFFER_SIZE, "%s", WIND_DIR_NE);
    }

    ESP_LOGI(TAG_INFO, "Got current conditions");

    return true;
}

bool update_forecast() {
    ESP_LOGI(TAG_INFO, "Getting forecast ...");

    // Configure the HTTP client.
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
        return false;
    }
    cJSON *cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast JSON");
        return false;
    }
    cJSON *cjson_forecast = cJSON_GetObjectItem(cjson_properties, JSON_FORECAST);
    if (cjson_forecast == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast item from forecast JSON");
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

    // Iterate through the "periods" until we get the first "isDaytTime": true object.
    cjson_root = cJSON_Parse(http_response_buffer);
    if (cjson_root == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast response from forecast JSON");
        return false;
    }
    cjson_properties = cJSON_GetObjectItem(cjson_root, JSON_PROPERTIES);
    if (cjson_properties == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse forecast response properties from forecast JSON");
        return false;
    }
    cJSON *cjson_periods_array = cJSON_GetObjectItem(cjson_properties, JSON_PERIODS);
    if (cjson_periods_array == NULL) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Failed to parse periods array from forecast JSON");
        return false;
    }
    int count = cJSON_GetArraySize(cjson_periods_array);
    uint32_t i;
    bool is_day_time = false;
    cJSON *cjson_array_item = NULL;
    cJSON *cjson_array_obj_item = NULL;
    for (i = 0; i < count; i++) {
        cjson_array_item = cJSON_GetArrayItem(cjson_periods_array, i);
        if (cjson_array_item == NULL) {
            error_bits = ERROR_P_JSON;
            ESP_LOGI(TAG_ERROR, "%s %i", "Failed parsing periods array from object.");
            return false;
        }
        cjson_array_obj_item = cJSON_GetObjectItem(cjson_array_item, JSON_IS_DAYTIME);
        if (cjson_array_obj_item == NULL || cJSON_IsFalse(cjson_array_obj_item)) {
            continue;
        }
        is_day_time = true;
        break;
    }
    if (is_day_time) {
        cJSON *cjson_temperature = cJSON_GetObjectItem(cjson_array_item, JSON_TEMPERATURE);
        if (cjson_temperature == NULL) {
            error_bits = ERROR_SP_JSON;
            ESP_LOGE(TAG_ERROR, "Failed to parse high temperature from forecast JSON");
            return false;
        }
        high_temperature= cJSON_GetNumberValue(cjson_temperature);
    }

    cJSON_Delete(cjson_root);

    return is_day_time;
}

bool update_seven_seg() {
    // Get the bits from the current error code.
    bool error_arr[4];
    uint8_t i = 0;
    for (i = 0; i < 4; i++) {
        error_arr[i] = get_bit_val_at_pos(error_bits, 4 + i);
    }

    // We need to output the current error code serially to the register.
    uint8_t register_val = 0b00000000;
	
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
        refresh_display = true;
	} else if (menu_down && (curr_menu_node->next_sibling != NULL)) {
		curr_menu_node = curr_menu_node->next_sibling;
		curr_menu_pos++;
		if (curr_menu_pos == MENU_MAX_OPTIONS_DISPLAYED) {
			curr_section_lead_node = curr_menu_node;
            curr_menu_pos = 0;
		}
        refresh_display = true;
	} else if (menu) {
		if (curr_menu_node->first_child != NULL) {
			curr_parent_node = curr_menu_node;
			curr_menu_node = curr_menu_node->first_child;
			curr_section_lead_node = curr_menu_node;
			curr_menu_pos = 0;
            refresh_display = true;		
		} else {
			perform_menu_action();
		}
	}
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
    dgx_fill_rectangle(tft, 5, 60, 310, 170, DGX_DARKCYAN(dgx_rgb_to_16));
    dgx_fill_rectangle(tft, 7, 62, 306, 166, CC_CONTAINER_COLOR);

    // Write the date and time.
    draw_string(date, 220, 25, DGX_WHITE(dgx_rgb_to_16), 2);
    draw_string(time_, 240, 45, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the high.
    char high_temp_buff[10];
    memset(high_temp_buff, 0, 10);
    snprintf(high_temp_buff, 10, ": %i %c", high_temperature, temperature_char);
    draw_string("H", 250, 95, DGX_YELLOW(dgx_rgb_to_16), 2);
    draw_string(high_temp_buff, 263, 95, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the temperature.
    char temp_buff[5];
    memset(temp_buff, 0, 5);
    snprintf(temp_buff, 5, "%i %c", temperature, temperature_char);
    draw_string(temp_buff, 250, 130, DGX_WHITE(dgx_rgb_to_16), 3);

    // Write the wind data.
    uint16_t wind_buff_size = 10 + WIND_DIR_BUFFER_SIZE;
    char wind_buff[wind_buff_size];
    memset(wind_buff, 0, wind_buff_size);
    snprintf(wind_buff, wind_buff_size, "%s %.1f mph", wind_dir, wind_speed);
    draw_string(wind_buff, 210, 160, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the dew point
    char dew_point_buff[8];
    memset(dew_point_buff, 0, 8);
    snprintf(dew_point_buff, 8, "%.1f %c", dew_point, temperature_char);
    draw_string(dew_point_buff, 240, 185, DGX_WHITE(dgx_rgb_to_16), 2);
    uint16_t *dew_point_bitmap = get_image_data_from_fs(FILE_DEWPOINT_BIN);
    draw_bitmap(dew_point_bitmap, 16, 225, 172, 1);
    free(dew_point_bitmap);

    // Write the current description.
    draw_string(cc_desc, 30, 210, DGX_WHITE(dgx_rgb_to_16), 3);

    // Determine what condition to draw based on the description.
    if (string_contains(cc_desc, CC_CLEAR)) {
        draw_condition_clear();
    } else if (string_contains(cc_desc, CC_PARTLY)) {
        draw_condition_partly_cloudy();
    } else if (string_contains(cc_desc, CC_CLOUDY)) {
        draw_condition_cloudy();
    } else if (string_contains(cc_desc, CC_THUNDER)) {
        draw_condition_storm();
    } else if (string_contains(cc_desc, CC_RAIN) || string_contains(cc_desc, CC_SHOWERS)) {
        draw_condition_rain();
    } else {
        draw_condition_unknown();
    }
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
    memset(heap_size, 0, HEAP_SIZE_BUFFER_SIZE);
    snprintf(heap_size, HEAP_SIZE_BUFFER_SIZE, "Mem Free: %lu B", rem_heap_size);
    draw_string(heap_size, 10, 20, DGX_WHITE(dgx_rgb_to_16), 2);
}

void draw_curr_alert() {
    ESP_LOGI(TAG_INFO, "Drawing current alert %s", alert_list.curr_node->data);
    draw_alert(alert_list.curr_node);
}

//*********************************************************************************************************************

void http_get_curr_conditions(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            refresh_display = update_curr_conditions() && curr_state == STATE_CC;
            xSemaphoreGive(mutex);
            // Wait for 5 minutes before the next request
            vTaskDelay(pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void http_get_forecast(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            if (curr_day != prev_day && update_forecast() && curr_state == STATE_CC) {
                refresh_display = true;
                prev_day = curr_day;
            }
            xSemaphoreGive(mutex);
            // Wait for 5 minutes before the next request
            vTaskDelay(pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void http_get_alerts(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            if (update_alerts()) {
                refresh_display = true;
            }
            xSemaphoreGive(mutex);
            // Wait for 5 minutes before the next request
            vTaskDelay(pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void get_time(void *arg) {
    while (true) {
        refresh_display = update_time() && curr_state == STATE_CC;
        // Wait for 60 seconds before the next request
        vTaskDelay(pdMS_TO_TICKS(TIME_REFRESH_TIME_MS)); 
    }
}

void check_error_bits(void *arg) { 
    while (true) {
        if (error_bits != prev_error_bits) {
            prev_error_bits = error_bits;
            update_seven_seg();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }   
}

void check_and_update_display(void *arg) {
    while (true) {
        if (refresh_display) {
            if (curr_state == STATE_CC) {
                draw_cc_view();
            } else if (curr_state == STATE_MENU) {
                draw_menu_view();
            } else if (curr_state == STATE_SI) {
                draw_si_view();
            }
            if (alert_list.curr_node != NULL) {
                draw_curr_alert();  // We always want to show alerts on top of whatever screen we're on.
            }
            refresh_display = false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cycle_alerts(void *arg) {
    while (true) {
        ESP_LOGI(TAG_INFO, "Setting current alert ...");
        if (alert_list.root_node != NULL) {
            if (alert_list.curr_node == NULL) {
                alert_list.curr_node = alert_list.root_node;
                ESP_LOGI(TAG_INFO, "Set alert_list.curr_node to alert_list.root_node.");
            } else {
                alert_list.curr_node = alert_list.curr_node->next_node;
                if (alert_list.curr_node == NULL) {
                    alert_list.curr_node = alert_list.root_node;
                }
                ESP_LOGI(TAG_INFO, "Set alert_list.curr_node to alert_list.curr_node->next_node.");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ALERT_CYCLE_MS));    // Change alerts every 2 minutes.
    }
}

void menu_btn_event(void *arg, void *usr_data) {
    if (curr_state == STATE_MENU) {
        update_menu(true, false, false);
    } else if (curr_state == STATE_CC) {
        curr_state = STATE_MENU;
        refresh_display = true;
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
        if (play_eas_tone) {
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            play_eas_tone = false;
        }
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void play_eas_tone_buzzer_2(void *arg) {
    while (true) {
        if (play_eas_tone) {
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, BUZZER_DUTY);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);
            vTaskDelay(pdMS_TO_TICKS(1000));
            play_eas_tone = false;
        }
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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
    gpio_config(&reg_oe_config);

    gpio_config_t reg_rclk_config = {
        .pin_bit_mask = (1ULL << PIN_REG_RCLK),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&reg_rclk_config);

    gpio_config_t reg_ser_config = {
        .pin_bit_mask = (1ULL << PIN_REG_SER),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&reg_ser_config);

    ESP_ERROR_CHECK(shiftregister_gpio_init(&reg_config));

    // Incoming data LED
    gpio_config_t inc_led_config = {
        .pin_bit_mask = (1ULL << PIN_INC_DATA_LED),
        .mode = GPIO_MODE_OUTPUT,            // Set as input
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&inc_led_config);

    // Buttons are configured differently than normal GPIO pins.
    menu_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_btn_gpio_config.gpio_num = PIN_MENU;
    menu_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_btn_gpio_config.disable_pull = true;
    button_handle_t menu_btn;
    esp_err_t ret_menu = iot_button_new_gpio_device(&menu_btn_config, &menu_btn_gpio_config, &menu_btn);

    menu_up_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_up_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_up_btn_gpio_config.gpio_num = PIN_MENU_UP;
    menu_up_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_up_btn_gpio_config.disable_pull = true;

    button_handle_t menu_up_btn;
    esp_err_t ret_menu_up = iot_button_new_gpio_device(&menu_up_btn_config, &menu_up_btn_gpio_config, &menu_up_btn);

    menu_down_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_down_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_down_btn_gpio_config.gpio_num = PIN_MENU_DOWN;
    menu_down_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_down_btn_gpio_config.disable_pull = true;
    button_handle_t menu_down_btn;
    esp_err_t ret_menu_down = iot_button_new_gpio_device(&menu_down_btn_config, &menu_down_btn_gpio_config, 
        &menu_down_btn);

    ret_menu = iot_button_register_cb(menu_btn, BUTTON_SINGLE_CLICK, NULL, menu_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu);
    ret_menu_up = iot_button_register_cb(menu_up_btn, BUTTON_SINGLE_CLICK, NULL, menu_up_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu_up);
    ret_menu_down = iot_button_register_cb(menu_down_btn, BUTTON_SINGLE_CLICK, NULL, menu_down_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu_down);
}

void init_display() {
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
        return;
    }
    tft = dgx_ili9341_init(bus, GPIO_NUM_4, 0, 16, DgxScreenRGB);
    if (tft == NULL) {
        ESP_LOGE(TAG_ERROR, "dgx_ili9341_init failed");
        bus->dispose(bus);
        return;
    }

    dgx_ili9341_orientation(tft, DgxScreenLeftRight, DgxScreenBottomTop, true);
    
    clear_screen();
}

void init_menu() {
    // Parse the JSON string and create the menu nodes from it.
    cJSON *json = cJSON_Parse(MENU_JSON_STR);
    json = json->child;
    ESP_LOGI(TAG_INFO, "Successfully parsed menu JSON.");

    root_menu_node = create_menu_nodes(json, NULL, false);
    ESP_LOGI(TAG_INFO, "Successfully created menu nodes.");

	curr_parent_node = root_menu_node;
	curr_section_lead_node = curr_parent_node->first_child;
	curr_menu_node = curr_section_lead_node;
	curr_menu_pos = 0;
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
    ledc_timer_config(&buzzer_1_config);
    ledc_channel_config(&buzzer_1_channel);

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
    ledc_timer_config(&buzzer_2_config);
    ledc_channel_config(&buzzer_2_channel);
}

void init_clock() {
    esp_sntp_config_t config =
    ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");

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

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ERROR, "Error initializing file system: %s", esp_err_to_name(ret));
    }
}

//*********************************************************************************************************************

void app_main(void) {
    // Initialize GPIO pins
    init_pins();

    // Initialize buzzers
    init_buzzers();

    // Create and initialize the display bus
    init_display();

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
    init_menu();

    // Initialize the file system
    draw_string("Initializing file system ...", 0, 140, DGX_WHITE(dgx_rgb_to_16), 2);
    init_fs();

    // Create mutex for HTTP response buffer.
    mutex = xSemaphoreCreateMutex();

    xTaskCreate(check_and_update_display, "check_and_update_display", 4028, NULL, 3, NULL);
    xTaskCreate(check_error_bits, "check_error_bits", 2048, NULL, 4, NULL);
    xTaskCreate(get_time, "get_time", 4028, NULL, 5, NULL);
    xTaskCreate(cycle_alerts, "cycle_alerts", 1024, NULL, 6, NULL);
    xTaskCreate(http_get_alerts, "http_get_alerts", 8192, NULL, 7, NULL);
    xTaskCreate(http_get_curr_conditions, "http_get_curr_conditions", 8192, NULL, 8, NULL);
    xTaskCreate(http_get_forecast, "http_get_forecast", 8192, NULL, 9, NULL);
    xTaskCreate(play_eas_tone_buzzer_1, "play_eas_tone_buzzer_1", 1024, NULL, 10, NULL);
    xTaskCreate(play_eas_tone_buzzer_2, "play_eas_tone_buzzer_2", 1024, NULL, 11, NULL);
}