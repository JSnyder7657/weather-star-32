#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <shiftregister_gpio.h>
#include <semaphore.h>
#include <cJSON.h>
#include "dgx_image.h"
#include "dgx_bits.h"
#include "iot_button.h"
#include "button_gpio.h"

// Constants
//*********************************************************************************************************************

// NWS API string constants 
#define API_HOST    "https://api.weather.gov/"
#define LATITUDE    "39.9623"
#define LONGITUDE   "-76.7276"
#define NWS_API_ITEM_LIMIT  "1"
#define LAT_LONG_DELIM  ","
#define PARAM_DELIM "&"
#define PARAM_START "?"
#define NWS_API_URL_PART_LIMIT_PARAM    "limit="
#define NWS_API_URL_POINT_PARAM "point="
#define NWS_API_ALERT_REGION    "PA"
#define NWS_API_POINTS_URL_PART "points/"
#define NWS_API_ALERTS_URL_PART "alerts/active"
#define NWS_API_OBSERVATIONS_LATEST_URL_PART    "/observations/latest"
#define NWS_API_MESSAGE_TYPE_ALERT  "Alert"

// JSON string constants
#define JSON_PROPERTIES "properties"
#define JSON_OBSERVATION_STATIONS   "observationStations"
#define JSON_TEMPERATURE    "temperature"
#define JSON_VALUE  "value"
#define JSON_TEXT_DESCRIPTION   "textDescription"
#define JSON_WIND_SPEED "windSpeed"
#define JSON_WIND_DIR   "windDirection"
#define JSON_DEWPOINT   "dewpoint"
#define JSON_FORECAST   "forecast"
#define JSON_PERIODS    "periods"
#define JSON_IS_DAYTIME    "isDaytime"
#define JSON_FEATURES "features"
#define JSON_EXTREME  "extreme"
#define JSON_SEVERE "severe"
#define JSON_MODERATE "moderate"
#define JSON_SEVERITY   "severity"
#define JSON_EVENT  "event"
#define JSON_DATE_TIME  "dateTime"

// Logging string constants
#define TAG_HTTP_GET    "http_get"
#define TAG_INFO    "INFO"
#define TAG_ERROR   "ERROR"

// Menu string constants
#define MENU_OPTION_BACK    "Back"
#define MENU_OPTION_CLEAR_ERROR_CODE  "Clear Error Code"
#define MENU_OPTION_SYSTEM_INFO  "System Info"
#define MENU_OPTION_CLEAR "Clear"
#define MENU_OPTION_CLOUDY  "Cloudy"
#define MENU_OPTION_PARTLY_CLOUDY "Partly Cloudy"
#define MENU_OPTION_SCATTERED_SHOWERS "Scattered Showers"
#define MENU_OPTION_THUNDERSTORM  "Thunderstorm"
#define MENU_OPTION_TORNADO_WARNING "Tornado Warning"
#define MENU_OPTION_TORNADO_WATCH "Tornado Watch"

// State constants
#define STATE_CC  UINT8_C(0)
#define STATE_MENU  UINT8_C(1)
#define STATE_SI  UINT8_C(2)

// Current condition string constants
#define CC_CLEAR    "Clear"
#define CC_CLOUDY    "Cloudy"
#define CC_PARTLY   "Partly"
#define CC_RAIN "Rain"
#define CC_SHOWERS  "Showers"
#define CC_THUNDER  "Thunder"
#define WIND_DIR_N  "N "
#define WIND_DIR_NW "NW"
#define WIND_DIR_W  "W "
#define WIND_DIR_SW "SW"
#define WIND_DIR_S  "S "
#define WIND_DIR_SE "SE"
#define WIND_DIR_E  "E "
#define WIND_DIR_NE "NE"

// File constants
#define FILE_PATH_BUFFER_SIZE UINT16_C(128)
#define BASE_PATH  "/littlefs"
#define PARTITION_NAME  "littlefs"
#define FILE_SUN_BIN  "sun.bin"
#define FILE_CLOUD_BIN  "cloud.bin"
#define FILE_RAIN_BIN "rain.bin"
#define FILE_STORM_BIN  "storm.bin"
#define FILE_DEWPOINT_BIN "dewpoint.bin"
#define FILE_UNKNOWN_BIN  "unknown.bin"

// UI string constants
#define HEADER_CURR_CONDITIONS   "Current Conditions"
#define FOOTER_WS32 "Weather Star 32"
#define FOOTER_BY "Joshua Snyder"

// Pin constants
const gpio_num_t PIN_MENU = (gpio_num_t) GPIO_NUM_33;
const gpio_num_t PIN_MENU_UP = (gpio_num_t) GPIO_NUM_34;
const gpio_num_t PIN_MENU_DOWN = (gpio_num_t) GPIO_NUM_35;
const gpio_num_t PIN_BUZZER_1 = (gpio_num_t) GPIO_NUM_25;
const gpio_num_t PIN_BUZZER_2 = (gpio_num_t) GPIO_NUM_21;
const gpio_num_t PIN_REG_SCLK = (gpio_num_t) GPIO_NUM_27;
const gpio_num_t PIN_REG_OE = (gpio_num_t) GPIO_NUM_13;
const gpio_num_t PIN_REG_SER = (gpio_num_t) GPIO_NUM_15;
const gpio_num_t PIN_REG_RCLK = (gpio_num_t) GPIO_NUM_12;
const gpio_num_t PIN_INC_DATA_LED = (gpio_num_t) GPIO_NUM_26;

