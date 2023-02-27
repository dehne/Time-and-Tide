/****
 *
 * TideClock.cpp
 * Part of the "TideClock" library for Arduino. Version 0.5.0
 *
 * See tideClock.h for details
 *
 ****
 *
 *  Copyright 2023 by D.L. Ehnebuske
 *  License: GNU Lesser General Public License v2.1
 * 
 ****/

#include "TideClock.h"

/***
 * Constructor
 ***/
TideClock::TideClock(uint8_t iPin, uint8_t oPin) {
  tickPin = iPin;
  tockPin = oPin;
  tick = true;
  paused = false;
  stepsTaken = 0;
  nextTide = 0;
  pinMode(tickPin, OUTPUT);  // Set the tick pin of the clock's Lavet motor to OUTPUT,
  digitalWrite(tickPin, LOW);  //   with a low-impedance path to ground
  pinMode(tockPin, OUTPUT);  // Set the tock pin of the clock's Lavet motor to OUTPUT,
  digitalWrite(tockPin, LOW);  //   with a low impedance path to ground
}

/***
 * begin(h, type)
 ***/
void TideClock::begin(getNextTideHandler_t h, tc_scale_t type) {
  handler = h;
  faceType = type;
  Serial.printf("[TideClock::begin] Using %s clock face.\n", faceType == linear ? "linear" : "nonlinear");
  lastMillis = millis();
}

/***
 * run(t)
 ***/
void TideClock::run(time_t t) {
  unsigned long curMillis = millis();
  // We only need to go as fast as the motor can.
  if (curMillis - lastMillis < TC_MIN_LAVET_STEP_INTERVAL) {
    return;
  }
  lastMillis = curMillis;
  bool firstPass = nextTide == 0;

  // If we don't have a valid predicted time for the next tide, get one
  // and indicate we're starting a new cycle
  bool startingNewCycle = false;
  if (t > nextTide) {
    nextTide = (*handler)();
    startingNewCycle = true;
    stepsTaken = 0;
  }

  // Calculate the relevant quantities
  int32_t secToNextTide = nextTide - t;
  float secFromCycleEnd;
  int32_t stepsNeeded;
  if (faceType == nonlinear) {
    secFromCycleEnd = static_cast<float>(TC_SECONDS_IN_18_HOURS - secToNextTide);
    stepsNeeded = static_cast<int32_t>(TC_A_COEFFICIENT * (secFromCycleEnd * secFromCycleEnd));
  } else {
    secFromCycleEnd = static_cast<float>(TC_SECONDS_IN_SIX_HOURS - secToNextTide);
    stepsNeeded = static_cast<int32_t>(secFromCycleEnd / TC_SECONDS_PER_STEP);
  }

  // If we're starting a new cycle, do the initializaton for it
  if (startingNewCycle) {
    if (secFromCycleEnd < 0) {
      Serial.printf("[TideClock::run %s] New tide %s is (%d seconds) away. Pausing for %d seconds.\n", 
        posixTimeToHHMMSS(t).c_str(), secToHHMMSS(secToNextTide).c_str(), secToNextTide, static_cast<int32_t>(-secFromCycleEnd));
      paused = true;
    } else {
      if (firstPass) {
        Serial.printf("[TideClock::run %s] The tide is %s (%d seconds) away. Check that the clock is set correctly.\n",
        posixTimeToHHMMSS(t).c_str(), secToHHMMSS(secToNextTide).c_str(), secToNextTide);
        stepsTaken = stepsNeeded;       // Assume clock is set correctly.
      } else {
        Serial.printf("[TideClock::run %s] New tide is %s (%d seconds) away. Taking %d quick steps to get on target.\n",
          posixTimeToHHMMSS(t).c_str(), secToHHMMSS(secToNextTide).c_str(), secToNextTide, stepsNeeded - stepsTaken);
      }
    }
  }

  // Deal with being paused
  if (paused) {
    if (secFromCycleEnd == 0) {
      Serial.printf("[TideClock::runNonlinear %s] The tide is %s (%d seconds) away. Starting clock.\n",
        posixTimeToHHMMSS(t).c_str(), secToHHMMSS(secToNextTide).c_str(), secToNextTide);
      paused = false;
    }
    return;
  }
  // If steps are needed, take one
  if (stepsNeeded > stepsTaken) {
    step();
    stepsTaken++;
  }
}

/***
 * getNextTide()
 ***/
time_t TideClock::getNextTide() {
  return nextTide;
}

/***
 * step()
 ***/
void TideClock::step() {
  if (tick) {
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(tickPin, HIGH);  // Issue a tick pulse
    delay(TC_LAVET_PULSE_DURATION);
    digitalWrite(tickPin, LOW);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(tockPin, HIGH);  // Issue a tock pulse
    delay(TC_LAVET_PULSE_DURATION);
    digitalWrite(tockPin, LOW);
  }
  tick = !tick;  // Switch from tick to tock or vice versa
}

/***
 * secToHHMMSS(sec)
 ***/
String TideClock::secToHHMMSS(int32_t sec) {
  sec = abs(sec % 86400);
  char buffer[9];
  snprintf(buffer, 9, "%02d:%02d:%02d", sec / 3600, (sec % 3600) / 60, sec % 60);
  return String(buffer);
}

/***
 * posixTimeToHHMMSS(t)
*/
String TideClock::posixTimeToHHMMSS(time_t t) {
  tm *tAsTM = localtime(&t);
  return String(tAsTM->tm_hour < 10 ? "0" : "") + String(tAsTM->tm_hour) + ":" +
    String(tAsTM->tm_min < 10 ? "0" : "") + String(tAsTM->tm_min) + ":" +
    String(tAsTM->tm_sec < 10 ? "0" : "") + String(tAsTM->tm_sec);
}