#include "inputs.h"
#include "debug.h"
#include "driver/gpio.h"

// Logical order matches ButtonId. Direction pins are placeholders until
// inputsSetOrientation() assigns them.
Button buttons[BUTTON_COUNT] = {
  {"UP", PIN_JOYSTICK_UP, DEBOUNCE_JOYSTICK_MS, false, false, 0, 0},
  {"DOWN", PIN_JOYSTICK_DOWN, DEBOUNCE_JOYSTICK_MS, false, false, 0, 0},
  {"LEFT", PIN_JOYSTICK_LEFT, DEBOUNCE_JOYSTICK_MS, false, false, 0, 0},
  {"RIGHT", PIN_JOYSTICK_RIGHT, DEBOUNCE_JOYSTICK_MS, false, false, 0, 0},
  {"CENTER", PIN_JOYSTICK_CENTER, DEBOUNCE_JOYSTICK_MS, false, false, 0, 0},
  {"A", PIN_BUTTON_A, DEBOUNCE_BUTTON_MS, false, false, 0, 0},
  {"B", PIN_BUTTON_B, DEBOUNCE_BUTTON_MS, false, false, 0, 0},
  {"C", PIN_BUTTON_C, DEBOUNCE_BUTTON_MS, false, false, 0, 0},
};

// Physical pin behind logical UP, DOWN, LEFT, RIGHT for each orientation.
static const uint8_t ORIENTATION_DIRECTION_PINS[ORIENTATION_COUNT][4] = {
  {PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN},   // 0: three buttons on top
  {PIN_JOYSTICK_DOWN, PIN_JOYSTICK_UP, PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_LEFT},   // 1: three buttons on left
  {PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_UP},   // 2: three buttons on bottom
  {PIN_JOYSTICK_UP, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_LEFT, PIN_JOYSTICK_RIGHT},   // 3: three buttons on right
};

static const uint8_t JOYSTICK_PINS[] = {
  PIN_JOYSTICK_UP, PIN_JOYSTICK_CENTER, PIN_JOYSTICK_RIGHT, PIN_JOYSTICK_DOWN, PIN_JOYSTICK_LEFT
};
static const uint8_t JOYSTICK_PIN_COUNT = sizeof(JOYSTICK_PINS) / sizeof(JOYSTICK_PINS[0]);

// Latched decision per joystick GPIO (0..4): the band between the two
// thresholds keeps whatever the line last decided.
static bool joystickLatched[JOYSTICK_PIN_COUNT] = {false, false, false, false, false};

static unsigned long lastActivityMs = 0;

// On the ESP32-C3 only GPIO0..GPIO4 reach ADC1, which is exactly the joystick.
static bool isJoystickPin(uint8_t pin)
{
  return pin < JOYSTICK_PIN_COUNT;
}

/* Attaching the ADC to a pad clears its internal pull-up, so the pull-ups
 * are asserted again before each scan. Without them an open contact
 * floats and reads anywhere, often near zero, which looks like a press.
 */
static void assertJoystickPullups()
{
  for (uint8_t i = 0; i < JOYSTICK_PIN_COUNT; i++)
  {
    gpio_pullup_en((gpio_num_t)JOYSTICK_PINS[i]);
    gpio_pulldown_dis((gpio_num_t)JOYSTICK_PINS[i]);
  }
}

// Puts a joystick pad into analog mode once, then restores the pull-up.
static void primeJoystickPin(uint8_t pin)
{
  analogSetPinAttenuation(pin, ADC_11db);
  analogRead(pin); // attaches the ADC, which is what clears the pull-up
  gpio_pullup_en((gpio_num_t)pin);
  gpio_pulldown_dis((gpio_num_t)pin);
}

// Median of a few conversions. The caller has already asserted the pull-ups.
static uint16_t readJoystickMillivolts(uint8_t pin)
{
  uint16_t samples[JOYSTICK_ADC_SAMPLES];
  for (uint8_t i = 0; i < JOYSTICK_ADC_SAMPLES; i++)
    samples[i] = (uint16_t)analogReadMilliVolts(pin);

  // Insertion sort; the array is tiny.
  for (uint8_t i = 1; i < JOYSTICK_ADC_SAMPLES; i++)
  {
    uint16_t value = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > value)
    {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = value;
  }

  return samples[JOYSTICK_ADC_SAMPLES / 2];
}

