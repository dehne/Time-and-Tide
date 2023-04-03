/****
 *
 * TideClock.cpp
 * Part of the "TideClock" library for Arduino. Version 0.6.1
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
  stepType = true;
  paused = false;
  stepsTaken = 0;
  nextTide.tideType = TC_UNAVAILABLE;
  nextTide.time = 0;
  pinMode(tickPin, OUTPUT);             // Set the tick pin of the clock's Lavet motor to OUTPUT,
  digitalWrite(tickPin, LOW);           //   with a low-impedance path to ground
  pinMode(tockPin, OUTPUT);             // Set the tock pin of the clock's Lavet motor to OUTPUT,
  digitalWrite(tockPin, LOW);           //   with a low impedance path to ground
}

/***
 * begin(h, type)
 ***/
void TideClock::begin(getNextTideHandler_t h, tc_scale_t type, tc_motor_t motor) {
  handler = h;
  faceType = type;
  motorType = motor;
  if (motorType == tcOne) {
    stepsPerTick = TC_ONE_STEPS_PER_TICK;
    minStepInterval = TC_ONE_MIN_STEP_INTERVAL;
    pulseDuration = TC_ONE_PULSE_DURATION;
  } else {
    stepsPerTick = TC_SIXTEEN_STEPS_PER_TICK;
    minStepInterval = TC_SIXTEEN_MIN_STEP_INTERVAL;
    pulseDuration = TC_SIXTEEN_PULSE_DURATION;
  }
  Serial.printf("[TideClock::begin] Using %s clock face with type %s motor.\n", 
    faceType == tcLinear ? "linear" : "nonlinear", motorType == tcOne ? "one" : "sixteen");
  lastMillis = millis();
  gotTideMillis = lastMillis - TC_ASK_TIDE_MILLIS;
}

/***
 * run(t)
 ***/
void TideClock::run(time_t t) {
  unsigned long curMillis = millis();
  // We only need to go as fast as the motor can.
  if (curMillis - lastMillis < minStepInterval) {
    return;
  }
  lastMillis = curMillis;
  // If steps are needed, take one
  if (stepsNeeded > stepsTaken) {
    step();
    stepsTaken++;
    return;
  }
  
  // All caught up. Figure out what's next
  bool firstPass = nextTide.time == 0;            // Whether we're just starting
  bool startingNewCycle = false;                  // Whether we're starting a new tide cycle
  bool missedCycle = false;                       // Whether clock showing wrong cycle phase. E.g., high instead of low

  // If we're past the time of the next tide, deal with it
  if (t > nextTide.time) {
    if (curMillis - gotTideMillis < TC_ASK_TIDE_MILLIS) {
      return;                                     // Don't try to get the new tide data too often
    }
    // Get the new tide data
    tc_tide_t newTide = (*handler)();
    gotTideMillis = curMillis;
    if (newTide.tideType == TC_UNAVAILABLE) {
      return;
    }
    // Set up to start a new tide cycle
    missedCycle = nextTide.tideType == newTide.tideType;
    nextTide = newTide;                            
    startingNewCycle = true;
    stepsTaken = 0;
  }
  // Calculate the new value for stepsNeeded
  int32_t secToNextTide = static_cast<int32_t>(nextTide.time - t);
  float secFromCycleEnd;
  if (faceType == tcNonlinear) {
    secFromCycleEnd = static_cast<float>(TC_SECONDS_IN_18_HOURS - secToNextTide);
    stepsNeeded = stepsPerTick * static_cast<int32_t>(TC_A_COEFFICIENT * (secFromCycleEnd * secFromCycleEnd));
  } else {
    secFromCycleEnd = static_cast<float>(TC_SECONDS_IN_SIX_HOURS - secToNextTide);
    stepsNeeded = stepsPerTick * static_cast<int32_t>(secFromCycleEnd / TC_SECONDS_PER_TICK);
  }
  
  // Deal with starting a new tide cycle
  if (startingNewCycle) {
    if (missedCycle) {
      stepsNeeded += stepsPerTick * TC_TICKS_IN_A_CYCLE;
      Serial.printf("[TideClock::run %s] Missed at least a whole tide cycle, but now have data.\n", posixTimeToHHMMSS(t).c_str());
    }
    String highOrLow = nextTide.tideType == HIGH ? "high" : "low";
    if (secFromCycleEnd < 0 && !missedCycle) {
      Serial.printf("[TideClock::run %s] New tide (%s) is %s away. Pausing for %d seconds.\n", 
        posixTimeToHHMMSS(t).c_str(), highOrLow, secToHHMMSS(secToNextTide).c_str(), static_cast<int32_t>(-secFromCycleEnd));
      paused = true;
    } else {
      if (firstPass) {
        Serial.printf("[TideClock::run %s] The tide (%s) is %s away. Check that the clock is set correctly.\n",
        posixTimeToHHMMSS(t).c_str(), highOrLow, secToHHMMSS(secToNextTide).c_str());
        stepsTaken = stepsNeeded;       // Assume clock is set correctly.
      } else {
        Serial.printf("[TideClock::run %s] New tide (%s) is %s away. Taking %d quick steps to get on target.\n",
          posixTimeToHHMMSS(t).c_str(), highOrLow, secToHHMMSS(secToNextTide).c_str(), stepsNeeded - stepsTaken);
      }
    }
  }

  // Deal with being paused
  if (paused) {
    if (secFromCycleEnd == 0) {
      Serial.printf("[TideClock::run %s] The tide is %s (%d seconds) away. Starting clock.\n",
        posixTimeToHHMMSS(t).c_str(), secToHHMMSS(secToNextTide).c_str(), secToNextTide);
      paused = false;
    }
  }
}

/***
 * test()
 ***/
bool TideClock::test() {
  unsigned long curMillis = millis();
  nextTide.time = 0;
  if (curMillis - lastMillis < minStepInterval) {
    return false;
  }
  lastMillis = curMillis;
  step();
  return true;
}

/***
 * getNextTide()
 ***/
tc_tide_t TideClock::getNextTide() {
  return nextTide;
}

/***
 * step()
 ***/
void TideClock::step() {
  if (stepType) {
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(tickPin, HIGH);  // Issue a forward pulse
    delay(pulseDuration);
    digitalWrite(tickPin, LOW);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(tockPin, HIGH);  // Issue a backward pulse
    delay(pulseDuration);
    digitalWrite(tockPin, LOW);
  }
  stepType = ! stepType;          // Switch from forward pulse to backward or vice versa
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