// API constants
#define HTTP_GET_RESPONSE_BUFFER_SIZE   UINT16_C(32768)
#define DATE_BUFFER_SIZE   UINT8_C(32)
#define TIME_BUFFER_SIZE  UINT8_C(8)
#define ALERT_EVENT_BUFFER_SIZE UINT8_C(32)
#define REQUEST_URL_BUFFER_SIZE    UINT8_C(1024)
#define OB_STN_BUFFER_SIZE UINT8_C(255)
#define SEVERITY_BUFFER_SIZE UINT8_C(16)
#define DATE_TIME_STRING_BUFFER_SIZE  UINT16_C(32)
#define TIME_ZONE "EST5EDT,M3.2.0,M11.1.0"
#define DATE_TIME_STRING_FORMAT "%Y-%m-%dT%H:%M:%S"

// Menu constants
#define MENU_JSON_STR "{\"Menu\":{\"Back\":0,\"Settings\":{\"Back\":0},\"Tests\":{\"Back\":0,\"EAS\":[\"Back\",\"Blizzard Watch\",\"Blizzard Warning\",\"Severe T'storm Watch\",\"Severe T'storm Warning\",\"Tornado Watch\",\"Tornado Warning\"],\"Current Conditions\":[\"Back\",\"Clear\",\"Cloudy\",\"Partly Cloudy\",\"Scattered Showers\",\"Thunderstorm\"]},\"View System Info\":0,\"Clear Error Code\":0}}"
#define MENU_NODE_DATA_BUFFER_SIZE  UINT8_C(50)
#define MENU_OPTION_BUFFER_SIZE UINT8_C(50)
#define MENU_OPTIONS_X  INT16_C(5)
#define MENU_MAX_OPTIONS_DISPLAYED  UINT8_C(6)
#define MENU_OPTIONS_OFFSET UINT8_C(20)

// System info constants
#define HEAP_SIZE_BUFFER_SIZE UINT8_C(32)

// Buzzer constants
#define EAS_TONE_FREQ_1 UINT16_C(850)
#define EAS_TONE_FREQ_2 UINT16_C(860)
#define BUZZER_POWER_ON_FREQ  UINT16_C(1000)
#define BUZZER_POWER_ON_DURATION_MS UINT16_C(250)
#define EAS_TONE_DURATION_MS  UINT16_C(3000)

// UI constants
#define CC_CONTAINER_COLOR  dgx_rgb_to_16(41, 54, 103)
#define TEXT_SIZE   UINT8_C(2)
#define SHADOW_OFFSET UINT8_C(2)

// Weather data constants
#define DESC_BUFFER_SIZE  UINT8_C(255)
#define WIND_DIR_BUFFER_SIZE UINT8_C(3)
#define ALERT_CYCLE_MS  UINT16_C(10000) // 10 seconds
#define GENERIC_ALERT_EVENT "Check local news"

// Refresh time constants
#define FORECAST_REFRESH_TIME_MS  UINT64_C(28800000)  // 8 hours
#define CURR_CONDITIONS_REFRESH_TIME_MS UINT64_C(300000) // 5 minutes
#define ALERTS_TIME_REFRESH_MS  UINT32_C(120000)  // 2 minutes
#define TIME_REFRESH_TIME_MS  UINT16_C(60000) // 1 minute

// Current conditions constants
const bool DEG_F = true;

// Button constants;
#define BTN_SHORT_PRESS_TIME_MS  200
#define BTN_LONG_PRESS_TIME_MS  5000
button_config_t menu_btn_config, menu_up_btn_config, menu_down_btn_config;
button_gpio_config_t menu_btn_gpio_config, menu_down_btn_gpio_config, menu_up_btn_gpio_config;

// Buzzer constants
#define BUZZER_DUTY 700

// Seven segment error code constants
const uint8_t ERROR_NWS_API_GET = 0b00000001;
const uint8_t ERROR_DISPLAY_CC = 0b00000010;
const uint8_t ERROR_DISPLAY_ALERT = 0b00000011;
const uint8_t ERROR_SP_JSON = 0b00000100;
const uint8_t ERROR_P_JSON = 0b00000101;
const uint8_t ERROR_EP_JSON = 0b00000110;
const uint8_t ERROR_TIME_API_GET = 0b00000111;
const uint8_t ERROR_REQUEST_URL_LIMIT = 0b00001000;
const uint8_t ERROR_FS_LOAD = 0b00001001;

// Structs
//*********************************************************************************************************************

struct MenuNode {
	char data[MENU_NODE_DATA_BUFFER_SIZE];
	struct MenuNode *first_child;
	struct MenuNode *next_sibling;
	struct MenuNode *prev_sibling;
};

