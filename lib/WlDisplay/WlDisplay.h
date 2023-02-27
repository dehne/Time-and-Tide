/****
 *
 * WDisplay.h
 * Part of the "WlDisplay" library for Arduino. Version 0.5.0
 *
 * A WlDisplay object is the software interface to a water level display that shows the current 
 * water level for a tide clock display device. It's powered by a 28BYJ-48 stepper via a 
 * ULN2003-based driver board using the GyverStepper library. The stepper runs a chain drive that 
 * raises and lowers a drawing of the sea surface to cover and uncover a drawing of the beach and 
 * land as seen from off shore, thus displaying the current water level. The display runs from 
 * minLevel (by default, WLD_MIN_LEVEL), the lowest water level it can display to maxLevel (by 
 * default WLD_MAX_LEVEL), the maximum. If requested to display a level outside this range, it 
 * ignores the request. Internally, the stepper position (in steps) corresponding to minLevel 
 * is WLD_BOTTOM_POS; maxLevel corresponds to WLD_TOP_POS.
 * 
 * The stepper runs on 5V from the USB input power, the ESP32-S2 we run on has a backup battery 
 * so it can keep going if unplugged for a while. If there's no USB power, we can't move the 
 * display, of course. To detect the state of the USB power, there's a "power present" signal on
 * the GPIO pin powerPin. It's HIGH when power is present.
 * 
 * When USB power comes up, the display has no idea where it is in the range. So, the device 
 * contains a 3144 Hall-effect sensor and magnet that mark the physical position corresponding 
 * to WLD_BOTTOM_POS. To home the device, we drive the stepper down (which, it turns out, is 
 * clockwise) until the Hall-effect sensor trips.
 * 
 * The typical way to use WlDisplay is to create a WlDisplay object as a global variable. Then 
 * invoke the begin member function in the Arduino setup() to do the initializaton. While running, 
 * use the setLevel member function whenever a new water level needs to be shown. Call the run 
 * member function at each pass through the Arduino loop function to give the stepper a chance to 
 * do its thing. Don't worry about USB power coming and going; the display will show the correct 
 * level whenever power is available but just remain still if it's not.
 *
 ****
 *
 *  Copyright 2023 by D.L. Ehnebuske
 *  License: GNU Lesser General Public License v2.1
 * 
 ****/
#pragma once

#include <Arduino.h>
#include <GyverStepper.h>   // The header for the Gyver stepper driver library

// Some constants
#define WLD_MIN_POS             (-1200)     // Stepper position corresponding to water level == maxLevel
#define WLD_MIN_LEVEL           (-4.3)      // Minimum displayable water level (feet MLLW)
#define WLD_MAX_LEVEL           (12.1)      // Maximum displayable water level (feet MLLW)
#define WLD_STEPS_PER_TURN      (2048)      // Number of steps per turn for the 28BYJ-48 stepper
#define WLD_HOMING_DEG_PER_SEC  (30)        // The speed (and direction) used to approach the limit switch (degrees/sec)
#define WLD_ENOUGH_MILLIS       (100)       // This many millis must have elapsed before we believe the power state is stable

class WlDisplay {
public:
  /**
   * 
   * @brief Construct a new Water Level Display object
   * 
   * @param sp1 GPIO pin to which stepper motor IN4 is attached
   * @param sp2 GPIO pin to which stepper motor IN2 is attached
   * @param sp3 GPIO pin to which stepper motor IN3 is attached
   * @param sp4 GPIO pin to which stepper motor IN1 is attached
   * @param lp  GPIO pin to which the Hall-effect sensor is attached
   * @param pp  GPIO pin to which the "power present" signal is attached
   * 
   */
  WlDisplay(uint8_t sp1, uint8_t sp2, uint8_t sp3, uint8_t sp4, uint8_t lp, uint8_t pp);

  /**
   *
   *  @brief Destructor
   * 
   */
  ~ WlDisplay();

  /**
   * 
   * @brief Initialize the WlDisplay. Call once in Arduino setup function
   * 
   * @param minL The minimum displayable water level
   * @param maxL The maximum displayable water level
   * 
   */
  void begin(float minL = WLD_MIN_LEVEL, float maxL = WLD_MAX_LEVEL);

  /**
   * @brief Recalibrate the WlDisplay by driving its position to the limit 
   *        switch. When done, the display shows the minimum displayable 
   *        water level.
   * 
   * @return true Homing successful, we're ready to go
   * @return false Homing could not be done, device is not ready
   * 
   */
  bool home();

  /**
   * @brief Set the level of the water shown in the display
   * 
   * @param level The water level the display is to show
   */
  void setLevel(float level);

  /**
   *
   * @brief Let the display do its thing to keep updated
   * 
   */
  void run();

private:
  GStepper<STEPPER4WIRE_HALF> *stepper;     // The stepper motor
  uint8_t limitPin;                         // The GPIO pin to which the Hall-effect sensor is attached
  uint16_t powerPin;                        // The GPIO pin to which the "power present" signal is attached
  int32_t minPos;                           // Stepper position (steps) at minLevel
  int32_t maxPos;                           // Stepper position (steps) at maxLevel
  int32_t stepsPerFoot;                     // The number of steps of the stepper per foot of water level
  float minLevel;                           // The minimum displayable water level
  float maxLevel;                           // The maximum displayable water level
  float curLevel;                           // Currently displayed level (feet above MLLW)
  bool ready;                               // True if ready to go: power is present and we did a home()
  bool powerIsOn;                           // The state of the power the last time we decided about it
  bool powerUnstable;                       // Becomes true when power state is stable but changes, false WLD_ENOUGH_MILLIS later
  unsigned long becameUnstableMillis;       // millis() at the time powerUnstable last became true
};