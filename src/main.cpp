#include "main.hpp"

HomieBootMode bootMode = HomieBootMode::UNDEFINED;
bool otaInitialDrawDone = false;
uint8_t otaState = 0;
uint8_t otaProgress = 0;

uint32_t currTempRotateTime = 0;

// Set initially to false to wait for WiFi before attempting update
// These are handled outside Homie loop to ensure it still functions
// even without an MQTT connection
bool initialUpdate = false;
bool doCurrentUpdate = false;
bool doForecastUpdate = false;
bool doAstronomyUpdate = false;
// Set to True inititally since sending is handled inside Homie loop
// and an MQTT connection is guarenteed
bool doTemperatureSend = true;

uint8_t moonAge = 0;
String moonAgeImage = "";
uint32_t lastTemperatureSent = 0;

HomieNode temperatureNode("temperature", "temperature");
HomieSetting<const char *> owApiKey("ow_api_key", "Open Weather API Key");
HomieSetting<const char *> tzUtcOffset("tz_utc_offset", "Standard time UTC offset. See https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html");
HomieSetting<const char *> tzDST("tz_dst", "Timezone abbrev when in Daylight Saving Time.");
HomieSetting<const char *> tzST("tz_st", "Timezone abbrev when in Standard Time.");
HomieSetting<const char *> dstStart("dst_start", "When DST starts in TZ format");
HomieSetting<const char *> dstEnd("dst_end", "When DST ends in TZ format");


void initialize() {
  // Setup timezone configurations
  // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
  String tzInfo;
  tzInfo.concat(tzST.get());
  tzInfo.concat(tzUtcOffset.get());
  tzInfo.concat(tzDST.get());
  tzInfo.concat(",M3.2.0/2,M11.1.0/2");
  Homie.getLogger() << F("Setting TZ info '") << tzInfo << F("'") << endl;
  setenv("TZ", tzInfo.c_str(), 1);
  tzset(); // save the TZ variable
  configTime(0, 0, NTP_SERVERS);

  doCurrentUpdate = true;
  doForecastUpdate = true;
  doAstronomyUpdate = true;
  temperatureNode.setProperty("unit").send("c");
}

void temperatureLoop() {
  if (doTemperatureSend) {
    sensors.requestTemperatures();
    float insideTemp = sensors.getTempCByIndex(0);
    Homie.getLogger() << F("Temperature: ") << insideTemp << endl;
    temperatureNode.setProperty("degrees").send(String(insideTemp));
    doTemperatureSend = false;
  }
}

void setup() {
  Serial.begin(115200);

  time_t rtc_time_t = 1543819410; // fake RTC time for now
  timezone tz_ = { 0, 0};
  timeval tv_ = { rtc_time_t, 0};
  settimeofday(&tv_, &tz_);

  // Setup pins
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  pinMode(TEMP_PIN, INPUT);
  sensors.begin();

  // Setup tickers
  updateCurrentTicker.attach(5 * 60 * 1000, []() {
    if (WiFi.status() == WL_CONNECTED) doCurrentUpdate = true;
  });
  updateForecastTicker.attach(20 * 60 * 1000, []() {
    if (WiFi.status() == WL_CONNECTED) doForecastUpdate = true;
  });
  updateAstronomyTicker.attach(60 * 60 * 1000, []() {
    if (WiFi.status() == WL_CONNECTED) doAstronomyUpdate = true;
  });
  sendTemperatureTicker.attach(60 * 1000, []() { doTemperatureSend = true; });

    // setup graphics driver
  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();
  carousel.setFrames(frames, frameCount);
  carousel.disableAllIndicators();
  carousel.setTargetFPS(3);

  // Setup HTTP clients
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));

  // Setup Homie
  Homie_setFirmware("weather-station", "0.0.1");
  Homie_setBrand("IoT");
  tzUtcOffset.setDefaultValue(TZ_UTC_OFFSET);
  tzST.setDefaultValue(TZ_ST);
  tzDST.setDefaultValue(TZ_DST);
  dstStart.setDefaultValue(DST_START);
  dstEnd.setDefaultValue(DST_END);
  Homie.onEvent(onHomieEvent);
  Homie.setSetupFunction(initialize);
  Homie.setLoopFunction(temperatureLoop);
  Homie.setup();
}

