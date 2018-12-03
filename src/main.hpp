#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>

#include <Carousel.h>
#include <ILI9341_SPI.h>
#include <MiniGrafx.h>
#include <SPI.h>
#include <Ticker.h>

#include <Astronomy.h>
#include <OpenWeatherMapCurrent.h>
#include <OpenWeatherMapForecast.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#include <Homie.h>
#include "ArialRounded.h"
#include "MoonPhases.h"
#include "Settings.h"
#include "WeatherIcons.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BITS_PER_PIXEL 2
#define TFT_DC D2
#define TFT_CS D1
#define TFT_LED D8
#define TEMP_PIN D3

#define NTP_SERVERS \
  "0.ch.pool.ntp.org", "1.ch.pool.ntp.org", "2.ch.pool.ntp.org"
#define MAX_FORECASTS 10
String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
String SUN_MOON_TEXT[] = {"Sun", "Rise", "Set", "Moon", "Age", "Illum"};
String MOON_PHASES[] = {"New Moon",       "Waxing Crescent", "First Quarter",
                        "Waxing Gibbous", "Full Moon",       "Waning Gibbous",
                        "Third quarter",  "Waning Crescent"};

ILI9341_SPI tft = ILI9341_SPI(TFT_CS, TFT_DC);
MiniGrafx gfx = MiniGrafx(&tft, BITS_PER_PIXEL, palette);
Carousel carousel(&gfx, 0, 0, SCREEN_WIDTH, 100);

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapCurrent currentWeatherClient;
OpenWeatherMapForecast forecastClient;
Astronomy astronomy;
Astronomy::MoonData moonData;

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
Ticker updateCurrentTicker;
Ticker updateForecastTicker;
Ticker updateAstronomyTicker;
Ticker sendTemperatureTicker;

const char *getMeteoconIconFromProgmem(String iconText);
int8_t getWifiQuality();
String getTime(time_t *timestamp);
const char* getTimezone(tm *timeInfo);
void onHomieEvent(const HomieEvent &event);
void updateData();
void updateTemperatureSensor();

void drawWifiQuality();
void drawMQTTConnection();
void drawNextUpdate();
void drawTime();
void drawProgress(uint8_t percentage, String text);
void drawCurrentWeather();
void drawAstronomy();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
void drawForecast1(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y);
void drawForecast2(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y);
void drawForecast3(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y);

FrameCallback frames[] = {drawForecast1, drawForecast2, drawForecast3};
int frameCount = 3;
