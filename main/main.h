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
#define JSON_TONIGHT  "Tonight"
#define JSON_NAME  "name"
#define JSON_SHORT_FORECAST "shortForecast"
#define JSON_START_TIME "startTime"
#define JSON_END_TIME "endTime"
#define JSON_NIGHT  " Night"
#define JSON_SUNNY  "Sunny"
#define JSON_VISIBILITY "visibility"
#define JSON_DETAILED_FORECAST  "detailedForecast"

// Logging string constants
#define TAG_HTTP_GET    "http_get"
#define TAG_INFO    "INFO"
#define TAG_ERROR   "ERROR"

// Menu string constants
#define MENU_OPTION_BACK    "Back"
#define MENU_OPTION_CLEAR_ERROR_CODE  "Clear Error Code"
#define MENU_OPTION_SYSTEM_INFO  "View System Info"
#define MENU_OPTION_CLEAR "Clear"
#define MENU_OPTION_CLOUDY  "Cloudy"
#define MENU_OPTION_FOG "Fog"
#define MENU_OPTION_PARTLY_CLOUDY "Partly Cloudy"
#define MENU_OPTION_SCATTERED_SHOWERS "Scattered Showers"
#define MENU_OPTION_SNOW  "Snow"
#define MENU_OPTION_THUNDERSTORM  "Thunderstorm"
#define MENU_OPTION_TORNADO_WARNING "Tornado Warning"
#define MENU_OPTION_TORNADO_WATCH "Tornado Watch"
#define MENU_OPTION_UPDATE_FORECASTS  "Update Forecasts"
#define MENU_OPTION_UPDATE_CC "Update Current Conditions"
#define MENU_OPTION_SCROLLING_TEXT  "Scrolling Text"
#define MENU_OPTION_WRAPPED_TEXT  "Wrapped Text"

// State constants
#define STATE_INIT  UINT8_C(0)
#define STATE_MAIN  UINT8_C(1)
#define STATE_MAIN_CC  UINT8_C(2)
#define STATE_MAIN_FORECAST UINT8_C(3)
#define STATE_MAIN_LOCAL_FORECAST UINT8_C(4)
#define STATE_MENU  UINT8_C(5)
#define STATE_SI  UINT8_C(6)
#define STATE_WRAPPED_TEXT  UINT8_C(7)

// Current condition string constants
#define CC_CLEAR    "Clear"
#define CC_CLOUDY    "Cloudy"
#define CC_PARTLY   "Partly"
#define CC_RAIN "Rain"
#define CC_SHOWERS  "Showers"
#define CC_SNOW "Snow"
#define CC_THUNDER  "Thunder"
#define CC_FOG  "Fog"
#define CC_HAZE "Haze"
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
#define FILE_LOGO_BIN "logo.bin"
#define FILE_SUN_BIN  "sun.bin"
#define FILE_CLOUD_BIN  "cloud.bin"
#define FILE_RAIN_BIN "rain.bin"
#define FILE_STORM_BIN  "storm.bin"
#define FILE_DEWPOINT_BIN "dewpoint.bin"
#define FILE_HIGH_BIN "high.bin"
#define FILE_LOW_BIN  "low.bin"
#define FILE_SNOW_BIN "snow.bin"
#define FILE_MOON_BIN "moon.bin"
#define FILE_UNKNOWN_BIN  "unknown.bin"
#define FILE_NOAA_BIN "noaa.bin"
#define FILE_FOG_BIN  "fog.bin"

#define LOGO_IMG_SIZE UINT16_C(32768)
#define ICON_IMG_SIZE UINT16_C(1024)

// UI string constants
#define HEADER_CURR_CONDITIONS   "Current Conditions"
#define HEADER_EXT_FORECAST "Extended Forecast"
#define HEADER_LOCAL_FORECAST "Local Forecast"
#define FOOTER_WS32 "Weather Star 32"
#define FOOTER_BY "Joshua Snyder"
#define TEST_WRAPPED_TEXT "According to all known laws of aviation, there is no way that a bee should be able to fly. Its wings are too small to get its fat little body off the ground. The bee, of course, flies anyway. Because bees don't care what humans think is impossible."

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
#define MAX_EF_COUNT  UINT8_C(7)

