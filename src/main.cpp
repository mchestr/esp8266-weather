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

// Message handlers for message display
bool messageReady = false;
bool messageAcknowledged = false;
String message;
uint8_t displayLength = 5;
uint32_t displayedAt = 0;

uint8_t moonAge = 0;
String moonAgeImage = "";
uint32_t lastTemperatureSent = 0;
uint8_t screenCount = 5;
uint8_t currentScreen = 0;
String tzInfo;

// Wizard helpers
String wizardLocId;
String wizardLocName;
String wizardDstTime;
String wizardStTime;
String wizardUtcOffset;
// load wizard defaults
String defaultWizardLocId;
String defaultWizardLocName;
String defaultWizardUtcOffset;
String defaultwizardStTime;
String defaultWizardDstTime;

HomieNode temperatureNode("temperature", "temperature");
HomieNode displayNode("display", "message");
HomieSetting<const char*> owApiKey("ow_api_key", "Open Weather API Key");
HomieSetting<const char*> owLocationName("ow_loc_name",
                                         "Open Weather Location Name");
HomieSetting<const char*> owLocationId("ow_loc_id", "Open Weather Location");
HomieSetting<const char*> tzUtcOffset(
    "tz_utc_offset",
    "Standard time UTC offset. See "
    "https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html");
HomieSetting<const char*> tzDST(
    "tz_dst", "Timezone abbrev when in Daylight Saving Time.");
HomieSetting<const char*> tzST("tz_st",
                               "Timezone abbrev when in Standard Time.");

TFTCallback nextPage(0, 50, 0, SCREEN_HEIGHT,
                     std::bind(switchPage, true), 255);
TFTCallback prevPage(SCREEN_WIDTH - 50, SCREEN_WIDTH, 0, SCREEN_HEIGHT,
                     std::bind(switchPage, false), 255);
TFTCallback toggleTempUnits(0, 160, 80, 120,
                            [](int16_t x, int16_t y) {
                              IS_METRIC = !IS_METRIC;
                              updateData(true);
                            },
                            0);
TFTCallback toggle24H(40, SCREEN_WIDTH - 40, 0, 80,
                      [](int16_t x, int16_t y) { IS_12H = !IS_12H; }, 0);
TFTCallback rebootButtonCallback(15, SCREEN_WIDTH - 15, 270, SCREEN_HEIGHT,
                                 rebootButton, 0);
TFTCallback wizardTouchCallback(0, SCREEN_WIDTH, 0, SCREEN_HEIGHT,
                                std::bind(&TFTWizard::touchCallback, &wizard,
                                          std::placeholders::_1,
                                          std::placeholders::_2),
                                0);
TFTCallback messageAcknowledgeCallback(20, SCREEN_WIDTH - 20, 300,
                                       SCREEN_HEIGHT - 5, messageAcknowledge,
                                       0);

void rebootButton(int16_t x, int16_t y) {
  drawProgress(50, F("Rebooting..."));
  Homie.setHomieBootModeOnNextBoot(HomieBootMode::CONFIGURATION);
  Homie.reboot();
}

bool displayMessageHandler(const HomieRange& range, const String& value) {
  Homie.getLogger() << F("Message Recieved: ") << value << endl;
  message = value;
  messageReady = true;
  messageAcknowledged = false;
  displayNode.setProperty("message").send(value);
  displayNode.setProperty("acknowledged").send("false");
  setCurrentScreenCallbacks(false);
  currentScreen = 10;
  setCurrentScreenCallbacks(true);
  return true;
}

void messageAcknowledge(int16_t x, int16_t y) {
  drawProgress(50, F("Acknowledging..."));
  messageAcknowledged = true;
  message = "";
  setCurrentScreenCallbacks(false);
  currentScreen = 0;
  setCurrentScreenCallbacks(true);
  displayNode.setProperty("acknowledged").send("true");
}

void setCurrentScreenCallbacks(bool enabled) {
  switch (currentScreen) {
    // going back on screen 0 has a happy side effect of making the
    // currentScreen 255, however all this causes is you need to press back
    // twice, and it works fine otherwise4Zx442CKYhwYGQLBvQz3d7HK
    case 255:
    case 0:
      toggle24H.setEnabled(enabled);
      toggleTempUnits.setEnabled(enabled);
      break;
    case 4:
      rebootButtonCallback.setEnabled(enabled);
      break;
    case 10:
      messageAcknowledgeCallback.setEnabled(enabled);
    default:
      break;
  }
}

