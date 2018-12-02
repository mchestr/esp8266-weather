// OpenWeather details
#define OPEN_WEATHER_DISPLAYED_CITY_NAME "Vancouver"
#define OPEN_WEATHER_LANGUAGE "en"
#define OPEN_WEATHER_MAP_LOCATION_ID "6173331"

// Define data display formats
#define IS_METRIC true
#define IS_12H true
#define UPDATE_INTERVAL 300
#define UTC_OFFSET -8
struct dstRule startRule = {"PDT", Last, Sun, Mar, 2, 3600};
struct dstRule endRule = {"PST", Last, Sun, Oct, 2, 0};

// Define pallete
#define MINI_BLACK 0
#define MINI_WHITE 1
#define MINI_YELLOW 2
#define MINI_BLUE 3
uint16_t palette[] = {ILI9341_BLACK, ILI9341_WHITE, ILI9341_YELLOW, 0x7E3C};