struct AlertNode {
  char data[MENU_NODE_DATA_BUFFER_SIZE];
  char severity[SEVERITY_BUFFER_SIZE];
  time_t expire_time;
  struct AlertNode *next_node;
};

struct AlertList {
  struct AlertNode *root_node;
  struct AlertNode *curr_node;
};

// Global vars
//*********************************************************************************************************************

// Buffer for HTTP response
char http_response_buffer[HTTP_GET_RESPONSE_BUFFER_SIZE];
int http_response_buffer_len = 0;
esp_http_client_config_t config;
esp_http_client_handle_t client;

// TFT SPI display bus and screen.
dgx_bus_protocols_t *bus;
dgx_screen_t *tft;

// Date and time
char date[DATE_BUFFER_SIZE], time_[TIME_BUFFER_SIZE];
int curr_day = 0;
int prev_day = -1;
time_t curr_time_utc;

// Weather data
int temperature, wind_direction, high_temperature = 0;
char temperature_char = 'C';
char cc_desc[DESC_BUFFER_SIZE], wind_dir[WIND_DIR_BUFFER_SIZE];
float wind_speed, dew_point = 0.0f;

// Alert list (singly linked list)
struct AlertList alert_list;

// Menu variables
uint8_t curr_state = STATE_CC;
int8_t curr_menu_pos = 0;

// State and menu variables
struct MenuNode *root_menu_node, *curr_parent_node, *curr_menu_node, *curr_section_lead_node, *global_temp_node;
bool refresh_display = false;

// Error bits
uint8_t error_bits, prev_error_bits = 0b00000000;

// Shift register configuration
shiftregister_config_t reg_config = {
    .data_io_num = PIN_REG_SER,
    .clk_io_num = PIN_REG_SCLK,
    .rclk_io_num = PIN_REG_RCLK,
    .mode = SHIFTREGISTER_MODE_WRITE
};

// Mutex
SemaphoreHandle_t mutex;

// Timers
TimerHandle_t alert_cycle_timer;

// File system
esp_vfs_littlefs_conf_t conf;

// Buzzers
bool play_eas_tone = false;

// Function prototypes
//*********************************************************************************************************************

uint16_t fast_blend(uint8_t alpha, uint32_t fgc, uint32_t bgc);
void fill_rect_v_gradient(int16_t x, int16_t y, int w, int h, uint32_t color_1, uint32_t color_2);

char* string_to_lower(const char *str);
bool compare_strings(const char *str_1, const char *str_2, int usr_comp_size);
bool string_contains(const char *str_1, const char *str_2);

//*********************************************************************************************************************

struct MenuNode* find_grandparent_node(struct MenuNode *node, struct MenuNode *grandparent_node);
void perform_menu_action();
struct MenuNode* create_menu_nodes(cJSON *curr_json, struct MenuNode *parent_node, bool is_array);

//*********************************************************************************************************************

esp_err_t http_event_handler(esp_http_client_event_t *evt);

//*********************************************************************************************************************

void draw_header_rect();
void draw_string(const char *str, int16_t x, int16_t y, uint32_t color, int scale);
void draw_header_string(const char *str);
void draw_bitmap(const uint16_t *bitmap, int size, int x, int y, uint32_t scale);
void draw_alert(struct AlertNode *alert);

void draw_condition_clear();
void draw_condition_cloudy();
void draw_condition_partly_cloudy();
void draw_condition_rain();
void draw_condition_storm();
void draw_condition_unknown();

uint16_t *get_image_data_from_fs(const char *file_name);

void clear_screen();

//*********************************************************************************************************************

void reset_response_buffer(char *response_buffer, int *response_len);
bool get_bit_val_at_pos(uint8_t num, uint8_t bit_pos);

//*********************************************************************************************************************

bool add_test_alert(const char *event, const char *severity);
bool add_alert(const char *event, const char *alert_exp_date_time, const char *severity);
bool remove_expired_alerts();

//*********************************************************************************************************************

void set_int_temperature_val(int *temperature_val);
void set_float_temperature_val(float *temperature_val);

bool update_time();
bool update_current_conditions();
bool update_alerts();
bool update_seven_seg();
void update_menu(bool menu, bool menu_up, bool menu_down);

//*********************************************************************************************************************

void draw_cc_view();
void draw_menu_view();
void draw_si_view();
void draw_curr_alert();

//*********************************************************************************************************************

void http_get_current_conditions(void *arg);
void http_get_forecast(void *arg);
void http_get_alerts(void *arg);

void get_time(void *arg);

void check_error_bits(void *arg);
void check_and_update_display(void *arg);
void cycle_alerts(void *arg);

void menu_btn_event(void *arg, void *usr_data);
void menu_up_btn_event(void *arg, void *usr_data);
void menu_down_btn_event(void *arg, void *usr_data);

void play_eas_tone_buzzer_1(void *arg);
void play_eas_tone_buzzer_2(void *arg);

//*********************************************************************************************************************

void init_pins();
void init_display();
void init_menu();
void init_buzzers();
void init_clock();
void init_fs();

#endif
