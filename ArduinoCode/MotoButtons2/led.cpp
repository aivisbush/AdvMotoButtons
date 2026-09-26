#include "led.h"

struct Rgb
{
  uint8_t red, green, blue;
};

static const Rgb RGB_OFF = {0, 0, 0};
static const Rgb RGB_BLUE = {0, 0, 255};
static const Rgb RGB_GREEN = {0, 255, 0};
static const Rgb RGB_ORANGE = {255, 60, 0};
static const Rgb RGB_MAGENTA = {245, 0, 245};
static const uint16_t MODE_BLINK_GAP_MS = 150;

static bool lastConnected = false;
static Mode lastMode = DEFAULT_MODE;
static bool enabled = true;
static unsigned long flashStartMs = 0;
static uint8_t flashCount = 0;
static unsigned long modeBlinkStartMs = 0;
static bool modeBlinkActive = false;

/*--------------------------- single LED -----------------------------*/
static void setStatusLed(uint8_t brightness)
{
  analogWrite(PIN_STATUS_LED, STATUS_LED_ACTIVE_LOW ? 255 - brightness : brightness);
}

// Perceived brightness follows roughly the square of the duty cycle.
static uint8_t gammaCorrect(uint8_t linear)
{
  uint16_t squared = (uint16_t)linear * linear;
  return (uint8_t)((squared + 127) / 255);
}

static void statusLedUpdate(bool connected)
{
  static int16_t lastBrightness = -1;

  uint8_t brightness;
  if (connected)
  {
    brightness = STATUS_LED_CONNECTED_BRIGHTNESS;
  }
  else
  {
    uint16_t phase = millis() % STATUS_LED_PULSE_PERIOD_MS;
    uint16_t halfPeriod = STATUS_LED_PULSE_PERIOD_MS / 2;
    uint16_t rise = phase < halfPeriod ? phase : STATUS_LED_PULSE_PERIOD_MS - phase;
    brightness = gammaCorrect((uint8_t)((uint32_t)rise * 255 / halfPeriod));
  }

  if (brightness != lastBrightness)
  {
    setStatusLed(brightness);
    lastBrightness = brightness;
  }
}

/*----------------------------- RGB LED ------------------------------*/
static Rgb modeColor(Mode mode)
{
  switch (mode)
  {
  case Mode::DMD2:
    return RGB_BLUE;
  case Mode::OsmAnd:
    return RGB_GREEN;
  case Mode::Locus:
    return RGB_ORANGE;
  default:
    return RGB_MAGENTA;
  }
}

// Common anode: 255 is off.
static uint8_t anodeLevel(uint8_t channel)
{
  return (uint8_t)(255 - ((uint16_t)channel * RGB_LED_BRIGHTNESS + 127) / 255);
}

static void setRgb(const Rgb &color)
{
  static Rgb shown = {1, 1, 1};
  if (color.red == shown.red && color.green == shown.green && color.blue == shown.blue)
    return;
  shown = color;
  analogWrite(PIN_LED_RED, anodeLevel(color.red));
  analogWrite(PIN_LED_GREEN, anodeLevel(color.green));
  analogWrite(PIN_LED_BLUE, anodeLevel(color.blue));
}

static void rgbUpdate(bool connected, Mode mode)
{
  unsigned long now = millis();

  if (flashCount > 0)
  {
    unsigned long phase = (now - flashStartMs) / (RGB_LED_FLASH_MS / 2);
    if (phase < (unsigned long)flashCount * 2)
    {
      setRgb(phase % 2 == 0 ? RGB_ORANGE : RGB_OFF);
      return;
    }
    flashCount = 0;
  }

  if (modeBlinkActive)
  {
    unsigned long elapsed = now - modeBlinkStartMs;
    if (elapsed < MODE_BLINK_GAP_MS + RGB_LED_MODE_BLINK_MS)
    {
      setRgb(elapsed < MODE_BLINK_GAP_MS ? RGB_OFF : modeColor(mode));
      return;
    }
    modeBlinkActive = false;
  }

  if (!connected)
    setRgb((now / RGB_LED_WAITING_BLINK_MS) % 2 == 0 ? RGB_BLUE : RGB_OFF);
  else
    setRgb(enabled ? modeColor(mode) : RGB_OFF);
}

/*------------------------------ common ------------------------------*/
void ledBegin()
{
  if (BOARD_HAS_RGB_LED)
  {
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    setRgb(RGB_ORANGE); // booting
  }
  else if (PIN_STATUS_LED != PIN_NONE)
  {
    pinMode(PIN_STATUS_LED, OUTPUT);
    setStatusLed(0);
  }
}

void ledUpdate(bool connected, Mode mode)
{
  lastConnected = connected;
  lastMode = mode;
  if (BOARD_HAS_RGB_LED)
    rgbUpdate(connected, mode);
  else if (PIN_STATUS_LED != PIN_NONE)
    statusLedUpdate(connected);
}

void ledShowMode()
{
  modeBlinkStartMs = millis();
  modeBlinkActive = true;
}

void ledFlash(uint8_t count)
{
  flashStartMs = millis();
  flashCount = count;
}

void ledSetEnabled(bool on)
{
  enabled = on;
}

void ledWait(uint16_t ms)
{
  unsigned long startMs = millis();
  while (millis() - startMs < ms)
  {
    ledUpdate(lastConnected, lastMode);
    delay(1);
  }
}