static bool readJoystickPressed(uint8_t pin)
{
  uint16_t millivolts = readJoystickMillivolts(pin);

  if (millivolts <= JOYSTICK_PRESS_MV)
    joystickLatched[pin] = true;
  else if (millivolts >= JOYSTICK_RELEASE_MV)
    joystickLatched[pin] = false;

  return joystickLatched[pin];
}

// Joystick pins are decided by measured level, A/B/C by digital level.
static bool readPressed(uint8_t pin)
{
  if (isJoystickPin(pin))
    return readJoystickPressed(pin);
  return digitalRead(pin) == LOW;
}

// Takes the current readings as the state, with no press events.
static void resetButtonStates()
{
  assertJoystickPullups();
  delayMicroseconds(JOYSTICK_ADC_SETTLE_US);

  unsigned long now = millis();
  for (uint8_t i = 0; i < BUTTON_COUNT; i++)
  {
    Button &button = buttons[i];
    bool reading = readPressed(button.pin);
    button.state = reading;
    button.priorReading = reading;
    button.lastReadingChangeMs = now;
    button.stateSinceMs = now;
  }
}

void inputsBegin(uint8_t orientation)
{
  pinMode(PIN_BUTTON_A, INPUT_PULLUP);
  pinMode(PIN_BUTTON_B, INPUT_PULLUP);
  pinMode(PIN_BUTTON_C, INPUT_PULLUP);

  // The joystick pads are measured, not read digitally.
  for (uint8_t i = 0; i < JOYSTICK_PIN_COUNT; i++)
    primeJoystickPin(JOYSTICK_PINS[i]);

  inputsSetOrientation(orientation);
}

void inputsSetOrientation(uint8_t orientation)
{
  if (orientation >= ORIENTATION_COUNT)
    orientation = DEFAULT_ORIENTATION;

  for (uint8_t direction = 0; direction < 4; direction++)
    buttons[BUTTON_UP + direction].pin = ORIENTATION_DIRECTION_PINS[orientation][direction];

  resetButtonStates();
}

static void logInputEvent(const Button &button, bool pressed, unsigned long previousStateMs);

/* Debounce filter after https://docs.arduino.cc/built-in-examples/digital/Debounce
 * A reading has to stay unchanged for the button's debounce time before it
 * becomes the state.
 */
bool inputsUpdate()
{
  assertJoystickPullups();
  delayMicroseconds(JOYSTICK_ADC_SETTLE_US);

  unsigned long now = millis();
  bool anyChanged = false;

  for (uint8_t i = 0; i < BUTTON_COUNT; i++)
  {
    Button &button = buttons[i];
    bool reading = readPressed(button.pin);

    // The reading is still moving: restart the settle timer.
    if (reading != button.priorReading)
    {
      button.priorReading = reading;
      button.lastReadingChangeMs = now;
      continue;
    }

    if (reading == button.state || now - button.lastReadingChangeMs < button.debounceMs)
      continue;

    unsigned long previousStateMs = now - button.stateSinceMs;
    button.state = reading;
    button.stateSinceMs = now;
    lastActivityMs = now;
    anyChanged = true;

    if (DEBUG_INPUTS)
      logInputEvent(button, reading, previousStateMs);
    else
      debugPrintf("Button %s %s\n", button.name, reading ? "pressed" : "released");
  }

  return anyChanged;
}

bool buttonHeld(ButtonId id)
{
  return buttons[id].state;
}

unsigned long buttonHeldMs(ButtonId id)
{
  if (!buttons[id].state)
    return 0;
  return millis() - buttons[id].stateSinceMs;
}

/* A 5-way joystick cannot physically report opposite directions at the
 * same time, so when it does, one of them is interference: ignore the
 * pair until it resolves. The OLED bar still shows the raw states.
 */
bool directionActive(ButtonId id)
{
  switch (id)
  {
  case BUTTON_UP:
    return buttons[BUTTON_UP].state && !buttons[BUTTON_DOWN].state;
  case BUTTON_DOWN:
    return buttons[BUTTON_DOWN].state && !buttons[BUTTON_UP].state;
  case BUTTON_LEFT:
    return buttons[BUTTON_LEFT].state && !buttons[BUTTON_RIGHT].state;
  case BUTTON_RIGHT:
    return buttons[BUTTON_RIGHT].state && !buttons[BUTTON_LEFT].state;
  default:
    return buttons[id].state;
  }
}

