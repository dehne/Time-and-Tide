/****
 * Time and Tides v0.5.0
 * 
 * This is the Arduino framework firmware for a device with a tide clock and a water level 
 * display. It uses a WiFi connection to the internet to ask the noaa.gov tides and currents 
 * HTTPS GET REST api for information about the times of for high and low tides and the 
 * predicted water levels for a specified tide station and displays that information on two
 * electromechanical displays.
 * 
 * The first display is the tide clock. It is managed by the TideClock library. This library 
 * drives a hacked quartz clock movement to display how much time there is before the next 
 * high or low tide. See the library for more details.
 * 
 * The second display shows the current water level. It is managed by the WlDisplay library. 
 * The library uses a small stepper motor to raise and lower the level of the "sea" in an 
 * illustration of a seaside scene. See the library for more details.
 * 
 * The firmware can communicate to a terminal emulator using the Arduino Serial interface over 
 * USB. It has a command interpreter that can be used to change various runtime parameters such 
 * as the WiFi SSID and password to use and which tidal station to show the data for, among 
 * others. There are also various utiility commands that may be of interest. The "h" command 
 * displays a list.
 *  
 * Once the configuration is set, it can be stored in non-volatile memory using the "save" 
 * command. Once saved, the parameters are used whenever the power comes on or the device is 
 * reset.
 * 
 * The hardware also has a built-in LiPo battery that lets the clock continue to run when USB 
 * power goes away. To allow the clock to run for as long as it can, the water level display 
 * is paused when running on battery. When USB powe is restored, it will once again display the 
 * correct water level. The tide clock continues to run when operating on battery.
 * 
 * This firmware was designed and tested to run on an Adafruit featheresp32-s2 using the Arduino 
 * framework; no effort was made to make it portable.
 * 
 * The complete NOAA tides and currents api definition may be found at 
 * 
 *  https://api.tidesandcurrents.noaa.gov/api/prod/
 * 
 * The api responses are documented at
 * 
 *  https://api.tidesandcurrents.noaa.gov/api/prod/responseHelp.html
 * 
 *  
 ****
 *
 * Copyright 2023 by D.L. Ehnebuske
 * License: GNU Lesser General Public License v2.1
 * 
 ****/

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <UserInput.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "config.h"                                   // Configuration definitions
#include "TideClock.h"                                // Tide clock object
#include "WlDisplay.h"                                // Water level display object

// Misc constants
#define BANNER                  F("Time and Tides v0.5.0")
#define MAX_SERIAL_READY_MILLIS (10000)               // Maximum millis to wait for Serial to become ready
#define SECONDS_IN_NOMINAL_TIDE ((6*60+12)*60+30)     // Nominal time between high and low tide (sec)
#define SECONDS_PER_DAY         (86400)               // How many seconds there are in a day
#define MINUTES_PER_DAY         (1440)                // How many minutes there are in a day
#define LEVEL_UNAVAILABLE       (-100.0)              // Value when water level unavailable

// Hardware pins
#define TICK_PIN          (11)                        // The pin to which the TideClock's tick input is attached
#define TOCK_PIN          (12)                        // The pin to which the TideClock's tock input is attached
#define STEPPER_PIN_1     (10)                        // The pin to which the WlDisplay's IN4 pin is attached
#define STEPPER_PIN_2     (6)                         // The pin to which the WlDisplay's IN2 pin is attached
#define STEPPER_PIN_3     (9)                         // The pin to which the WlDisplay's IN3 pin is attached
#define STEPPER_PIN_4     (5)                         // The pin to which the WlDisplay's IN1 pin is attached
#define LIMIT_PIN         (A4)                        // The pin to which the limit sensor signal is attached
#define POWER_PIN         (A5)                        // The pin to which the "power present" signal is attached

// Some useful macros
#define timeToTimeOfDayUTC(t) (uint32_t)((t) % SECONDS_PER_DAY)       // Convert from time_t to seconds past midnight UTC
#define timeToSx(t)       (sx_t)((((t) / 60) % MINUTES_PER_DAY) / 6)  // Convert from time_t to sx_t

