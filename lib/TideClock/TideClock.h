/****
 *
 *  TideClock.h
 *  Part of the "TideClock" library for Arduino. Version 0.6.1
 *
 * The TideClock class uses a hacked Lavet motor quartz clock movement -- one of the ubiquitous, cheap
 * quartz mechanisms powered by a single AA cell to display the number of hours to the next tide. It 
 * assumes the movement has been modified by connecting the two ends of its coil to two pins, tickPin 
 * and tockPin, of a microprocessor running Arduino framework-based firmware. 
 * 
 * A clock hacked in this way can be advanced by alternating pulses on the two pins. Each pulse 
 * advances the clock mechanism by one step. I've found two versions of the mechanism. You can tell 
 * which kind you have by installing a second hand and popping in a battery. If the second hand jumps 
 * forward and pauses with each passing second, you have a clock with a type tcOne motor. If the second 
 * hand moves forward smoothly with the passing of time, you have a clock with a tcSixteen motor.
 * 
 * So far I have been unable to make the tcSixteen motor version work reliably. They run fine once 
 * they're started but are a finicky about starting and stopping, which is required here. I've got 
 * a test program I'm using to try to find a set of timing parameters that works, but so far it's 
 * been "close but no cigar." Worse, after stopping, I've seen them end up part way between steps and 
 * completely refuse to start again without bumping them. Tiny forces and odd mechanical resonances. 
 * For now, at least stick with clocks that use type tcOne motors which are designed to run 
 * intermittantly.
 * 
 * TideClock supports two alternative clock face designs. The first, called the linear design, has 
 * traditional tide clock markings and one hand: straight up is high tide and straight down is low 
 * tide. The interval from high and low tide is divided six sub-intervals, showing how many hours there 
 * are from the current time to low tide. E.g., when the hand gets to what on a normal clock is the 
 * "one o'clock" position there are five hours to the upcoming low tide and that position on the face 
 * is labeled with a "5". The "two o'clock" position is labeled with a "4" and so on. The interval from 
 * low tide to high is similarly marked to show the number of hours to the next high tide. 
 * 
 * Because tides usually don't occur regularly every six hours, the clock can't simply count off standard 
 * hours. Over the years, various methods have been employed to make tide clocks show more or less the 
 * correct number of hours to the next tide. Most commonly, they're set to run so that the hand makes a 
 * complete trip around the dial every 12 hours and 25 minutes, which is close to the average time it takes 
 * for the moon to cross the same line of longitude on successive days. That's approximately okay if you 
 * live where the tides more or less follow that pattern (e.g., the eastern seaboard of the US), but it's 
 * really pretty unsatisfactory for many places. Where I live in Port Townsend, WA, the tides are much more 
 * irregular than that because the many tidal rivers, bays, inlets, sounds, and straits all slosh around 
 * and couple to one another under the influence of the moon's (and, to some extent, the sun's) gravity. 
 * This tide clock accommodates irregular tides and shows the correct tide time. Here's how it works.
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
 * NOAA Tides and Currents comes to mind.
 * 
 * To use the TideClock, instantiate a TideCLock object as a global variable, telling it which GPIO
 * pins the Lavet motor is connected to. In the Arduino setup function, use the begin member function to 
 * tell it what the address of the handler function is, whether the clock face is linear or nonlinear and
 * what type of motor your movement has. Then in in the Arduino loop function invoke run member function, 
 * passing the current POSIX time. Do this as often as possible.
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
#define TC_ONE_MIN_STEP_INTERVAL        (200)                   // For tcOne motors, minimum interval between steps (millis())
#define TC_ONE_PULSE_DURATION           (60)                    // For tcOne motors, duration of the step pulses (millis())
#define TC_ONE_STEPS_PER_TICK           (1)                     // For tcOne motors, steps per tick
#define TC_SIXTEEN_MIN_STEP_INTERVAL    (31)                    // For tcSixteen motors, minimum interval between steps (millis())
#define TC_SIXTEEN_PULSE_DURATION       (31)                    // For tcSixteen motors, duration of the step pulses (millis())
#define TC_SIXTEEN_STEPS_PER_TICK       (16)                    // For tcSixteen motors, steps per tick
#define TC_SECONDS_PER_TICK             (12)                    // How many seconds there are in one (linear) clock tick
#define TC_SECONDS_IN_SIX_HOURS         ((uint32_t)6 * 60 * 60) // Six hours in seconds
#define TC_SECONDS_IN_18_HOURS          ((uint32_t)18 * 60 *60) // Eighteen hours in seconds
#define TC_A_COEFFICIENT                (1800.0 / (TC_SECONDS_IN_18_HOURS * TC_SECONDS_IN_18_HOURS)) // a in ticks(t) = a * t**2
#define TC_TICKS_IN_A_CYCLE             (60 * 30)               // Number of ticks between high and low (or low and high) tide
#define TC_ASK_TIDE_MILLIS              (120000UL)              // Rate limit for asking for a tide prediction
#define TC_UNAVAILABLE                  (3)                     // tc_tide_t.tideType when the next tide in not available

typedef uint8_t sx_t;                               // Our unit of time i.e. six minutes -- 1/10th of an hour, 1/240th of a day
enum tc_scale_t : uint8_t {tcLinear, tcNonlinear};  // The type of scale on a clock face
enum tc_motor_t : uint8_t {tcOne, tcSixteen};       // The type of lavet motor: one step/tick or 16 steps/tick
struct tc_tide_t {                                  // A tide event -- the type of event -- high or low -- and when it occurs
    uint8_t tideType;                               //  The type of tide event, HIGH or LOW
    time_t time;                                    //  When the event happens
};
extern "C" {
// Sketch-supplied getNextTide handler: time_t handler(void); It should return a tc_tide_t for the tide extreme
typedef tc_tide_t(*getNextTideHandler_t) (void);
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
 * @param motor (tc_motor_t) The type of motor the clock has
 */