// Menu constants
#define MENU_JSON_STR "{\"Menu\":{\"Back\":0,\"Settings\":{\"Back\":0},\"Tests\":{\"Back\":0,\"EAS\":[\"Back\",\"Blizzard Watch\",\"Blizzard Warning\",\"Severe T'storm Watch\",\"Severe T'storm Warning\",\"Tornado Watch\",\"Tornado Warning\"],\"Current Conditions\":[\"Back\",\"Clear\",\"Cloudy\",\"Fog\",\"Partly Cloudy\",\"Scattered Showers\",\"Snow\",\"Thunderstorm\"],\"Wrapped Text\":0},\"View System Info\":0,\"Clear Error Code\":0,\"Update Forecasts\":0,\"Update Current Conditions\":0}}"
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
#define BUZZER_DUTY UINT16_C(700)

// UI constants
#define CC_CONTAINER_COLOR  dgx_rgb_to_16(41, 54, 103)
#define TEXT_SIZE   UINT8_C(2)
#define SHADOW_OFFSET UINT8_C(3)

#define HEADER_RECT_X_POS UINT8_C(0)
#define HEADER_RECT_Y_POS UINT8_C(10)
#define HEADER_RECT_W UINT8_C(210)
#define HEADER_RECT_H UINT8_C(40)
#define HEADER_TEXT_X_POS UINT8_C(10)
#define HEADER_TEXT_Y_POS UINT8_C(35)

#define DATE_X_POS UINT8_C(220)
#define DATE_Y_POS  UINT8_C(25)
#define TIME_X_POS  UINT8_C(240)
#define TIME_Y_POS  UINT8_C(45)

#define EF_DAY_1_CONTAINER_X_POS  UINT8_C(5)
#define EF_DAY_2_CONTAINER_X_POS  UINT8_C(165)
#define EF_CONTAINER_Y_POS  UINT8_C(60)
#define EF_DAY_1_DAY_X_POS  UINT8_C(62)
#define EF_DAY_2_DAY_X_POS  UINT8_C(220)
#define EF_DAY_Y_POS  UINT8_C(95)

#define EF_DAY_1_LOW_TEMP_X_POS  UINT8_C(111)
#define EF_DAY_2_LOW_TEMP_X_POS  UINT8_C(271)
#define EF_DAY_1_HIGH_TEMP_X_POS UINT8_C(45)
#define EF_DAY_2_HIGH_TEMP_X_POS UINT8_C(205)
#define EF_TEMP_Y_POS  UINT8_C(210)
#define EF_TEMP_FONT_SCALE UINT8_C(3)

#define EF_CONTAINER_H  UINT8_C(166)
#define EF_CONTAINER_W  UINT8_C(146)

#define EF_IMG_SIZE UINT8_C(32)
#define EF_IMG_SCALE  UINT8_C(2)

#define EF_DAY_1_IMG_X_POS  UINT8_C(50)
#define EF_DAY_2_IMG_X_POS  UINT8_C(209)
#define EF_IMG_Y_POS  UINT8_C(115)

#define EF_HIGH_LOW_ARR_Y_POS UINT8_C(185)

#define EF_TEMP_BUFFER_SIZE  UINT8_C(4)

#define CC_HIGH_ARR_X_POS UINT8_C(199)
#define CC_LOW_ARR_X_POS  UINT8_C(253)
#define CC_ARR_Y_POS  UINT8_C(70)

#define CC_HIGH_TEMP_X_POS  UINT8_C(234)
#define CC_LOW_TEMP_X_POS UINT8_C(289)
#define CC_HIGH_LOW_TEMP_Y_POS  UINT8_C(95)
#define CC_HIGH_LOW_TEMP_FONT_SCALE  UINT8_C(2)

#define CC_TEMP_X_POS UINT8_C(255)
#define CC_TEMP_Y_POS UINT8_C(130)

#define CC_WIND_X_POS UINT8_C(210)
#define CC_WIND_Y_POS UINT8_C(160)

#define CC_DEW_POINT_X_POS  UINT8_C(255)
#define CC_DEW_POINT_Y_POS  UINT8_C(185)
#define CC_DEW_POINT_IMG_Y_POS  UINT8_C(170)
#define CC_DEW_POINT_IMG_SIZE UINT8_C(16)