// Types
typedef uint8_t sx_t;                                 // Sample index type i.e. six minutes -- 1/10th of an hour, 1/240th of a day
struct configData_t {                                 // The shape of the data we store in "EEPROM"
  char ssid[33];                                      //   The SSID of the WiFi we should use (null-padded)
  char pw[33];                                        //   The WiFi password (null-padded)
  char station[8];                                    //   The 7-decimal-digit NOAA station ID we're doing tides for (null-padded) 
  float minLevel;                                     //   The lowest tide to be displayed (feet above/below MLLW)
  float maxLevel;                                     //   The highest tide to be displayed (feet above MLLW)
  tc_scale_t clockFace;                               //   The type of clock face; linear or nonlinear
};
enum opMode_t : uint8_t {notInit, run, test};         // The opMode type

/***
 * 
 * Global variables
 * 
 ***/
WiFiMulti WiFiMulti;                                  // The WiFi connection we'll use.
TideClock tc {TICK_PIN, TOCK_PIN};                    // The tide clock device
WlDisplay wld {STEPPER_PIN_1, STEPPER_PIN_2, STEPPER_PIN_3, STEPPER_PIN_4, LIMIT_PIN, POWER_PIN}; // The water level display device
UserInput ui {};                                      // User interface object -- cmd line processor
float predWl[TAT_N_PRED_WL];                          // The today's predicted water levels, every six minutes from 00:00 to 24:00
bool nextTideType = HIGH;                             // The next tide type; HIGH or LOW
configData_t config;                                  // The configuration data stored in NVS
opMode_t opMode;                                      // Whether we're running normally or doing adjustments

/***
 * 
 * Set the system clock to the current local time and date using an NTP server.
 * 
 * "Local time" is defined by the constant POSIX_TZ
 * 
 ***/
void setClock() {
  configTime(0, 0, TAT_NTP_SERVER);

  Serial.print(F("Waiting for NTP time sync..."));
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  setenv("TZ", TAT_POSIX_TZ, 1);
  tzset();
  Serial.printf("Sync successful. Current time: %s", ctime(&nowSecs));
}

/***
 * 
 * @brief Convert the POSIX time_t t to "hh:mm:ss"
 * 
 * @param  t The posix time_t to be converted
 * @return The String equivalent
 * 
 ***/
String toHhmmss(time_t t) {
  //           1         2
  // 012345678901234567890123
  // Fri Feb  3 13:13:00 2023
  return String(ctime(&t)).substring(11, 19);
}

/***
 * 
 * @brief Convert the POSIX time_t t to "yyyymmdd hh:mm" (the form the NOAA APIs like)
 * 
 * @param t The POSIX time_t to be converted
 * @param urlEncode Set true if the ' ' in the answer is to be rendered as '&20'; false by default
 * @return The String equivalent
 * 
 ***/
String toNOAAformat(time_t t, bool urlEncode = false) {
  tm *tAsTM = localtime(&t);
  return String(1900 + tAsTM->tm_year) +
    String(tAsTM->tm_mon < 9 ? "0" : "") + String(1 + tAsTM->tm_mon) +
    String(tAsTM->tm_mday < 10 ? "0" : "") + String(tAsTM->tm_mday) + String(urlEncode ? "&20" : " ") +
    String(tAsTM->tm_hour < 10 ? "0" : "") + String(tAsTM->tm_hour) + ":" +
    String(tAsTM->tm_min < 10 ? "0" : "") + String(tAsTM->tm_min);
}

/**
 * @brief Convert String noaa from NOAA format date/time to POSIX time_t
 * 
 * @param noaa String in the form "yyyy-mm-dd hh:mm"
 * @return time_t The time_t equivalent
 */
time_t fromNOAAformat(String noaa) {
  // Convert NOAA format time to POSIX time_t
  String dateTime = noaa; // "yyyy-mm-dd hh:mm"
  tm tideTm;              //  0123456789012345
  tideTm.tm_year = dateTime.substring(0, 4).toInt() - 1900;
  tideTm.tm_mon = dateTime.substring(5, 7).toInt() - 1;
  tideTm.tm_mday = dateTime.substring(8, 10).toInt();
  tideTm.tm_hour = dateTime.substring(11, 13).toInt();
  tideTm.tm_min = dateTime.substring(14).toInt();
  tideTm.tm_sec = 0;
  tideTm.tm_isdst = -1;
  return mktime(&tideTm);
}

/**
 * 
 * @brief Blink the built-in LED
 * 
 * @param n The number of times to blink. default: 1
 * 
 */