void switchPage(bool forward) {
  setCurrentScreenCallbacks(false);
  if (forward) {
    currentScreen = (currentScreen + 1) % screenCount;
  } else {
    currentScreen = (currentScreen - 1) % screenCount;
  }
  setCurrentScreenCallbacks(true);
  Homie.getLogger() << F("Current Screen: ") << currentScreen << endl;
}

void initialize() {
  temperatureNode.setProperty("unit").send(IS_METRIC ? "c" : "f");
}

void temperatureLoop() {
  if (doTemperatureSend) {
    sensors.requestTemperatures();

    float insideTemp = IS_METRIC
                           ? (sensors.getTempCByIndex(0) + TEMPERATURE_OFFSET_C)
                           : sensors.getTempFByIndex(0);
    Homie.getLogger() << F("Temperature: ") << insideTemp << endl;
    temperatureNode.setProperty("degrees").send(String(insideTemp));
    doTemperatureSend = false;
  }
}

void loadWizardDefaults() {
  drawProgress(15, F("Initializing System..."));
  File f = SPIFFS.open("/wizard/location_id.txt", "r");
  if (!f) {
    defaultWizardLocId = DEFAULT_WIZARD_LOCATION_ID;
  } else {
    defaultWizardLocId = f.readString();
  }
  Homie.getLogger() << F("loaded defaultWizardLocId=") << defaultWizardLocId
                    << endl;
  f.close();
  drawProgress(30, F("Feeding the hamsters..."));
  f = SPIFFS.open("/wizard/location_name.txt", "r");
  if (!f) {
    defaultWizardLocName = DEFAULT_WIZARD_LOCATION_NAME;
  } else {
    defaultWizardLocName = f.readString();
  }
  Homie.getLogger() << F("loaded defaultWizardLocName=") << defaultWizardLocName
                    << endl;
  f.close();
  drawProgress(45, F("Loading..."));
  f = SPIFFS.open("/wizard/utc_offset.txt", "r");
  if (!f) {
    defaultWizardUtcOffset = DEFAULT_WIZARD_UTC_OFFSET;
  } else {
    defaultWizardUtcOffset = f.readString();
  }
  Homie.getLogger() << F("loaded defaultWizardUtcOffset=")
                    << defaultWizardUtcOffset << endl;
  f.close();
  drawProgress(60, F("Loading..."));
  f = SPIFFS.open("/wizard/st_time.txt", "r");
  if (!f) {
    defaultwizardStTime = DEFAULT_WIZARD_ST_TIME;
  } else {
    defaultwizardStTime = f.readString();
  }
  Homie.getLogger() << F("loaded defaultwizardStTime=") << defaultwizardStTime
                    << endl;
  f.close();
  drawProgress(75, F("Still loading..."));
  f = SPIFFS.open("/wizard/dst_time.txt", "r");
  if (!f) {
    defaultWizardDstTime = DEFAULT_WIZARD_DST_TIME;
  } else {
    defaultWizardDstTime = f.readString();
  }
  Homie.getLogger() << F("loaded defaultWizardDstTime=") << defaultWizardDstTime
                    << endl;
  f.close();
  drawProgress(90, F("Still loading..."));
  f = SPIFFS.open("/wizard/password.txt", "r");
  if (f) {
    wizard.setDefaultWiFiPassword(f.readString());
  }
  f.close();
  drawProgress(90, F("Done."));
}

void saveWizardValue(String name, String value) {
  File f = SPIFFS.open("/wizard/" + name, "w+");
  f.print(value);
  f.close();
  Homie.getLogger() << F("Saved ") << name << "=" << value << endl;
}