#define CC_VISIBILITY_X_POS UINT8_C(190)
#define CC_VISIBILITY_Y_POS UINT8_C(185)

#define CC_DESC_X_POS UINT8_C(30)
#define CC_DESC_Y_POS UINT8_C(210)

#define CC_CONDITION_IMG_SIZE  UINT8_C(32)
#define CC_CONDITION_IMG_X_POS  UINT8_C(65)
#define CC_CONDITION_IMG_Y_POS  UINT8_C(85)
#define CC_CONDITION_IMG_SCALE  UINT8_C(3)

#define CC_CONTAINER_X_POS  UINT8_C(5)
#define CC_CONTAINER_Y_POS  UINT8_C(60)
#define CC_CONTAINER_W  INT16_C(310)
#define CC_CONTAINER_H  INT16_C(170)

#define SI_X_POS UINT8_C(10)
#define SI_FREE_HEAP_SIZE_Y_POS UINT8_C(60)
#define SI_MIN_FREE_HEAP_SIZE_Y_POS UINT8_C(90)
#define SI_FONT_SCALE UINT8_C(2)

#define ALERT_TEXT_X_POS  UINT8_C(10)
#define ALERT_TEXT_Y_POS  UINT8_C(20)
#define ALERT_RECT_X_POS  UINT8_C(0)
#define ALERT_RECT_Y_POS  UINT8_C(0)
#define ALERT_RECT_W  UINT8_C(320)
#define ALERT_RECT_H UINT8_C(30)

#define LF_TEXT_BOUND_W UINT16_C(300)
#define LF_TEXT_X_POS UINT8_C(10)
#define LF_TEXT_Y_POS UINT8_C(65)
#define LF_TEXT_FONT_SCALE  UINT8_C(2)

#define NOAA_LOGO_IMG_X_POS UINT8_C(150)
#define NOAA_LOGO_IMG_Y_POS UINT8_C(12)

#define WRAPPED_TEXT_ROW_OFFEST INT16_C(15)
#define WRAPPED_TEXT_MAX_NUM_ROWS INT8_C(10)
#define WRAPPED_TEXT_TEMP_BUFFER_SIZE UINT16_C(512)

#define SHADOW_IMG_REPLACEMENT_COLOR  UINT16_C(0x1082)

// Weather data constants
#define DESC_BUFFER_SIZE  UINT8_C(255)
#define DAY_BUFFER_SIZE UINT8_C(4)
#define WIND_DIR_BUFFER_SIZE UINT8_C(3)
#define LOCAL_FORECAST_BUFFER_SIZE  UINT16_C(2048)
#define ALERT_CYCLE_MS  UINT16_C(10000) // 10 seconds
#define MAIN_STATE_CYCLE_MS UINT16_C(15000)  // 15 seconds
#define GENERIC_ALERT_EVENT "Check local news"

// Refresh time constants
#define FORECAST_REFRESH_TIME_MS  UINT64_C(28800000)  // 8 hours
#define CURR_CONDITIONS_REFRESH_TIME_MS UINT64_C(300000) // 5 minutes
#define ALERTS_TIME_REFRESH_MS  UINT32_C(120000)  // 2 minutes
#define TIME_REFRESH_TIME_MS  UINT16_C(60000) // 1 minute

// Current conditions constants
#define PM_THRESHOLD  UINT8_C(18)
#define CC_STAT_BUFFER_SIZE UINT8_C(50)
const bool DEG_F = true;

// Button constants;
#define BTN_SHORT_PRESS_TIME_MS  UINT8_C(200)
#define BTN_LONG_PRESS_TIME_MS  UINT16_C(5000)

// Seven segment error code constants
#define ERROR_NWS_API_GET UINT8_C(0x01)
#define ERROR_DISPLAY_CC  UINT8_C(0x02)
#define ERROR_DISPLAY_ALERT UINT8_C(0x03)
#define ERROR_SP_JSON UINT8_C(0x04)
#define ERROR_P_JSON  UINT8_C(0x05)
#define ERROR_EP_JSON UINT8_C(0x06)
#define ERROR_TIME_API_GET  UINT8_C(0x07)
#define ERROR_REQUEST_URL_LIMIT UINT8_C(0x08)
#define ERROR_FS_LOAD UINT8_C(0x09)

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