void blinkLED(uint8_t n = 1) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(250);
    digitalWrite(LED_BUILTIN, LOW);
    delay(250);
  }
}

/**
 * @brief Convert the specified configData_t to a String
 * 
 * @param c The configData_t to be converted
 * @return String The string representation of c
 */
String configToString(configData_t c) {
  char buffer[180];
  sprintf(buffer, "Configuration: \n"
                  "  ssid:     \'%s\'\n"
                  "  pw:       \'%s\'\n"
                  "  station:  \'%s\'\n"
                  "  minLevel: %f\n"
                  "  maxLevel: %f\n"
                  "  face:     %s\n",
                  c.ssid, c.pw, c.station, c.minLevel, c.maxLevel, c.clockFace == linear ? "linear" : "nonlinear");
  return String(buffer);
}

/**
 * 
 * Get a payload from an https GET REST service
 * 
 * N.B. The client must have previously established a secured connection 
 * with the server, i.e. gone through the setCACert() process.
 * 
 * @param   (const char *) url: The url to use for the request
 * @return  (String) The payload from the server
 * 
 */
String getPayload(const char *url) {
  log_d("[getPayload] Request: \"%s\"", url);
  String answer = "";
  HTTPClient https;
  WiFiClientSecure *client = new WiFiClientSecure; // Try to make a new client
  if(client) {                                     // If that worked
    client -> setCACert(TAT_SERVER_ROOT_CA_PEM);   //   Set things up to use a secure connection
    if (https.begin(*client, url)) {
      int httpCode = https.GET();                  //   Start connection and send HTTP header
      if (httpCode > 0) {                          //   If HTTP header has been sent and response header has been handled  
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                                                   //   If HTTPS GET succeeded
          String payload = https.getString();       //    Retrieve the json payload and deserialize it into jsonDoc
          log_d("Payload: \"%s\"\n", payload.c_str());
          answer = payload;
        } else {
          Serial.printf("[getPayload] HTTPS GET unsuccessful. HTTP response code: %d\n", httpCode);
          Serial.printf("[getPayload] Request URL was: %s\n", url);
        }
      } else {
        Serial.printf("[getPayload] HTTPS GET failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      Serial.printf("[getPayload] Unable to connect.\n");
    }
    delete client;
  }
  return answer;
}

/**
 * 
 * @brief Get the water level predictions for configured station on the specified date 
 *        and put them in the global predWl
 * 
 * @param   yyyymmdd: The date for which to get the water level predictions
 * @return  true if succeeded, false if something went wrong
 * 
 */
bool getWlPredections(String yyyymmdd) {
  String payload = getPayload((String(TAT_SERVER_URL "?" TAT_GET_PRED_WL) + 
    yyyymmdd + "&station=" + config.station).c_str());

  if (payload.length() > 0) {                                   //   If that worked, deserialize it
    DynamicJsonDocument predictions(TAT_JSON_CAPACITY_PRED);    //     The deserialized predictions
    DeserializationError err = deserializeJson(predictions, payload.c_str());
    if (err == DeserializationError::Ok) {                      //     If deserialization went well
      int sz = predictions["predictions"].size();               //       Check that we got more or less what was expected
      log_d("Predictions deserialized into a %d element array.", sz);
      if(sz == TAT_N_PRED_WL) {                                 //       If so, update wl with new data
        for (sx_t sx = 0; sx < TAT_N_PRED_WL; sx++) {
          predWl[sx] = predictions["predictions"][sx]["v"].as<const float>();
        }
        return true;                                            //         And say we did good
      } else {                                                  //       Otherwise, complain and indicate we have no predictions
        Serial.printf("Didn't get the expected %d prediction values. Instead got %d\n", TAT_N_PRED_WL, sz);
        log_d("Payload: \"%s\"\n", payload.c_str());
      }
    } else {
      Serial.printf("Json deserialization of water level predictions didn't work out. Error: %s\n", err.c_str());
    }
  }
  return false;
}

/***
 * 
 * @brief  Return the most recent water level measurement.
 * 
 * @return (float) The water level in feet above MLLW, NAN if couldn't get a prediction
 * 
 ***/
float getActualWl() {
  float answer = LEVEL_UNAVAILABLE;
  String payload = getPayload((String(TAT_SERVER_URL "?" TAT_GET_WL) + String(config.station)).c_str());
  if (payload.length() > 0) {
    log_d("[getActualWl] Payload: \"%s\"\n", payload.c_str());
    StaticJsonDocument<TAT_JSON_CAPACITY_WL> jsonDoc;
    DeserializationError err = deserializeJson(jsonDoc, payload.c_str());
    if (err == DeserializationError::Ok) {
      answer = jsonDoc["data"][0]["v"].as<float>();
    } else {
      Serial.printf("[getActualWl] Json deserialization of water level measurement didn't work out. error: ", err.c_str());
    }
  } else {
    Serial.print("[getActualWl] Couldn\'t get the water level.\n");
  }
  return answer;
}

/***
 * 
 * @brief  Return the current water level prediction.
 * 
 * @return (float) The water level in feet above MLLW, NAN if couldn't get a prediction
 * 
 ***/
float getPredWl() {
  static time_t dataMidnight = 0;   // 00:00:00 the date for which predWl is valid
  time_t nowSecs = time(nullptr);
  time_t midnightNow = (nowSecs / SECONDS_PER_DAY) * SECONDS_PER_DAY;
  if (dataMidnight != midnightNow) {
    dataMidnight = midnightNow;
    if (!getWlPredections(toNOAAformat(dataMidnight, true))) {
      return LEVEL_UNAVAILABLE;
    }
  }
  return predWl[timeToSx(nowSecs)];
}

/**
 * @brief Get-next-tide handler. Return the time, in time_t form, of the next high or low tide.
 *        This is intended as the handler function for a TideClock
 * 
 * @return time_t 
 */
time_t getNextTide() {
  time_t nowSecs = time(nullptr);
  time_t answer = nowSecs + SECONDS_IN_NOMINAL_TIDE;
  String timeStamp = toNOAAformat(nowSecs);
  String payload = getPayload(((String(TAT_SERVER_URL "?" TAT_GET_PRED_TIDES) + 
    toNOAAformat(nowSecs, true)) + String("&station=") + String(config.station)).c_str());
  if (payload.length() > 0) {
    DynamicJsonDocument predictions(TAT_JSON_CAPACITY_TIDES);
    DeserializationError err = deserializeJson(predictions, payload.c_str());
    if (err == DeserializationError::Ok) {
      int sz = predictions["predictions"].size();
      if(sz > 0) {
        log_d("[getNextTide %s] Got %d tide predictions: ", timeStamp.c_str(), sz);
        for (uint8_t ix = 0; ix < sz; ix++) {
          log_d("%s", predictions["predictions"][ix]["t"].as<String>().c_str());
          log_d("%s", ix < sz -1 ? ", " : "\n");
        }
        for (uint8_t ix = 0; ix < sz; ix++) {
          answer = fromNOAAformat(predictions["predictions"][ix]["t"].as<String>());
          nextTideType = predictions["predictions"][ix]["type"].as<String>().equals("H");
          if (answer > nowSecs) {
            break;
          }
          if (ix == sz - 1) {
            Serial.printf(". Using default tidal interval; didn't find a next tide after %s",
              asctime(localtime(&nowSecs)));
            answer = nowSecs + SECONDS_IN_NOMINAL_TIDE;
          }
        }
      } else {
        Serial.printf("[getNextTide %s] Didn't get any predicted tides.\n", timeStamp.c_str());
        log_d("[getNextTide] Payload: \"%s\"\n", payload.c_str());
      }
    } else {
      Serial.printf("[getNextTide %s] Using default tide interval. Json deserialization of tides didn't work out. Error: %d\n", 
        timeStamp.c_str(), err.c_str());
    }
  }
  uint32_t fromNow = (uint32_t)(answer - nowSecs);
  Serial.printf("[getNextTide %s] Next tide (%s) is %02d:%02d:%02d from now at %s\n", 
    timeStamp.c_str(), nextTideType ? "High" : "Low", fromNow / 3600, (fromNow % 3600) / 60, fromNow % 60, toNOAAformat(answer).c_str());
  return answer;
}

/**
 * @brief Connect the global Wifi connection WiFiMulti to the WiFi using the given SSID and password
 * 
 * @param ssid The ssid to use
 * @param pass The password to use
 * @return true WiFi connected okay
 * @return false Unable to connect WiFi
 */
bool connectWiFi(char * ssid, char * pass) {
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, pass);
  unsigned long startMillis = millis();

  // wait for WiFi connection
  Serial.print("Waiting for WiFi to connect...");
  while ((WiFiMulti.run() != WL_CONNECTED) && millis() - startMillis < TAT_WIFI_WAIT_MILLIS) {
    Serial.print(".");
  }
  if (WiFiMulti.run() == WL_CONNECTED) {
    Serial.print("Connected.\n");
    return true;
  }
  Serial.print("Unable to connect.\n");
  return false;
}