int8_t inputsHeldDirection()
{
  int8_t held = -1;
  for (uint8_t id = BUTTON_UP; id <= BUTTON_RIGHT; id++)
  {
    if (!buttons[id].state)
      continue;
    if (held >= 0)
      return -1; // ambiguous: more than one direction held
    held = (int8_t)id;
  }
  return held;
}

uint8_t inputsButtonMask()
{
  uint8_t mask = 0;
  for (uint8_t i = 0; i < BUTTON_COUNT; i++)
  {
    if (buttons[i].state)
      mask |= (uint8_t)(1u << i);
  }
  return mask;
}

unsigned long inputsLastActivityMs()
{
  return lastActivityMs;
}

int8_t orientationForUpPin(uint8_t pin)
{
  for (uint8_t orientation = 0; orientation < ORIENTATION_COUNT; orientation++)
  {
    if (ORIENTATION_DIRECTION_PINS[orientation][0] == pin)
      return (int8_t)orientation;
  }
  return -1;
}

const char *joystickPinName(uint8_t pin)
{
  switch (pin)
  {
  case PIN_JOYSTICK_UP:
    return "UP";
  case PIN_JOYSTICK_DOWN:
    return "DOWN";
  case PIN_JOYSTICK_LEFT:
    return "LEFT";
  case PIN_JOYSTICK_RIGHT:
    return "RIGHT";
  default:
    return "?";
  }
}

/*------------------------- INPUT DIAGNOSTICS ------------------------*/
static void logInputEvent(const Button &button, bool pressed, unsigned long previousStateMs)
{
  Serial.printf("[%8lu] %-6s GPIO%-2u %-8s  previous state held %6lu ms",
                millis(), button.name, button.pin, pressed ? "PRESSED" : "released", previousStateMs);
  if (isJoystickPin(button.pin))
    Serial.printf("  line %4u mV", readJoystickMillivolts(button.pin));
  Serial.println();
}

/* Periodic table of every input. For the joystick, P/- is the decision the
 * millivolt thresholds made; for A/B/C it is the digital level.
 *   ~2984 mV = idle at the rail (the ADC ceiling on this chip)
 *   under 200 mV = contact truly closed
 *   in between   = leakage dragging the line down; ignored as a press
 */
static void logInputSnapshot()
{
  Serial.printf("[%8lu] ", millis());
  for (uint8_t i = 0; i < BUTTON_COUNT; i++)
  {
    const Button &button = buttons[i];
    if (isJoystickPin(button.pin))
    {
      Serial.printf("%s=%c/%umV", button.name, joystickLatched[button.pin] ? 'P' : '-',
                    readJoystickMillivolts(button.pin));
    }
    else
    {
      Serial.printf("%s=%c", button.name, digitalRead(button.pin) ? 'H' : 'L');
    }
    Serial.print(i + 1 < BUTTON_COUNT ? "  " : "\n");
  }
}

void inputsLogHeader()
{
  Serial.println();
  Serial.println("=== MotoButtons 2 input diagnostics ===");
  Serial.printf("debounce joystick %u ms, buttons %u ms, %u ADC samples per read\n",
                DEBOUNCE_JOYSTICK_MS, DEBOUNCE_BUTTON_MS, JOYSTICK_ADC_SAMPLES);
  Serial.printf("logical mapping: UP=GPIO%u DOWN=GPIO%u LEFT=GPIO%u RIGHT=GPIO%u CENTER=GPIO%u\n",
                buttons[BUTTON_UP].pin, buttons[BUTTON_DOWN].pin, buttons[BUTTON_LEFT].pin,
                buttons[BUTTON_RIGHT].pin, buttons[BUTTON_CENTER].pin);
  Serial.printf("joystick thresholds: press <= %u mV, release >= %u mV\n",
                JOYSTICK_PRESS_MV, JOYSTICK_RELEASE_MV);
  Serial.println("Joystick: P = pressed, - = released. A/B/C: H = released, L = pressed.");
  Serial.println("Idle sits at the ADC ceiling (~2984 mV); a real press reads a few mV.");
  Serial.println("=======================================");
}

void inputsDiagnosticsTick()
{
  static unsigned long lastSnapshotMs = 0;
  if (millis() - lastSnapshotMs < DEBUG_INPUTS_SNAPSHOT_MS)
    return;

  lastSnapshotMs = millis();
  assertJoystickPullups();
  delayMicroseconds(JOYSTICK_ADC_SETTLE_US);
  logInputSnapshot();
}