void setup() {
  Serial.begin(115200);

  time_t rtc_time_t = 1543819410;  // fake RTC time for now
  timezone tz_ = {0, 0};
  timeval tv_ = {rtc_time_t, 0};
  settimeofday(&tv_, &tz_);

  // Setup pins
  pinMode(TEMP_PIN, INPUT);
  sensors.begin();

  ts.begin();

  // Setup tickers
  updateCurrentTicker.attach(5 * 60, []() {
    if (WiFi.status() == WL_CONNECTED) doCurrentUpdate = true;
  });
  updateForecastTicker.attach(20 * 60, []() {
    if (WiFi.status() == WL_CONNECTED) doForecastUpdate = true;
  });
  updateAstronomyTicker.attach(60 * 60, []() {
    if (WiFi.status() == WL_CONNECTED) doAstronomyUpdate = true;
  });
  sendTemperatureTicker.attach(60, []() { doTemperatureSend = true; });

  // setup graphics driver
  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();
  carousel.setFrames(frames, frameCount);
  carousel.disableAllIndicators();
  carousel.setTargetFPS(3);
  wizard.setCallback(wizardCallback);
  SPIFFS.begin();

  wizard.addStep(
      [](TFTKeyboard* key) { key->setDefaultValue(defaultWizardLocId); },
      [](TFTKeyboard* key) {
        key->draw(
            F("Location ID?\nVisit https://openweathermap.org\nLethbridge: "
              "6053154"),
            false);
      },
      [](String value) {
        wizardLocId = value;
        saveWizardValue(F("location_id.txt"), value);
      });
  wizard.addStep(
      [](TFTKeyboard* key) { key->setDefaultValue(defaultWizardLocName); },
      [](TFTKeyboard* key) {
        key->draw(F("Location Name?\nExample: Lethbridge"), false);
      },
      [](String value) {
        wizardLocName = value;
        saveWizardValue(F("location_name.txt"), value);
      });
  wizard.addStep(
      [](TFTKeyboard* key) { key->setDefaultValue(defaultWizardUtcOffset); },
      [](TFTKeyboard* key) { key->draw(F("UTF Offset?\nExample: 7"), false); },
      [](String value) {
        wizardUtcOffset = value;
        saveWizardValue(F("utc_offset.txt"), value);
      });
  wizard.addStep(
      [](TFTKeyboard* key) { key->setDefaultValue(defaultwizardStTime); },
      [](TFTKeyboard* key) {
        key->draw(F("Standard Time Abbrev?\n\nExample: MST"), false);
      },
      [](String value) {
        wizardStTime = value;
        saveWizardValue(F("st_time.txt"), value);
      });
  wizard.addStep(
      [](TFTKeyboard* key) { key->setDefaultValue(defaultWizardDstTime); },
      [](TFTKeyboard* key) {
        key->draw(F("Daylight Saving Time Abbrev?\nExample: MDT"), false);
      },
      [](String value) {
        wizardDstTime = value;
        saveWizardValue(F("dst_time.txt"), value);
      });

  // Setup HTTP clients
  currentWeatherClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  forecastClient.setLanguage(OPEN_WEATHER_LANGUAGE);
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));

  // Setup Homie
  Homie_setFirmware("weather-station", "0.0.1");
  Homie_setBrand("IoT");
  displayNode.advertise("message").settable(displayMessageHandler);
  displayNode.advertise("acknowledged");
  Homie.onEvent(onHomieEvent);
  Homie.setSetupFunction(initialize);
  Homie.setLoopFunction(temperatureLoop);
  Homie.setup();

  boolean isCalibrationAvailable = touchController.loadCalibration();
  if (!isCalibrationAvailable) {
    Homie.getLogger() << F("Calibration not available") << endl;
    touchController.calibrate(calibrationCallback);
  }
}

