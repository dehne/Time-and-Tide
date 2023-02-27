/****
 *
 *  TideClock.h
 *  Part of the "TideClock" library for Arduino. Version 0.5.0
 *
 * The TideClock class makes a hacked Lavet motor quartz clock movements -- one of the ubiquitous, cheap
 * quartz mechanisms powered by a single AA cell. It assumes the movement has been modified to bypass 
 * the quartz timing mechanism by tying the connections to the ends of its coil to two pins, tickPin 
 * and tockPin, of the Arduino. A clock hacked in this way can be advanced by alternating pulses on the 
 * two pins. Each pulse advances the clock mechanism by one second.
 * 
 * For this application, there are two alternative clock face designs. The first, called the linear 
 * design, has traditional tide clock markings and one hand: straight up is high tide and straight down 
 * is low tide. The interval from high and low tide is divided six sub-intervals, showing how many hours 
 * there are from the current time to low tide. E.g., when the hand gets to the "one o'clock" position 
 * there are five hours to the upcoming low tide. The interval from low tide to high is similarly 
 * subdivided to show the number of hours to the next low tide. 
 * 
 * Because tides usually don't occur regularly every six hours, the clock can't simply count off standard 
 * hours. Over the years, various methods have been employed to make tide clocks show more or less the 
 * correct number of hours to the next tide. Most commonly, they're set to run so that the hand makes a 
 * complete trip around the dial every 12 hours and 50 minutes, which is close to the average time it takes 
 * for the moon to cross the same line of longitude on successive days. That's approximately okay if you 
 * live where the tides more or less follow that pattern (e.g., the eastern seaboard of the US), but it's 
 * really pretty unsatisfactory for many places. Where I live in Port Townsend, WA, the tides are much more 
 * irregular than that because the many tidal rivers, bays, inlets, sounds, and straits all slosh around 
 * and couple to one another under the influence of the moon's (and, to some extent, the sun's) gravity. 
 * This tide clock accomodates irregular tides and shows the correct tide time. Here's how it works.
 * 
 * Normally, the time on the face progresses at the same rate a normal clock does, thus showing the correct 
 * number of hours to the next tide, but when its position indicates it's at the next tide -- high tide, 
 * say -- TideClock makes a call to a "get next tide" handler function. The handler returns the POSIX time 
 * of the next tide (low in this case). If the time until the next tide is more than six hours off, the clock  
 * pauses for the appropriate amount of time before starting up again. If, on the other hand, the time to the 
 * next (low) tide is less than six hours away, the clock ticks as fast as it can until it points to the 
 * right place and then resumes moving at normal clock-speed once more. Things work exactly the same way when 
 * the clock gets to low tide: It asks the handler for the time of the next high tide and then pauses or 
 * races forward as needed before beginning once again to run normally.
 * 
 * The second face design, called the nonlinear design, also shows the correct time to the next tide, but 
 * does so in a different way. Instead of showing six hours between tides, it shows 18. But the spacing is 
 * very nonlinear. When it's a long time to the next tide, the speed at which the hand travels is very low. 
 * As the time to the next tide gets closer, the speed with which the hand travels gradually increases. When 
 * when the high or low tide is reached, it uses the "get next tide" handler to figure out how much time 
 * there is until the next tide and pauses (hardly ever) or zips forward until the hand points to the 
 * correct place. From there, it resumes moving, but nonlinearly, of course.
 * 
 * With both face designs, the single hand is attached to the quartz clock mechanism's minute hand, even 
 * though it indicates hours. No hands are attached to the mechanism's other hand positions.
 * 
 * How the "get next tide" handler function works isn't a concern of the clock, but typically it works by
 * asking an online tide model service for the requisite information at the location of interest. For the US, 
 * NOAA comes to mind.
 * 
 * To use the TideClock, instantiate a TideCLock object as a global variable, telling it which GPIO
 * pins the Lavet motor is connected to. In the Arduino setup function, use the begin member function to 
 * tell it what the address of the handler function is and whether the clock face is linear or nonlinear. 
 * Then in in the Arduino loop function invoke run member function, passing the current POSIX time. Do 
 * this at least once every six seconds, or more often if you don't have anyting better for the Arduino to 
 * do.
 * 
 * TideClock assumes that the clock's position has been set manually at the time of the first call to 
 * run().
 *
 ****
 *
 *  Copyright 2023 by D.L. Ehnebuske
 *  License: GNU Lesser General Public License v2.1
 * 
 ****/
