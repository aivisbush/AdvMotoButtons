/* The eight inputs: four joystick directions, joystick centre and buttons
 * A, B, C. Directions are logical, i.e. already mapped through the
 * mounting orientation; the physical pin behind each one changes with
 * the orientation, nothing else does.
 */
#pragma once

#include <Arduino.h>
#include "config.h"

enum ButtonId : uint8_t
{
  BUTTON_UP = 0,
  BUTTON_DOWN,
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_CENTER,
  BUTTON_A,
  BUTTON_B,
  BUTTON_C,
  BUTTON_COUNT
};

struct Button
{
  const char *name;                 // logical name, used in logs
  uint8_t pin;                      // physical GPIO currently behind this input
  uint16_t debounceMs;              // how long a reading must hold to become the state
  bool state;                       // debounced state, true = pressed
  bool priorReading;                // last raw reading, for the debounce filter
  unsigned long lastReadingChangeMs;// when the raw reading last changed
  unsigned long stateSinceMs;       // when the debounced state last changed
};

extern Button buttons[BUTTON_COUNT];

// Configures the pins and reads the initial states.
void inputsBegin(uint8_t orientation);
// Re-points the four direction inputs at their pins for this orientation.
void inputsSetOrientation(uint8_t orientation);
// Samples every input once. Returns true when a debounced state changed.
bool inputsUpdate();

bool buttonHeld(ButtonId id);
// Milliseconds the button has been in its pressed state, 0 when released.
unsigned long buttonHeldMs(ButtonId id);
// A direction, with a simultaneous opposite direction treated as noise.
bool directionActive(ButtonId id);
// The single direction currently held, or -1 for none or more than one.
int8_t inputsHeldDirection();
// One bit per input, bit i = buttons[i].state.
uint8_t inputsButtonMask();
// millis() of the last debounced state change, for idle detection.
unsigned long inputsLastActivityMs();

// Orientation index whose UP is this physical pin, or -1.
int8_t orientationForUpPin(uint8_t pin);
// Physical direction name of a joystick pin.
const char *joystickPinName(uint8_t pin);

// DEBUG_INPUTS diagnostics.
void inputsLogHeader();
void inputsDiagnosticsTick();