void onHomieEvent(const HomieEvent& event) {
  switch (event.type) {
    case HomieEventType::NORMAL_MODE:
      bootMode = HomieBootMode::NORMAL;
      break;
    case HomieEventType::CONFIGURATION_MODE:
      bootMode = HomieBootMode::CONFIGURATION;
      loadWizardDefaults();
      wizard.start();
      wizardTouchCallback.enable();
      break;
    case HomieEventType::WIFI_CONNECTED:
      doCurrentUpdate = true;
      doForecastUpdate = true;
      doAstronomyUpdate = true;
      // Setup timezone configurations
      // https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
      tzInfo = String(tzST.get()) + String(tzUtcOffset.get()) +
              String(tzDST.get()) + ",M3.2.0/2,M11.1.0/2";
      Homie.getLogger() << F("Setting TZ info '") << tzInfo << F("'") << endl;
      setenv("TZ", tzInfo.c_str(), 1);
      tzset();
      configTime(0, 0, NTP_SERVERS);
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
        drawProgress(otaProgress, "Updating...(" + String(otaProgress) + "%)");
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

  touchController.loop();
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
        // handle message displays
        if (messageReady || (message != "" && !messageAcknowledged)) {
          gfx.setTextAlignment(TEXT_ALIGN_CENTER);
          gfx.setColor(MINI_BLUE);
          gfx.setFont(ArialRoundedMTBold_36);
          gfx.drawString(SCREEN_WIDTH / 2, 20, F("- BROADCAST -"));
          gfx.setColor(MINI_WHITE);
          gfx.setFont(ArialMT_Plain_16);
          gfx.drawStringMaxWidth(SCREEN_WIDTH / 2, 60, 200, message);
          gfx.setColor(MINI_WHITE);
          gfx.drawRect(20, 290, SCREEN_WIDTH - 40, 25);
          gfx.setColor(MINI_BLUE);
          gfx.setFont(ArialRoundedMTBold_14);
          gfx.setTextAlignment(TEXT_ALIGN_CENTER);
          gfx.drawString(SCREEN_WIDTH / 2, 293, F("ACKNOWLEDGE"));
          gfx.commit();
          if (messageReady) {
            displayedAt = millis();
            messageReady = false;
          }
          return;
        }
        switch (currentScreen) {
          case 1:
            drawCurrentWeatherDetail();
            break;
          case 2:
            drawForecastTable(0);
            break;
          case 3:
            drawForecastTable(4);
            break;
          case 4:
            drawAbout();
            break;
          default:
            drawTime();
            drawWifiQuality();
            carousel.update();
            drawCurrentWeather();
            drawAstronomy();
        }
        gfx.commit();
      } else {
        if (WiFi.status() != WL_CONNECTED) {
          drawProgress((millis() / 1000) % 100, F("Connecting to WiFi..."),
                       false);
        }
        gfx.setColor(MINI_WHITE);
        gfx.drawRect(15, 270, SCREEN_WIDTH - 30, 30);
        gfx.setColor(MINI_YELLOW);
        gfx.setTextAlignment(TEXT_ALIGN_CENTER);
        gfx.drawString(SCREEN_WIDTH / 2, 270, F("RESET"));
        gfx.commit();
        rebootButtonCallback.enable();
      }
      break;
    case HomieBootMode::CONFIGURATION:
      if (wizard.inProgress()) {
        wizard.draw();
      } else {
        drawProgress((millis() / 1000) % 100, F("Getting Started..."));
      }
      break;
    default:
      break;
  }
  yield();
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
  struct tm* timeinfo = localtime(&tnow);

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