#pragma once

#include <Arduino.h>  // Arduino 1.0

// Some constants
#define TC_MIN_LAVET_STEP_INTERVAL  (200)		            // Minimum interval between steps of the Lavet motor (ms)
#define TC_LAVET_PULSE_DURATION     (60)		            // Duration of the pulses that move the Lavet motor (ms)
#define TC_SECONDS_PER_STEP         (12)                    // How many seconds there are in one (linear) clock step
#define TC_SECONDS_IN_SIX_HOURS     ((uint32_t)6 * 60 * 60) // Six hours in seconds
#define TC_SECONDS_IN_18_HOURS      ((uint32_t)18 * 60 *60) // Eighteen hours in seconds
#define TC_A_COEFFICIENT            (1800.0 / (TC_SECONDS_IN_18_HOURS * TC_SECONDS_IN_18_HOURS)) // a in steps(t) = a * t**2

typedef uint8_t sx_t;                           // Our unit of time i.e. six minutes -- 1/10th of an hour, 1/240th of a day
enum tc_scale_t : uint8_t {linear, nonlinear};  // The type of scale on a clock face
extern "C" {
// Sketch-supplied getNextTide handler: time_t handler(void); It should return the time (in POSIX time_t) of the tide extreme
typedef time_t(*getNextTideHandler_t) (void);
}

class TideClock {
public:
/**
 * @brief Construct a new Tide Clock object
 * 
 * @param iPin (uint8_t)            The digital GPIO pin to which the clock's tick input is attached
 * @param oPin (uint8_t)            The digital GPIO pin to which the clock's tock input is attached
 * 
 */
TideClock(uint8_t iPin, uint8_t oPin);

/**
 * @brief Initialize the TideClock
 * 
 * @param h     (getNextTideHandler_t) The handler to call to get the POSIX time of the next tide extreme
 * @param type  (tc_scale_t) The type of face the clock has
 */
void begin(getNextTideHandler_t h, tc_scale_t type = nonlinear);
	
/**
 *
 * @brief The run method for the clock display; call as often as possible
 * 
 * @param t (time_t) Current local time in POSIX time
 * 
 */
void run(time_t t);

/**
 * @brief Get the POSIX time of the next tide. 0 if none.
 * 
 * @return time_t 
 */
time_t getNextTide();
  
private:
/***
 * 
 * Member variables
 * 
 ***/
uint8_t tickPin;                        // The pin to pulse to tick the clock forward one second
uint8_t tockPin;                        // The pin to pulse to tock the clock forward one second
bool tick;                              // Step should "tick" the clock if true, "tock" it if not
bool paused;                            // True if we're waiting to get close enough to a tide to run
tc_scale_t faceType;                    // The type of face the clock has; linear or nonlinear
int32_t stepsTaken;                     // The number of steps taken by the clock since the last tide
time_t nextTide;                        // The POSIX time of the next tide extreme (high or low)
unsigned long lastMillis;               // millis() the last time step() was invoked
getNextTideHandler_t handler;           // The handler to call for the time of the next high/low tide

/**
 * @brief   Member function invoked to cause the clock mechanism to make one step forward. One step is 1/300 of 
 *          an hour -- 12 seconds.
 * 
 */
void step();							// Step clock forward one step -- 12 seconds

/**
 * @brief Convert the given count in seconds past midnight to "hh:mm:ss"
 * 
 * @param sec the int_32 to be converted
 * @return String 
 */
String secToHHMMSS(int32_t sec);

/**
 * @brief Convert the given POSIX time to "hh:mm:ss"
 * 
 * @param t The POSIX time to be converted
 * @return String 
 */
String posixTimeToHHMMSS(time_t t);
};
