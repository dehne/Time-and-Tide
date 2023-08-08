/****
 * Time and Tides v0.5.1
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
 * others. There are also various utility commands that may be of interest. The "h" command 
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
#include <esp_sntp.h>
#include "config.h"                                   // Configuration definitions
#include "TideClock.h"                                // Tide clock object
#include "WlDisplay.h"                                // Water level display object

// Misc constants
#define BANNER                  F("Time and Tides v0.5.1")
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
  tc_scale_t clockFace;                               //   The type of clock face; tcLinear or tcNonlinear
  tc_motor_t motor;                                   //   The type of lavet motor; tcOne or tcSixteen
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
configData_t config;                                  // The configuration data stored in NVS
opMode_t opMode;                                      // Whether we're running normally or doing adjustments
uint16_t testTick;                                    // In test mode, the number of ticks to run the clock
uint16_t testNsecs;                                   // In test mode, how many seconds between ticks
uint16_t testTicksTaken;                              // In test mode, how many ticks are have been taken

/***
 * 
 * Set the system clock to the current local time and date using an NTP server. Doing this 
 * also causes the ESP SNTP library to sync the system time using NTP every hour see
 * https://techtutorialsx.com/2021/09/03/esp32-sntp-additional-features/#Setting_the_SNTP_sync_interval
 * for details of an expreiment. 
 * 
 * Since we only deal with time to the one second level, one hour synchronization should not cause time()
 * to appear to go backwards.
 * 
 * "Local time" is defined by the constant POSIX_TZ
 * 
 * @return true   Time set successfully
 * @return false  Unable to set the time. Don't trust the system time
 * 
 ***/
bool setClock() {
  configTzTime(TAT_POSIX_TZ, TAT_NTP_SERVER);

  Serial.print(F("Waiting for NTP time sync..."));
  sntp_sync_status_t status;
  for (uint8_t i = 0; i < (TAT_NTP_WAIT_MILLIS / TAT_NTP_CHECK_MILLIS) && (status = sntp_get_sync_status()) != SNTP_SYNC_STATUS_COMPLETED; i++) {
    delay(TAT_NTP_CHECK_MILLIS);
    Serial.print(".");
  }
  if (status != SNTP_SYNC_STATUS_COMPLETED) {
    Serial.printf("sync not successful: %s\n", 
      status == SNTP_SYNC_STATUS_RESET ? "reset" : status == SNTP_SYNC_STATUS_IN_PROGRESS ? "in progress" : "completed");
      return false;
  }
  time_t nowSecs = time(nullptr);
  Serial.printf("Sync successful. Current time: %s", ctime(&nowSecs)); // ctime() appends a "\n", just because.
  return true;
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
  char buffer[200];
  sprintf(buffer, "Configuration: \n"
                  "  ssid:     \'%s\'\n"
                  "  pw:       \'%s\'\n"
                  "  station:  \'%s\'\n"
                  "  minLevel: %f\n"
                  "  maxLevel: %f\n"
                  "  face:     %s\n"
                  "  motor:    %s\n",
                  c.ssid, c.pw, c.station, c.minLevel, c.maxLevel, 
                  c.clockFace == tcLinear ? "linear" : "nonlinear", c.motor == tcOne ? "one" : "sixteen");
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
 * @return  (String) The payload from the server; empty String on error
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
        Serial.printf("[getPayload] HTTPS GET failed, error: '%s'. WiFi status: %d\n", 
          https.errorToString(httpCode).c_str(), WiFi.status());
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
 * @brief Get-next-tide handler. Return the information, in time_t form, about 
 *        the next high or low tide. This function is intended as the handler 
 *        function for a TideClock
 * 
 * @return tc_tide_t 
 */
tc_tide_t getNextTide() {
  time_t nowSecs = time(nullptr);
  tc_tide_t answer;
  answer.tideType = TC_UNAVAILABLE;
  answer.time = 0;
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
          log_d("%s%s", predictions["predictions"][ix]["t"].as<String>().c_str(), ix < sz -1 ? ", " : "\n");
        }
        for (uint8_t ix = 0; ix < sz; ix++) {
          answer.time = fromNOAAformat(predictions["predictions"][ix]["t"].as<String>());
          answer.tideType = predictions["predictions"][ix]["type"].as<String>().equals("H") ? HIGH : LOW;
          if (answer.time > nowSecs) {
            break;
          }
          if (ix == sz - 1) {
            Serial.printf(". Didn't find a next tide after %s",
              asctime(localtime(&nowSecs)));
            answer.tideType = TC_UNAVAILABLE;
            answer.time = 0;
          }
        }
      } else {
        Serial.printf("[getNextTide %s] Didn't get any predicted tides.\n", timeStamp.c_str());
        log_d("[getNextTide] Payload: \"%s\"\n", payload.c_str());
      }
    } else {
      Serial.printf("[getNextTide %s] Json deserialization of tides didn't work out. Error: %d\n", 
        timeStamp.c_str(), err.c_str());
    }
  }
  if (answer.tideType == TC_UNAVAILABLE) {
    Serial.printf("[getNextTide %s] Next tide data unavailable.\n", timeStamp.c_str());
  } else {
    uint32_t fromNow = (uint32_t)(answer.time - nowSecs);
    Serial.printf("[getNextTide %s] Next tide (%s) is %02d:%02d:%02d from now at %s\n", 
      timeStamp.c_str(), answer.tideType == HIGH ? "high" : "low", 
      fromNow / 3600, (fromNow % 3600) / 60, fromNow % 60, toHhmmss(answer.time).c_str());
  }
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
    "help | h                       Print this summary of the commands\n"
    "mode run | test                Set the operating mode: run normally or enter test mode\n"
    "tick nTicks [nSecs]            In test mode, tick the clock for nTicks, once every nSecs seconds\n"
    "tide                           Print information about the next high or low tide\n"
    "wl                             Print information about the current water level\n"
    "wl <float>                     In test mode, set the displayed water level (ft MLLW)\n"
    "config                         Print the current configuration\n"
    "config ssid  <string>          Set the WiFi ssid to use to <string>\n"
    "config pw <string>             Set the WiFi password to use to <string>\n"
    "config station <7 digits>      Set the 7-digit NOAA station ID\n"
    "config minlevel <float>        Set the minimum displayable water level (ft MLLW)\n"
    "config maxlevel <float>        Set the maximum displayable water level (ft MLLW)\n"
    "config face linear | nonlinear Set the type of clock face being used\n"
    "config motor one | sixteen     Set the type of motor the clock uses\n"
    "save                           Save the current configuration\n"
    "restart                        Restart things using the saved configuration\n");
}