void drawProgress(uint8_t percentage, String text, bool commit) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_YELLOW);

  gfx.drawString(120, 146, text);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(10, 168, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 170, 216 * percentage / 100, 11);

  if (commit) gfx.commit();
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
  gfx.drawString(220, 65, displayCurrent ? owLocationName.get() : "Inside");

  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setColor(MINI_WHITE);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);

  if (!displayCurrent) {
    sensors.requestTemperatures();
    float insideTemp = IS_METRIC
                           ? (sensors.getTempCByIndex(0) + TEMPERATURE_OFFSET_C)
                           : sensors.getTempFByIndex(0);
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

void drawForecast1(MiniGrafx* display, CarouselState* state, int16_t x,
                   int16_t y) {
  drawForecastDetail(x + 10, y + 165, 0);
  drawForecastDetail(x + 95, y + 165, 1);
  drawForecastDetail(x + 180, y + 165, 2);
}

void drawForecast2(MiniGrafx* display, CarouselState* state, int16_t x,
                   int16_t y) {
  drawForecastDetail(x + 10, y + 165, 3);
  drawForecastDetail(x + 95, y + 165, 4);
  drawForecastDetail(x + 180, y + 165, 5);
}

void drawForecast3(MiniGrafx* display, CarouselState* state, int16_t x,
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
  struct tm* timeinfo = localtime(&time);
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

void drawCurrentWeatherDetail() {
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, F("Current Conditions"));

  gfx.setTransparentColor(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(
      0, 20, getMeteoconIconFromProgmem(currentWeather.icon));

  String degreeSign = "°F";
  if (IS_METRIC) {
    degreeSign = "°C";
  }
  // String weatherIcon;
  // String weatherText;
  drawLabelValue(6, F("Temperature:"), currentWeather.temp + degreeSign);
  drawLabelValue(
      7, F("Wind Speed:"),
      String(currentWeather.windSpeed, 1) + (IS_METRIC ? "m/s" : "mph"));
  drawLabelValue(8, F("Wind Dir:"), String(currentWeather.windDeg, 1) + "°");
  drawLabelValue(9, F("Humidity:"), String(currentWeather.humidity) + "%");
  drawLabelValue(10, F("Pressure:"), String(currentWeather.pressure) + "hPa");
  drawLabelValue(11, F("Clouds:"), String(currentWeather.clouds) + "%");
  drawLabelValue(12, F("Visibility:"), String(currentWeather.visibility) + "m");

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(120, 40, F("Description: "));
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(120, 70, 120 - 2 * 15, currentWeather.description);
}

void drawForecastTable(uint8_t start) {
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, F("Forecasts"));
  uint16_t y = 0;

  String degreeSign = "°F";
  if (IS_METRIC) {
    degreeSign = "°C";
  }
  for (uint8_t i = start; i < start + 4; i++) {
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    y = 45 + (i - start) * 75;
    if (y > 320) {
      break;
    }
    gfx.setColor(MINI_WHITE);
    gfx.setTextAlignment(TEXT_ALIGN_CENTER);
    time_t time = forecasts[i].observationTime;
    struct tm* timeinfo = localtime(&time);
    gfx.drawString(120, y - 15,
                   WDAY_NAMES[timeinfo->tm_wday] + " " +
                       String(timeinfo->tm_hour) + ":00");

    gfx.drawPalettedBitmapFromPgm(
        0, y, getMiniMeteoconIconFromProgmem(forecasts[i].icon));
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);
    gfx.setColor(MINI_YELLOW);
    gfx.setFont(ArialRoundedMTBold_14);
    gfx.drawString(10, y - 15, forecasts[i].main);
    gfx.setTextAlignment(TEXT_ALIGN_LEFT);

    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y, F("T:"));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y, String(forecasts[i].temp, 0) + degreeSign);

    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y + 15, F("H:"));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y + 15, String(forecasts[i].humidity) + F("%"));

    gfx.setColor(MINI_BLUE);
    gfx.drawString(50, y + 30, F("P: "));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(70, y + 30,
                   String(forecasts[i].rain, 2) + (IS_METRIC ? "mm" : "in"));

    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y, F("Pr:"));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(170, y, String(forecasts[i].pressure, 0) + "hPa");

    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y + 15, F("WSp:"));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(
        170, y + 15,
        String(forecasts[i].windSpeed, 0) + (IS_METRIC ? "m/s" : "mph"));

    gfx.setColor(MINI_BLUE);
    gfx.drawString(130, y + 30, F("WDi: "));
    gfx.setColor(MINI_WHITE);
    gfx.drawString(170, y + 30, String(forecasts[i].windDeg, 0) + "°");
  }
}

void drawAbout() {
  gfx.fillBuffer(MINI_BLACK);

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  drawLabelValue(0, F("LocationID:"), owLocationId.get());
  drawLabelValue(1, F("DeviceID:"), Homie.getConfiguration().deviceId);
  drawLabelValue(3, F("SSID:"), WiFi.SSID());
  drawLabelValue(4, F("IP:"), WiFi.localIP().toString());
  drawLabelValue(5, F("MQTT:"),
                 String(Homie.getConfiguration().mqtt.server.host));
  drawLabelValue(7, F("Heap Mem:"), String(ESP.getFreeHeap() / 1024) + "kb");
  drawLabelValue(8, F("Flash Mem:"),
                 String(ESP.getFlashChipRealSize() / 1024 / 1024) + "MB");
  drawLabelValue(9, F("WiFi Strength:"), String(WiFi.RSSI()) + "dB");
  drawLabelValue(10, F("Chip ID:"), String(ESP.getChipId()));
  drawLabelValue(12, F("CPU Freq.: "), String(ESP.getCpuFreqMHz()) + "MHz");
  char time_str[15];
  const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
  const uint32_t millis_in_hour = 1000 * 60 * 60;
  const uint32_t millis_in_minute = 1000 * 60;
  uint8_t days = millis() / (millis_in_day);
  uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
  uint8_t minutes =
      (millis() - (days * millis_in_day) - (hours * millis_in_hour)) /
      millis_in_minute;
  sprintf(time_str, "%2dd%2dh%2dm", days, hours, minutes);
  drawLabelValue(13, F("Uptime: "), time_str);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(15, 270, SCREEN_WIDTH - 30, 30);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(SCREEN_WIDTH / 2, 275, F("RESET"));
}