void begin(getNextTideHandler_t h, tc_scale_t type = tcNonlinear, tc_motor_t motor = tcOne);
	
/**
 *
 * @brief The normal run method for the clock display; call as often as possible
 * 
 * @param t (time_t) Current local time in POSIX time
 * 
 */
void run(time_t t);

/**
 * @brief   The test mode run method for the clock display; Step whenever it's possible
 *          without exceeding the motor's speed limit.
 * 
 * @return  true  Step taken
 * @return false  Step not taken (not enough time passed)
 */
bool test();

/**
 * @brief Get the tide event for the next tide. 0 if none.
 * 
 * @return tc_tide_t 
 */
tc_tide_t getNextTide();
  
private:
/***
 * 
 * Member variables
 * 
 ***/
uint8_t tickPin;                        // The pin to pulse to tick the clock forward one second
uint8_t tockPin;                        // The pin to pulse to tock the clock forward one second
bool stepType;                          // The direction of the pulse step() should use next
bool paused;                            // True if we're waiting to get close enough to a tide to run
tc_scale_t faceType;                    // The type of face the clock has; tcLinear or tcNonlinear
tc_motor_t motorType;                   // The type of motor the clock has, tcOne ot tcSixteen
unsigned long stepsPerTick;             // The number of steps per tick for our motor
unsigned long minStepInterval;          // The minimum step interval for our motor (millis())
uint32_t pulseDuration;                 // How long the step pulse is for our motor (millis())
int32_t stepsTaken;                     // The number of steps taken by the clock since the last tide
int32_t stepsNeeded;                    // Number of steps since the last tide needed to indicate correctly
tc_tide_t nextTide;                     // The next tide event
unsigned long gotTideMillis;            // millis() at the time we last asked for the next tide prediction
unsigned long lastMillis;               // millis() the last time step() or test() was invoked
getNextTideHandler_t handler;           // The handler to call for the time of the next high/low tide

/**
 * @brief   Member function invoked to cause the clock mechanism to make one step forward.
 * 
 */
void step();

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