/**
 * @brief The mode command handler. Set the mode either to "run" or "test." During run mode, 
 *        the clock and the tide display run normally. During test mode neither runs and the
 *        setwl and tick commands become active.tick
 * 
 */
void onMode() {

  String modeName = ui.getWord(1);
  if (modeName.equalsIgnoreCase("run")) {
    opMode = run;
    Serial.print(F("Run mode.\n"));
  } else if (modeName.equalsIgnoreCase("test")) {
    Serial.print(F("Test mode. Displays not running.\n"));
    opMode = test;
  } else {
    Serial.printf("Unrecognized mode: %s.\n", ui.getWord(1).c_str());
  }
}

/**
 * @brief The tick command handler. Test mode only. 
 * 
 *        tick <n> [<r>]
 * 
 *        Make the clock take <n> steps, one step every <r> seconds.
 *        <r> defaults to 6
 * 
 */
void onTick() {
  if (opMode != test) {
    Serial.print(F("The tick command is only active in test mode.\n"));
    return;
  }
  testTick = ui.getWord(1).toInt();
  testNsecs = ui.getWord(2).toInt();
  if (testNsecs < 1) {
    testNsecs = 6;
  }
  Serial.printf("Ticking %d times at 1 tick every %d seconds.\n", testTick, testNsecs);
  testTicksTaken = 0;
}

/**
 * @brief The tide command handler. Display information related to the next tide
 */
void onTide() {
  tc_tide_t nextTide = tc.getNextTide();
  time_t t = time(nullptr);
  Serial.printf("It is now %s UTC. ", toHhmmss(t).c_str());
  if (nextTide.tideType == TC_UNAVAILABLE) {
    Serial.print(" Next tide data is unavailable.\n");
    return;
  }
  int32_t secToNextTide = static_cast<int32_t>(nextTide.time) - static_cast<int32_t>(t);
  Serial.printf("The next tide (%s) is %s from now at %s.\n",
    nextTide.tideType == HIGH ? "high" : "low", toHhmmss(secToNextTide).c_str(), toHhmmss(nextTide.time).c_str());
}