/**
 * @brief Store our signature and the specified configuration data in NVS
 * 
 * @param c The configuration data to be stored
 * @return true   All went well
 * @return false  Something bad happened
 */
bool putConfig(configData_t c) {
  nvs_handle_t handle;
  esp_err_t err;
  size_t blobSize = sizeof(c);

  // Open the our name space in the default NVS partition
  err = nvs_open(TAT_NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.printf("[putConfig] Unable to open NVS: 0x%x\n", err);
    nvs_close(handle);
    return false;
  }
  // Store the config info
  err = nvs_set_blob(handle, TAT_NVS_DATA_NAME, &c, blobSize);
  if (err != ESP_OK) {
    Serial.printf("[putConfig] Couldn\'t write config data to NVS: 0x%x\n", err);
    nvs_close(handle);
    return false;
  }
  //Store the signature
  err = nvs_set_u16(handle, TAT_NVS_SIG_NAME, TAT_NVS_SIG);
  if (err != ESP_OK) {
    Serial.printf("[putConfig] Couldn\'t write signature to NVS: 0x%x\n", err);
    nvs_close(handle);
    return false;
  }
  //Commit what we stored
  err = nvs_commit(handle);
  if (err != ESP_OK) {
    Serial.printf("[putConfig] Couldn\'t commit the configuration to NVS: 0x%x\n", err);
    nvs_close(handle);
    return false;
  }
  nvs_close(handle);
  return true;
}