struct ExtForecastNode {
  struct ExtForecastNode *next_node;
  char desc[DESC_BUFFER_SIZE], day[DAY_BUFFER_SIZE];
  time_t start_time, end_time;
  uint8_t high_temp, low_temp;
};

struct CurrentConditions {
  int16_t high_temp, low_temp, temperature, visibility;
  float dew_point, wind_speed, wind_dir_f;
  char desc[DESC_BUFFER_SIZE], wind_dir[WIND_DIR_BUFFER_SIZE];
};

struct MainStateNode {
  uint8_t state;
  char local_forecast[LOCAL_FORECAST_BUFFER_SIZE];
  struct MainStateNode *next_node;
  struct CurrentConditions cc;
  struct ExtForecastNode *ext_forecasts_root_node; // 2 ext forecasts per main state
  
};

struct MainStateList {
  struct MainStateNode *root_node;
  struct MainStateNode *curr_node;
};


// Global vars
//*********************************************************************************************************************

// Buffer for HTTP response
char http_response_buffer[HTTP_GET_RESPONSE_BUFFER_SIZE];
int http_response_buffer_len = 0;
esp_http_client_config_t config;
esp_http_client_handle_t client;

// Buttons
button_config_t menu_btn_config, menu_up_btn_config, menu_down_btn_config;
button_gpio_config_t menu_btn_gpio_config, menu_down_btn_gpio_config, menu_up_btn_gpio_config;

// TFT SPI display bus and screen.
dgx_bus_protocols_t *bus;
dgx_screen_t *tft;

// Date and time
char date[DATE_BUFFER_SIZE], time_[TIME_BUFFER_SIZE];
int curr_day = 0;
int prev_day = -1;
bool is_night = false;
time_t curr_time_utc;

// Weather data
char temperature_char = 'C';

// Alert list (singly linked list)
struct AlertList alert_list;

// Menu variables
uint8_t curr_state = STATE_INIT;
int8_t curr_menu_pos = 0;

// State and menu variables
struct MenuNode *root_menu_node, *curr_parent_node, *curr_menu_node, *curr_section_lead_node, *global_temp_node;
struct MainStateList main_state_list = {0};

// Error bits
uint8_t error_bits, prev_error_bits = 0x00;

// Shift register configuration
shiftregister_config_t reg_config = {
    .data_io_num = PIN_REG_SER,
    .clk_io_num = PIN_REG_SCLK,
    .rclk_io_num = PIN_REG_RCLK,
    .mode = SHIFTREGISTER_MODE_WRITE
};

// Mutex
SemaphoreHandle_t mutex;

// Task handles
TaskHandle_t forecast_task_handle, cc_task_handle, alert_task_handle, cycle_alert_task_handler, 
    cycle_main_state_task_handle, refresh_display_task_handle, time_task_handle, eas_task_handle_1, eas_task_handle_2,
    error_bits_task_handle;

// Timers
TimerHandle_t alert_cycle_timer, main_view_cycle_timer, cc_refresh_timer, forecast_refresh_timer, alert_refresh_timer,
    time_refresh_timer;

// File system
esp_vfs_littlefs_conf_t conf;

// Function prototypes
//*********************************************************************************************************************

uint16_t fast_blend(uint8_t alpha, uint32_t fgc, uint32_t bgc);
void fill_rect_v_gradient(int16_t x, int16_t y, int w, int h, uint32_t color_1, uint32_t color_2);

//*********************************************************************************************************************

uint16_t *get_image_data_from_fs(const char *file_name, size_t size);
void add_image_to_fs(const uint16_t *image_data, const char *file_name, unsigned int size);

//*********************************************************************************************************************

char* string_to_lower(const char *str);
bool compare_strings(const char *str_1, const char *str_2, int usr_comp_size);
bool string_contains(const char *str_1, const char *str_2, bool case_sensitive);

//*********************************************************************************************************************

time_t get_utc_time_t_from_str(const char *datetime_str, const char *format);

//*********************************************************************************************************************

int divide_round_up(int a, int b);

//*********************************************************************************************************************