/**
 * @brief The wl command handler. Display information about the current water level or,
 *        in test mode, set the water level being displayed.
 * 
 */
void onWl() {
  String wlString = ui.getWord(1);
  time_t t = time(nullptr);
  if (wlString.length() == 0) {
    Serial.printf("It is now %s UTC. The water level currently displayed is %f feet MLLW.\n", 
      toHhmmss(t).c_str(), wld.getLevel());
      return;
  }    
  if (opMode != test) {
    Serial.print(F("Can only set the water level in test mode.\n"));
    return;
  }
  float wl = wlString.toFloat();
  wld.setLevel(wl);
  Serial.printf("Water level display set to %f\n", wl);
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
      config.clockFace = tcLinear;
    } else if (faceType.equalsIgnoreCase("nonlinear")) {
      config.clockFace = tcNonlinear;
    } else {
      Serial.printf("Invalid face type. \"%s\". Must be \"linear\" or \"nonlinear\".\n",
        faceType);
    }
    return;
  }
  if (subCmd.equalsIgnoreCase("motor")) {
    String motorType = ui.getWord(2);
    if (motorType.equalsIgnoreCase("one")) {
      config.motor = tcOne;
    } else if (motorType.equalsIgnoreCase("sixteen")) {
      config.motor = tcSixteen;
    } else {
      Serial.printf("Invalid motor type. \"%s\". Must be \"one\" or \"sixteen\".\n",
        motorType);
    }
    return;
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
    ui.attachCmdHandler("tide", onTide) &&
    ui.attachCmdHandler("wl", onWl) &&
    ui.attachCmdHandler("config", onConfig) &&
    ui.attachCmdHandler("save", onSave) &&
    ui.attachCmdHandler("restart", onRestart) &&
    ui.attachCmdHandler("tick", onTick))) {
    Serial.print(F("[setup] Need more command space.\n"));
  }

  // Try to get things going
  opMode = notInit;
  if (getConfig()) {
    if(connectWiFi(config.ssid, config.pw)) {
      if(setClock()) {
        opMode = run;
      }
      wld.begin(config.minLevel, config.maxLevel);
      tc.begin(getNextTide, config.clockFace, config.motor);
    }
  }

  // Say how that worked out.
  if (opMode == run) {
    Serial.print("All set to run normally. Just need NOAA's cooperation.\n");
  } else {
    Serial.print("Unable to start normally. Hopefully the reason is obvious.\n");
  }
  Serial.print(F("Type h or help for a list of commands.\n"));
}

/**
 * @brief Arduino loop() function. Execute repeatedly after setup() completes.
 */
void loop() {
  static time_t lastWlTime = 0;
  time_t curTime = time(nullptr);
  uint32_t curGmToD = timeToTimeOfDayUTC(curTime);

  // Deal with test mode
  if (opMode == test) {
    static uint32_t stepsToTick = 0;
    static unsigned long lastTickMillis = 0;
    unsigned long curMillis = millis();
    // Take a step towards taking testTeck ticks
    if (testTick > testTicksTaken && curMillis - lastTickMillis >= testNsecs * 1000) {
      if (tc.test()) {
        stepsToTick++;
      }
      if (stepsToTick >= (config.motor == tcOne ? TC_ONE_STEPS_PER_TICK : TC_SIXTEEN_STEPS_PER_TICK)) {
        stepsToTick = 0;
        testTicksTaken++;
        lastTickMillis = curMillis;
      }
      if (testTicksTaken >= testTick) {
        testTick = 0;
        testTicksTaken = 0;
        Serial.print("Tick test complete.\n");
      }
    }
  // Otherwise deal with run and not initialized modes
  } else {
    if (opMode != notInit) {

      // If we're updating the water level display and enough time has passed, do the update
      if (curTime - lastWlTime >= TAT_LEVEL_CHECK_SECS) {
        float waterlevel = getPredWl();
        lastWlTime = curTime;
        if (waterlevel != LEVEL_UNAVAILABLE) {
          wld.setLevel(waterlevel);
        }
      }

      // Let the tide clock do its thing
      tc.run(curTime);
    }
  }

  // Let the water level display do its thing
  wld.run();

  // Let the ui do its thing
  ui.run();
}