/**
 * @brief   Read the configuration information from non-volatile storage, putting the results 
 *          into the global, config. If the configuration data is not in NVS, set the configuration to
 *          the defaults.
 * 
 * @return true   The configuration was successfully read from NVS 
 * @return false  The default configuration was used
 */
bool getConfig() {
  configData_t c;
  uint16_t sig;
  nvs_handle_t handle;
  esp_err_t err;
  size_t blobSize = sizeof(c);

  // Assume we'll fail to read from NVS and the config should be set to the default values
  strcpy(config.ssid, TAT_SSID);
  strcpy(config.pw, TAT_PASSWORD);
  strcpy(config.station, TAT_STATION_ID);
  config.minLevel = TAT_STATION_MIN_LEVEL;
  config.maxLevel = TAT_STATION_MAX_LEVEL;
  config.clockFace = TAT_FACE_TYPE;

  // Open the our name space in the default NVS partition
  err = nvs_open(TAT_NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    Serial.printf("[getConfig] Unable to open NVS: 0x%x\n", err);
    nvs_close(handle);
    return false;
  }

  // Try to get the value of the stored signature
  err = nvs_get_u16(handle, TAT_NVS_SIG_NAME, &sig);

  // if that went well, get the stored data
  if (err == ESP_OK && sig == TAT_NVS_SIG) {
    err = nvs_get_blob(handle, TAT_NVS_DATA_NAME, &c, &blobSize);
    if (err != ESP_OK) {
      Serial.printf("[getConfig] Unable to get the configuration data from NVS: 0x%x\n", err);
      nvs_close(handle);
      return false;
    }
    config = c;
  } else {
    Serial.printf("[getConfig] Unable to get a good signature: 0x%x; err: 0x%x\n", sig, err);
    nvs_close(handle);
    return false;
  }
  nvs_close(handle);
  return true;
 }

/**
 * @brief The handler for unrecognized user commands.
 */
void onCmdUnrecognized() {
  Serial.printf("Command %s not recognized.\n", ui.getWord(0).c_str());
}

/**
 * @brief The "help" and "h" command handler. Print a summary of the available commands.
 */