struct MenuNode* find_grandparent_node(struct MenuNode *node, struct MenuNode *grandparent_node);
void exit_menu();
void perform_menu_action();
struct MenuNode* create_menu_nodes(cJSON *curr_json, struct MenuNode *parent_node, bool is_array);

//*********************************************************************************************************************

esp_err_t http_event_handler(esp_http_client_event_t *evt);

//*********************************************************************************************************************

void add_main_state(const uint8_t state, const char *local_forecast, struct ExtForecastNode *forecasts_root_node);
void remove_forecast_main_states();

//*********************************************************************************************************************

struct ExtForecastNode* create_ext_forecast_node(const char *desc, const char *day, uint8_t high_temp, uint8_t low_temp,
    const char *start_time_str, const char *end_time_str);
void clear_ext_forecasts_list(struct ExtForecastNode *root_node);

//*********************************************************************************************************************

void draw_header_rect();
void draw_header_string(const char *str);

void draw_string(const char *str, int16_t x, int16_t y, uint32_t color, int scale);
void draw_string_wrapped(const char *str, int16_t x, int16_t y, uint32_t color, int scale, int16_t bound_width, 
      int16_t bound_height);

void draw_alert(struct AlertNode *alert);

void draw_date_time();

void draw_bitmap(const uint16_t *bitmap, int size, int x, int y, uint32_t scale);
void draw_low_arrow_bitmap(int size, int x, int y, uint32_t scale);
void draw_high_arrow_bitmap(int size, int x, int y, uint32_t scale);
void draw_dew_point_bitmap(int size, int x, int y, uint32_t scale);
void draw_logo_bitmap(int size, int x, int y, uint32_t scale);
void draw_noaa_logo_bitmap(int size, int x, int y, uint32_t scale);
void draw_ext_forecast_condition_bitmap(const char *condition, uint8_t day);
void draw_condition_bitmap(const char *condition);

void draw_condition_clear(int size, int x, int y, uint32_t scale);
void draw_condition_cloudy(int size, int x, int y, uint32_t scale);
void draw_condition_fog(int size, int x, int y, uint32_t scale);
void draw_condition_partly_cloudy(int size, int x, int y, uint32_t scale);
void draw_condition_rain(int size, int x, int y, uint32_t scale);
void draw_condition_snow(int size, int x, int y, uint32_t scale);
void draw_condition_storm(int size, int x, int y, uint32_t scale);
void draw_condition_unknown(int size, int x, int y, uint32_t scale);

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
bool update_alerts();
bool update_curr_conditions();
bool update_forecast();
bool update_seven_seg();
void update_menu(bool menu, bool menu_up, bool menu_down);
void update_curr_alert();

//*********************************************************************************************************************

void draw_cc_view();
void draw_local_forecast_view();
void draw_ext_forecast_view();
void draw_menu_view();
void draw_si_view();
void draw_test_wrapped_text_view();
void draw_curr_alert();

//*********************************************************************************************************************

void http_get_current_conditions(void *arg);
void http_get_forecast(void *arg);
void http_get_alerts(void *arg);

void get_time(void *arg);

void check_error_bits(void *arg);
void check_and_update_display(void *arg);

void menu_btn_event(void *arg, void *usr_data);
void menu_up_btn_event(void *arg, void *usr_data);
void menu_down_btn_event(void *arg, void *usr_data);

void play_eas_tone_buzzer_1(void *arg);
void play_eas_tone_buzzer_2(void *arg);

void cycle_alerts(void *arg);
void cycle_main_states(void *arg);

void alert_cycle_timer_callback(TimerHandle_t xTimer);
void main_view_cycle_timer_callback(TimerHandle_t xTimer);

void cc_refresh_timer_callback(TimerHandle_t xTimer);
void forecast_refresh_timer_callback(TimerHandle_t xTimer);
void alert_refresh_timer_callback(TimerHandle_t xTimer);
void time_refresh_timer_callback(TimerHandle_t xTimer);

//*********************************************************************************************************************

void init_pins();
bool init_display();
bool init_menu();
void init_http_client_config();
void init_buzzers();
void init_clock();
void init_fs();
void init_main_states();

void handle_init_error(const char *error_msg);

#endif
