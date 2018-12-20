// OpenWeather details
#define OPEN_WEATHER_LANGUAGE "en"
#define DST_START "M3.2.0/2"
#define DST_END "M11.1.0/2"
#define VERSION "0.0.1"

#define DEFAULT_WIZARD_LOCATION_ID "6053154"
#define DEFAULT_WIZARD_LOCATION_NAME "Lethbridge"
#define DEFAULT_WIZARD_UTC_OFFSET "7"
#define DEFAULT_WIZARD_ST_TIME "MST"
#define DEFAULT_WIZARD_DST_TIME "MDT"

// Define data display formats
bool IS_METRIC = true;
bool IS_12H = true;
#define UPDATE_INTERVAL 300
#define TEMPERATURE_UPDATE 10
uint8_t allowedHours[] = {3, 15, 21};
#define TEMPERATURE_OFFSET_C -5

// Define pallete
#define MINI_BLACK 0
#define MINI_WHITE 1
#define MINI_YELLOW 2
#define MINI_BLUE 3
uint16_t palette[] = {ILI9341_BLACK, ILI9341_WHITE, ILI9341_YELLOW, 0x7E3C};

#ifndef MQTT_HOST
#define MQTT_HOST
#endif
#ifndef MQTT_PORT
#define MQTT_PORT
#endif
#ifndef MQTT_AUTH
#define MQTT_AUTH
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD
#endif
#ifndef MQTT_BASE_TOPIC
#define MQTT_BASE_TOPIC
#endif
#ifndef NAME
#define NAME
#endif
#ifndef OW_API_KEY
#define OW_API_KEY
#endif
