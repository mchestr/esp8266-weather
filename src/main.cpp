#include "main.hpp"

HomieBootMode bootMode = HomieBootMode::UNDEFINED;
uint8_t otaState = 0;
uint8_t otaProgress = 0;
bool canUpdate = false;
int lastUpdate = 0;
time_t dstOffset = 0;
uint8_t moonAge = 0;
String moonAgeImage = "";

HomieSetting<const char*> owApiKey("ow_api_key", "Open Weather API Key");

void setup() {
  Serial.begin(115200);

  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();

  carousel.setFrames(frames, frameCount);
  carousel.disableAllIndicators();
  carousel.setTargetFPS(3);

  Homie_setFirmware("weather-station", "0.0.1");
  Homie_setBrand("IoT");
  Homie.onEvent(onHomieEvent);
  Homie.setup();
}

void onHomieEvent(const HomieEvent &event) {
  switch (event.type) {
    case HomieEventType::NORMAL_MODE:
      bootMode = HomieBootMode::NORMAL;
      break;
    case HomieEventType::OTA_STARTED:
      otaState = 1;
      break;
    case HomieEventType::OTA_SUCCESSFUL:
      otaState = 2;
      break;
    case HomieEventType::OTA_FAILED:
      otaState = 3;
      break;
    case HomieEventType::WIFI_CONNECTED:
      canUpdate = true;
      break;
    case HomieEventType::WIFI_DISCONNECTED:
      canUpdate = false;
      break;
    case HomieEventType::OTA_PROGRESS:
      otaProgress = ((float)event.sizeDone / (float)event.sizeTotal) * 100;
      break;
    default:
      break;
  };
}

void loop() {
  // Handle OTA display first to ensure it is displaued before restarts
  switch (otaState) {
    case 1:  // started
      if (otaProgress < 4 || otaProgress % 10 == 0 || (otaProgress > 96)) {
        drawProgress(otaProgress, F("Updating..."));
      }
      return;
    case 2:  // success
      drawProgress(100, F("Success, restarting..."));
      otaState = 0;
      break;
    case 3:  // failed
      drawProgress(otaProgress, F("Failed to update."));
      otaState = 0;
      break;
    default:
      break;
  }

  Homie.loop();

  // Handle Normal mode screen drawing
  switch (bootMode) {
    case HomieBootMode::NORMAL:
      // Only update data if WiFi connected and interval passed
      if (canUpdate &&
          (lastUpdate == 0 || millis() - lastUpdate > UPDATE_INTERVAL * 1000)) {
        updateData();
        lastUpdate = millis();
      }

      // To avoid showing unix time zero dates/temps wait for initial update to
      // run
      if (lastUpdate != 0) {
        gfx.fillBuffer(MINI_BLACK);
        drawTime();
        drawWifiQuality();
        carousel.update();
        drawCurrentWeather();
        drawAstronomy();
        drawNextUpdate();
        gfx.commit();
      } else {
        drawProgress(millis() / 1000, F("Initializing..."));
      }
    default:
      break;
  }
}

void drawNextUpdate() {
  float percentAlong =
      ((float)(millis() - lastUpdate) / (float)(UPDATE_INTERVAL * 1000));
  int progressLength = SCREEN_WIDTH * percentAlong;
  gfx.drawHorizontalLine(0, 317, progressLength);
}

void drawWifiQuality() {
  int8_t quality = getWifiQuality();
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(228, 9, String(quality) + "%");
  for (int8_t i = 0; i < 4; i++) {
    for (int8_t j = 0; j < 2 * (i + 1); j++) {
      if (quality > i * 25 || j == 0) {
        gfx.setPixel(230 + 2 * i, 18 - j);
      }
    }
  }
}

void drawTime() {
  char time_str[11];
  char *dstAbbrev;
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm *timeinfo = localtime(&now);

  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_WHITE);
  String date =
      WDAY_NAMES[timeinfo->tm_wday] + " " + MONTH_NAMES[timeinfo->tm_mon] +
      " " + String(timeinfo->tm_mday) + " " + String(1900 + timeinfo->tm_year);
  gfx.drawString(120, 6, date);

  gfx.setFont(ArialRoundedMTBold_36);

  if (IS_12H) {
    int hour =
        (timeinfo->tm_hour + 11) % 12 + 1;  // take care of noon and midnight
    sprintf(time_str, "%2d:%02d:%02d\n", hour, timeinfo->tm_min,
            timeinfo->tm_sec);
    gfx.drawString(120, 20, time_str);
  } else {
    sprintf(time_str, "%02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min,
            timeinfo->tm_sec);
    gfx.drawString(120, 20, time_str);
  }

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(ArialMT_Plain_10);
  gfx.setColor(MINI_BLUE);
  if (IS_12H) {
    sprintf(time_str, "%s\n%s", dstAbbrev,
            timeinfo->tm_hour >= 12 ? "PM" : "AM");
    gfx.drawString(195, 27, time_str);
  } else {
    sprintf(time_str, "%s", dstAbbrev);
    gfx.drawString(195, 27, time_str);  // Known bug: Cuts off 4th character of
                                        // timezone abbreviation
  }
}