void onHomieEvent(const HomieEvent &event) {
  switch (event.type) {
    case HomieEventType::NORMAL_MODE:
      bootMode = HomieBootMode::NORMAL;
      break;
    case HomieEventType::OTA_STARTED:
      updateCurrentTicker.detach();
      updateForecastTicker.detach();
      updateAstronomyTicker.detach();
      otaState = 1;
      break;
    case HomieEventType::OTA_SUCCESSFUL:
      otaState = 2;
      break;
    case HomieEventType::OTA_FAILED:
      otaState = 3;
      break;
    case HomieEventType::OTA_PROGRESS:
      otaProgress = ((float)event.sizeDone / (float)event.sizeTotal) * 100;
      break;
    default:
      break;
  };
}

void loop() {
  // Handle OTA display first to ensure it is displayed before restarts
  switch (otaState) {
    case 1:  // started
      if (!otaInitialDrawDone || otaProgress % 5 == 0) {
        drawProgress(otaProgress, F("Updating..."));
        otaInitialDrawDone = true;
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
      if (doCurrentUpdate || doForecastUpdate || doAstronomyUpdate) {
        updateData();
        return;
      }

      // To avoid showing unix time zero dates/temps wait for initial update to
      // run
      if (initialUpdate) {
        gfx.fillBuffer(MINI_BLACK);
        drawTime();
        drawWifiQuality();
        carousel.update();
        drawCurrentWeather();
        drawAstronomy();
        gfx.commit();
      } else {
        // throttle drawing while system is getting started
        if ((millis() / 1000) % 5 == 0)
          drawProgress(millis() / 1000, F("Initializing..."));
      }
    default:
      break;
  }
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

  time_t tnow = time(nullptr);
  struct tm *timeinfo = localtime(&tnow);

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
    sprintf(time_str, "%s\n%s", getTimezone(timeinfo),
            timeinfo->tm_hour >= 12 ? "PM" : "AM");
    gfx.drawString(195, 27, time_str);
  } else {
    sprintf(time_str, "%s", getTimezone(timeinfo));
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
  // Rotate every 10 seconds
  bool displayCurrent = millis() - currTempRotateTime < 10 * 1000;
  if (millis() - currTempRotateTime > 20 * 1000) currTempRotateTime = millis();

  gfx.setTransparentColor(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(
      0, 55,
      getMeteoconIconFromProgmem(displayCurrent ? currentWeather.icon : "01n"));

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_BLUE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(220, 65,
                 displayCurrent ? OPEN_WEATHER_DISPLAYED_CITY_NAME : "Inside");

  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);

  if (!displayCurrent) {
    sensors.requestTemperatures();
    float insideTemp = sensors.getTempCByIndex(0);
    gfx.drawString(220, 78, String(insideTemp, 1) + (IS_METRIC ? "°C" : "°F"));
  } else {
    gfx.drawString(220, 78,
                   String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F"));
  }

  if (displayCurrent) {
    gfx.setFont(ArialRoundedMTBold_14);
    gfx.setColor(MINI_YELLOW);
    gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
    gfx.drawString(220, 118, currentWeather.description);
  }
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
  time_t time = forecasts[dayIndex].observationTime;
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
  time_t time = currentWeather.sunrise;
  gfx.drawString(5, 276, SUN_MOON_TEXT[1] + ":");
  gfx.drawString(45, 276, getTime(&time));
  time = currentWeather.sunset;
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

  if (doCurrentUpdate) {
    drawProgress(50, F("Updating conditions..."));
    doCurrentUpdate = !currentWeatherClient.updateCurrentById(
        &currentWeather, owApiKey.get(), OPEN_WEATHER_MAP_LOCATION_ID);
    Homie.getLogger() << F("Current Forecast Successful? ")
                      << (doCurrentUpdate ? F("False") : F("True")) << endl;
  }

  if (doForecastUpdate) {
    drawProgress(70, F("Updating forecasts..."));
    doForecastUpdate = !forecastClient.updateForecastsById(
        forecasts, owApiKey.get(), OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
    Homie.getLogger() << F("Forcast Update Successful? ")
                      << (doForecastUpdate ? F("False") : F("True")) << endl;
  }

  if (doAstronomyUpdate) {
    drawProgress(80, F("Updating astronomy..."));
    moonData = astronomy.calculateMoonData(time(nullptr));
    float lunarMonth = 29.53;
    moonAge = moonData.phase <= 4
                  ? lunarMonth * moonData.illumination / 2
                  : lunarMonth - moonData.illumination * lunarMonth / 2;
    moonAgeImage = String((char)(65 + ((uint8_t)((26 * moonAge / 30) % 26))));
    doAstronomyUpdate = false;
  }
  initialUpdate = true;
}

const char* getTimezone(tm *timeInfo) {
  if (timeInfo->tm_isdst) {
    return tzDST.get();
  } else {
    return tzST.get();
  }
}

String getTime(time_t *timestamp) {
  struct tm *timeInfo = gmtime(timestamp);

  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}