void drawLabelValue(uint8_t line, String label, String value) {
  const uint8_t labelX = 15;
  const uint8_t valueX = 130;
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(labelX, 30 + line * 15, label);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(valueX, 30 + line * 15, value);
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

void updateData(bool force) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(ArialRoundedMTBold_14);

  if (force || doCurrentUpdate) {
    drawProgress(50, F("Updating conditions..."));
    currentWeatherClient.setMetric(IS_METRIC);
    bool doCurrentUpdate_ = !currentWeatherClient.updateCurrentById(
        &currentWeather, owApiKey.get(), owLocationId.get());
    Homie.getLogger() << F("Current Forecast Successful? ")
                      << (doCurrentUpdate_ ? F("False") : F("True")) << endl;
    doCurrentUpdate = false;
    // Throttle the update and try again in 5 seconds if failed
    if (doCurrentUpdate_) {
      updateCurrentTicker.once(5, []() { doCurrentUpdate = true; });
    }
  }

  if (force || doForecastUpdate) {
    drawProgress(70, F("Updating forecasts..."));
    forecastClient.setMetric(IS_METRIC);
    bool doForecastUpdate_ = !forecastClient.updateForecastsById(
        forecasts, owApiKey.get(), owLocationId.get(), MAX_FORECASTS);
    Homie.getLogger() << F("Forcast Update Successful? ")
                      << (doForecastUpdate_ ? F("False") : F("True")) << endl;
    doForecastUpdate = false;
    // Throttle the update and try again in 5 seconds if failed
    if (doForecastUpdate_) {
      updateForecastTicker.once(5, []() { doForecastUpdate = true; });
    }
  }

  if (force || doAstronomyUpdate) {
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
  nextPage.enable();
  prevPage.enable();
  toggle24H.enable();
  toggleTempUnits.enable();
}

const char* getTimezone(tm* timeInfo) {
  if (timeInfo->tm_isdst) {
    return tzDST.get();
  } else {
    return tzST.get();
  }
}

String getTime(time_t* timestamp) {
  struct tm* timeInfo = gmtime(timestamp);

  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}

void calibrationCallback(int16_t x, int16_t y) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.setColor(MINI_YELLOW);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.drawString(120, 160,
                 "Please calibrate\ntouch screen by\ntouching the point");
  gfx.setColor(MINI_WHITE);
  gfx.fillCircle(x, y, 10);
  gfx.commit();
}

void wizardCallback(String ssid, String password) {
  saveWizardValue(F("password.txt"), password);
  StaticJsonBuffer<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["name"] = NAME;
  JsonObject& wifi = root.createNestedObject("wifi");
  wifi["ssid"] = ssid;
  wifi["password"] = password;
  JsonObject& mqtt = root.createNestedObject("mqtt");
  mqtt["host"] = MQTT_HOST;
  mqtt["port"] = MQTT_PORT;
  mqtt["auth"] = MQTT_AUTH;
  mqtt["username"] = MQTT_USERNAME;
  mqtt["password"] = MQTT_PASSWORD;
  mqtt["base_topic"] = MQTT_BASE_TOPIC;
  JsonObject& ota = root.createNestedObject("ota");
  ota["enabled"] = true;
  JsonObject& settings = root.createNestedObject("settings");
  settings["ow_api_key"] = OW_API_KEY;
  settings["tz_utc_offset"] = wizardUtcOffset;
  settings["tz_st"] = wizardStTime;
  settings["tz_dst"] = wizardDstTime;
  settings["ow_loc_id"] = wizardLocId;
  settings["ow_loc_name"] = wizardLocName;
  Homie.getConfig().write(root);
  Homie.reboot();
}