void onHelp() {
  Serial.print(
    "help | h                       Print this summary of the commands.\n"
    "mode run | test                Set the operating mode: run normally or enter test mode.\n"
    "setwl <float>                  In test mode, set the displayed water level (ft MLLW)\n"
    "tide                           Print information about the next high or low tide\n"
    "wl                             Print information about the current water level\n"
    "config                         Print the current configuration\n"
    "config ssid  <string>          Set the WiFi ssid to use to <string>\n"
    "config pw <string>             Set the WiFi password to use to <string>\n"
    "config station <7 digits>      Set the 7-digit NOAA station ID\n"
    "config minlevel <float>        Set the minimum displayable water level (ft MLLW)\n"
    "config maxlevel <float>        Set the maximum displayable water level (ft MLLW)\n"
    "config face linear | nonlinear Set the type of clock face being used\n"
    "save                           Save the current configuration\n"
    "restart                        Restart things using the saved configuration\n");
}

/**
 * @brief The mode command handler. Set the mode either to "run" or "test." During run mode, 
 *        the clock and the tide display run normally, during test mode, the tide level 
 *        display is paused and the setwl command can be used to show what different water 
 *        levels will look like.
 */
void onMode() {
  if (opMode == notInit) {
    Serial.print("Initialization didn\'t succeed; can\'t change mode.\n");
    return;
  }
  String modeName = ui.getWord(1);
  if (modeName.equalsIgnoreCase("run")) {
    opMode = run;
    Serial.print(F("Run mode.\n"));
  } else if (modeName.equalsIgnoreCase("test")) {
    Serial.print(F("Test mode. Water level display not running.\n"));
    opMode = test;
  } else {
    Serial.printf("Unrecognized mode: %s.\n", ui.getWord(1).c_str());
  }
}

/**
 * @brief The setwl command handler. Set the water level display to show the 
 *        given value (in feet above/below MLLW)
 */
void onSetWl() {
  if (opMode != test) {
    Serial.print(F("setwl command only active in test mode.\n"));
    return;
  }
  float wl = ui.getWord(1).toFloat();
  wld.setLevel(wl);
  Serial.printf("Water level display set to %f\n", wl);
}

/**
 * @brief The tide command handler. Display information related to the next tide
 */
void onTide() {
  time_t nextTide = tc.getNextTide();
  time_t t = time(nullptr);
  Serial.printf("It is now %s UTC. ", toHhmmss(t).c_str());
  if (nextTide == 0) {
    Serial.print(" No next tide has been set yet.\n");
    return;
  }
  int32_t secToNextTide = static_cast<int32_t>(nextTide) - static_cast<int32_t>(t);
  Serial.printf("The next tide (%s) is at %s, %s (%d seconds) from now.\n",
    nextTideType ? "High" : "Low", toHhmmss(nextTide).c_str(), toHhmmss(secToNextTide).c_str(), secToNextTide);
}

/**
 * @brief The wl command handler. Display information about the current water level
 * 
 */
void onWl() {
  time_t t = time(nullptr);
  Serial.printf("It is now %s UTC. The water level currently displayed is %f feet MLLW.\n", 
    toHhmmss(t).c_str(), wld.getLevel());
}

/**
 * @brief The config command handler. Set and display the available configuration 
 *        variables.
 */
void onConfig() {
  String subCmd = ui.getWord(1);
  if (subCmd.equalsIgnoreCase("minLevel")) {
    config.minLevel = ui.getWord(2).toFloat();
    return;
  }
  if (subCmd.equalsIgnoreCase("maxLevel")) {
    config.maxLevel = ui.getWord(2).toFloat();
    return;
  }
  if (subCmd.length() == 0) {
    Serial.print(configToString(config));
    return;
  }
  String rest = ui.getCommandLine();
  rest = rest.substring(rest.indexOf(subCmd) + subCmd.length());
  rest.trim();
  if (subCmd.equalsIgnoreCase("ssid")) {
    if (rest.length() >= sizeof(config.ssid)) {
      Serial.printf("SSID value \'%s\' too long. Max length is %d.\n", rest.c_str(), sizeof(config.ssid) - 1);
      return;
    }
    strcpy(config.ssid, rest.c_str());
    return;
  }
  if (subCmd.equalsIgnoreCase("pw")) {
    if (rest.length() >= sizeof(config.pw)) {
      Serial.printf("Password value \'%s\' too long. Max length is %d.\n", rest.c_str(), sizeof(config.pw) - 1);
      return;
    }
    strcpy(config.pw, rest.c_str());
    return;
  }
  if (subCmd.equalsIgnoreCase("station")) {
    if (rest.length() != 7 || rest.toInt() < 1000000) {
      Serial.printf("Invalid station ID \'%s\'. Must be a 7-digit number.\n", rest.c_str());
      return;
    }
    strcpy(config.station, rest.c_str());
    return;
  }
  if (subCmd.equalsIgnoreCase("face")) {
    String faceType = ui.getWord(2);
    if (faceType.equalsIgnoreCase("linear")) {
      config.clockFace = linear;
    } else if (faceType.equalsIgnoreCase("nonlinear")) {
      config.clockFace = nonlinear;
    } else {
      Serial.printf("Invalid face type. \"%s\". Must be \"linear\" or \"nonlinear\".\n",
        faceType);
    }
  }
  Serial.printf("Unrecognized configuration variable \'%s\'.\n", subCmd.c_str());
}