void drawProgress(uint8_t percentage, String text) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_YELLOW);

  gfx.drawString(120, 146, text);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(10, 168, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 170, 216 * percentage / 100, 11);

  gfx.commit();
}

void drawCurrentWeather() {
  gfx.setTransparentColor(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(
      0, 55, getMeteoconIconFromProgmem(currentWeather.icon));

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_BLUE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 65, OPEN_WEATHER_DISPLAYED_CITY_NAME);

  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);

  gfx.drawString(220, 78,
                 String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F"));

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 118, currentWeather.description);
}

void drawForecast1(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y) {
  drawForecastDetail(x + 10, y + 165, 0);
  drawForecastDetail(x + 95, y + 165, 1);
  drawForecastDetail(x + 180, y + 165, 2);
}

void drawForecast2(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y) {
  drawForecastDetail(x + 10, y + 165, 3);
  drawForecastDetail(x + 95, y + 165, 4);
  drawForecastDetail(x + 180, y + 165, 5);
}

void drawForecast3(MiniGrafx *display, CarouselState *state, int16_t x,
                   int16_t y) {
  drawForecastDetail(x + 10, y + 165, 6);
  drawForecastDetail(x + 95, y + 165, 7);
  drawForecastDetail(x + 180, y + 165, 8);
}

void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
  gfx.setColor(MINI_YELLOW);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  time_t time = forecasts[dayIndex].observationTime + dstOffset;
  struct tm *timeinfo = localtime(&time);
  gfx.drawString(
      x + 25, y - 15,
      WDAY_NAMES[timeinfo->tm_wday] + " " + String(timeinfo->tm_hour) + ":00");

  gfx.setColor(MINI_WHITE);
  gfx.drawString(
      x + 25, y,
      String(forecasts[dayIndex].temp, 1) + (IS_METRIC ? "°C" : "°F"));

  gfx.drawPalettedBitmapFromPgm(
      x, y + 15, getMiniMeteoconIconFromProgmem(forecasts[dayIndex].icon));
  gfx.setColor(MINI_BLUE);
  gfx.drawString(
      x + 25, y + 60,
      String(forecasts[dayIndex].rain, 1) + (IS_METRIC ? "mm" : "in"));
}

void drawAstronomy() {
  gfx.setFont(MoonPhases_Regular_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 275, moonAgeImage);

  gfx.setColor(MINI_WHITE);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(120, 250, MOON_PHASES[moonData.phase]);

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(5, 250, SUN_MOON_TEXT[0]);
  gfx.setColor(MINI_WHITE);
  time_t time = currentWeather.sunrise + dstOffset;
  gfx.drawString(5, 276, SUN_MOON_TEXT[1] + ":");
  gfx.drawString(45, 276, getTime(&time));
  time = currentWeather.sunset + dstOffset;
  gfx.drawString(5, 291, SUN_MOON_TEXT[2] + ":");
  gfx.drawString(45, 291, getTime(&time));

  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(235, 250, SUN_MOON_TEXT[3]);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(235, 276, String(moonAge) + "d");
  gfx.drawString(235, 291, String(moonData.illumination * 100, 0) + "%");
  gfx.drawString(200, 276, SUN_MOON_TEXT[4] + ":");
  gfx.drawString(200, 291, SUN_MOON_TEXT[5] + ":");
}

int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

void updateData() {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  while (!time(nullptr)) {
    Serial.print("#");
    delay(10);
  }
  // calculate for time calculation how much the dst class adds.
  dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - time(nullptr);

  drawProgress(50, F("Updating conditions..."));
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  currentWeatherClient.updateCurrentById(
      &currentWeather, owApiKey.get(), OPEN_WEATHER_MAP_LOCATION_ID);

  drawProgress(70, F("Updating forecasts..."));
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  uint8_t allowedHours[] = {12, 0};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, owApiKey.get(),
                                     OPEN_WEATHER_MAP_LOCATION_ID,
                                     MAX_FORECASTS);

  drawProgress(80, F("Updating astronomy..."));
  moonData = astronomy.calculateMoonData(time(nullptr));
  float lunarMonth = 29.53;
  moonAge = moonData.phase <= 4
                ? lunarMonth * moonData.illumination / 2
                : lunarMonth - moonData.illumination * lunarMonth / 2;
  moonAgeImage = String((char)(65 + ((uint8_t)((26 * moonAge / 30) % 26))));
}

String getTime(time_t *timestamp) {
  struct tm *timeInfo = gmtime(timestamp);

  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}
