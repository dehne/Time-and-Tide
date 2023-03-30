/****
 *
 * WDisplay.cpp
 * Part of the "WlDisplay" library for Arduino. Version 0.5.0
 *
 * See WlDisplay.h for details
 *
 ****
 *
 *  Copyright 2023 by D.L. Ehnebuske
 *  License: GNU Lesser General Public License v2.1
 * 
 ****/
#include <WlDisplay.h>

/***
 * Constructor
 ***/
WlDisplay::WlDisplay(uint8_t sp1, uint8_t sp2, uint8_t sp3, uint8_t sp4, uint8_t lp, uint8_t pp) {
  stepper = new GStepper<STEPPER4WIRE_HALF>(WLD_STEPS_PER_TURN, sp1, sp2, sp3, sp4);
  limitPin = lp;
  powerPin = pp;
  ready = false;
}

/***
 * Destructor
 ***/
WlDisplay::~WlDisplay() {
  delete stepper;
}

/***
 * begin()
 ***/
void WlDisplay::begin(float minL, float maxL) {
  minLevel = minL;
  maxLevel = maxL;
  stepsPerFoot = (int32_t)((WLD_MIN_POS / maxL) - 0.5);
  pinMode(limitPin, INPUT_PULLUP);
  pinMode(powerPin, INPUT_PULLDOWN);
  powerIsOn = digitalRead(powerPin) == HIGH;
  powerUnstable = true;
  stepper->autoPower(true);
  log_d("[WlDisplay::begin] Stepper parms - stepsPerFoot: %d, pos at minLevel: %d, pos at maxLevel: %d.\n",
    stepsPerFoot, (int16_t)(minLevel * stepsPerFoot), (int16_t)(maxLevel * stepsPerFoot));
}

/***
 *  bool home()
 ***/
bool WlDisplay::home() {
  if (digitalRead(powerPin) == HIGH) {
    stepper->setRunMode(KEEP_SPEED);
    stepper->setSpeedDeg(WLD_HOMING_DEG_PER_SEC);
    while (digitalRead(limitPin) == HIGH) {
      stepper->tick();
    }
    stepper->setRunMode(FOLLOW_POS);
    stepper->setMaxSpeed(600);
    stepper->setCurrent(stepsPerFoot * minLevel);
  }
  return digitalRead(powerPin) == HIGH;  // Could have pulled the plug half-way through
}

/***
 * setLevel(level)
 ***/
void WlDisplay::setLevel(float level) {
  if (level > maxLevel || level < minLevel) {
    Serial.printf("[WlDisplay::setLevel] Ignoring out-of-range water level: %f.\n", level);
    return;
  }
  curLevel = level;
  long target = curLevel * stepsPerFoot;
  stepper->setTarget(target);
  log_d("[WlDisplay::setLevel] Water level set to %f (stepper target %d).\n", curLevel, target);
}

/***
 * float getLevel()
 ***/
float WlDisplay::getLevel() {
  return curLevel;
}

/***
 * run()
 ***/
void WlDisplay::run() {
  // Figure out what's going on with the USB power
  unsigned long curMillis = millis();
  bool powerCameOn = false;
  bool curPowerIsOn = digitalRead(powerPin) == HIGH;
  if (powerUnstable) {
    if (curMillis - becameUnstableMillis < WLD_ENOUGH_MILLIS) {
      return;
    }
    powerUnstable = false;
    powerCameOn = curPowerIsOn && !powerIsOn;
    powerIsOn = curPowerIsOn;
  } else {
    powerUnstable = curPowerIsOn != powerIsOn;
    if (powerUnstable) {
      becameUnstableMillis = curMillis;
      return;
    }
  }

  // If the power just came on, do a home() just to be on the safe side.
  if (powerCameOn) {
    Serial.print("[WlDisplay::run] Homing the water level display.\n");
    home();
  }

  if (powerIsOn) {
    // If we're not ready, try to home the device
    if (!ready && powerIsOn) {
      if (home()) {
        ready = true;
      } else {
        return;
      }
    }
    stepper->tick();
  }
}