/**
 * @brief The save command handler. Save the current configuration in NVS
 * 
 */
void onSave() {
  if (putConfig(config)) {
    Serial.print("Configuration saved.\n");
  }
}
/**
 * @brief The restart command handler. Cause a software reset, restarting 
 *        things with the stored configuration.
 */
void onRestart() {
  ESP.restart();
}

/**
 * @brief Arduino setup function. Execute once upon startup or reset.
 */
void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  unsigned long startMillis = millis();
  unsigned long curMillis;
  do {
    blinkLED();
    curMillis = millis();
  } while (!Serial && curMillis - startMillis < MAX_SERIAL_READY_MILLIS);
  Serial.println(BANNER);

  // Attach the handlers for the ui
  ui.attachDefaultCmdHandler(onCmdUnrecognized);
  if (!(
    ui.attachCmdHandler("help", onHelp) &&
    ui.attachCmdHandler("h", onHelp) &&
    ui.attachCmdHandler("mode", onMode) &&
    ui.attachCmdHandler("setwl", onSetWl) &&
    ui.attachCmdHandler("tide", onTide) &&
    ui.attachCmdHandler("wl", onWl) &&
    ui.attachCmdHandler("config", onConfig) &&
    ui.attachCmdHandler("save", onSave) &&
    ui.attachCmdHandler("restart", onRestart))) {
    Serial.print(F("[setup] Need more command space.\n"));
  }

  // Try to get things going
  opMode = notInit;
  if (getConfig()) {
    if(connectWiFi(config.ssid, config.pw)) {
      setClock();
      wld.begin(config.minLevel, config.maxLevel);
      tc.begin(getNextTide, config.clockFace);
      opMode = run;
    }
  }

  // Say how that worked out.
  if (opMode == run) {
    Serial.print("Running normally.\n");
  } else {
    Serial.print("Unable to start using the current configuration.\n");
  }
  Serial.print(F("Type h or help for a list of commands.\n\n"));
}

/**
 * @brief Arduino loop() function. Execute repeatedly after setup() completes.
 */
void loop() {
  static bool needToSetClock = false;
  static time_t lastWlTime = 0;
  time_t curTime = time(nullptr);
  uint32_t curGmToD = timeToTimeOfDayUTC(curTime);

  if (opMode != notInit) {                                        // If initialized

    // Use NTP to set our internal clock once a day
    uint32_t curTimeOfDayUTC = timeToTimeOfDayUTC(curTime);
    if (needToSetClock && curTimeOfDayUTC >= TAT_SET_CLOCK_UTC_SECS) {
      setClock();
      needToSetClock = false;
    } else if (curTimeOfDayUTC < TAT_SET_CLOCK_UTC_SECS) {
      needToSetClock = true;
    }

    // If we're updating the water level display and enough time has passed, do the update
    if (opMode != test && curTime - lastWlTime >= TAT_LEVEL_CHECK_SECS) {
      float waterlevel = getPredWl();
      lastWlTime = curTime;
      if (waterlevel != LEVEL_UNAVAILABLE) {
        wld.setLevel(waterlevel);
      }
    }

    // Let the tide clock do its thing
    tc.run(curTime);
  }

  // Let the water level display do its thing
  wld.run();

  // Let the ui do its thing
  ui.run();
}