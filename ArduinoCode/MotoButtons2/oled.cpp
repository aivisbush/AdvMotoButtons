#include "oled.h"
#include "config.h"
#include "inputs.h"
#include "keymap.h"
#include "debug.h"
#include <U8g2lib.h>

// U8g2 starts Wire itself with these pins on the ESP32.
static U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

static const uint8_t OLED_WIDTH = 72;
static const uint8_t OLED_HEIGHT = 40;
static const uint8_t OLED_MAX_LINES = 3;
static const uint8_t OLED_TEXT_BUFFER = 40;

// Button overlay bar across the top, with the message area below it.
static const uint8_t BAR_SLOTS = 8;
static const uint8_t BAR_SLOT_WIDTH = OLED_WIDTH / BAR_SLOTS;
static const uint8_t BAR_CENTER_Y = 4;
static const uint8_t BAR_ARROW_LENGTH = 3;   // tip to centre
static const uint8_t BAR_ARROW_HALF_BASE = 3;
static const uint8_t BAR_ARROW_BASE_OFFSET = 2;
static const uint8_t BAR_DOT_RADIUS = 3;
static const uint8_t BAR_LABEL_BASELINE = BAR_CENTER_Y + 3;
static const uint8_t TEXT_TOP_WITH_BAR = 12;

static const uint8_t SPLASH_LINES = 3;
static const char *const SPLASH_TEXT[SPLASH_LINES] = {"READY", "TO >>", "RACE"};
// The connection screens share one size, so both strings must fit the chosen font.
static const char TEXT_CONNECTING[] = "Connecting";
static const char TEXT_CONNECTED[] = "Connected";

// Largest first: used where a screen must keep one stable font size.
static const uint8_t *const FONT_LADDER[] = {
  u8g2_font_10x20_tr,
  u8g2_font_9x15B_tr,
  u8g2_font_9x15_tr,
  u8g2_font_8x13B_tr,
  u8g2_font_7x14B_tr,
  u8g2_font_7x13B_tr,
  u8g2_font_6x12_tr,
  u8g2_font_5x8_tr
};
static const uint8_t FONT_LADDER_COUNT = sizeof(FONT_LADDER) / sizeof(FONT_LADDER[0]);
static const uint8_t *const FONT_DEFAULT = u8g2_font_7x14B_tr;
static const uint8_t *const FONT_SMALL = u8g2_font_5x8_tr;
static const uint8_t *const FONT_BAR = u8g2_font_5x8_tr;

// Wrap-safe timer: a start time and a duration, never an absolute deadline.
struct Countdown
{
  unsigned long startMs;
  uint32_t durationMs;
  bool active;
};

static void countdownStart(Countdown &timer, uint32_t durationMs)
{
  timer.startMs = millis();
  timer.durationMs = durationMs;
  timer.active = true;
}

static bool countdownRunning(const Countdown &timer)
{
  return timer.active && (millis() - timer.startMs) < timer.durationMs;
}

// Which kind of screen is on the panel, so each render can tell whether
// it still owns the display.
enum class Screen : uint8_t
{
  None,
  Boot,
  Text,
  Splash
};

static bool enabled = true;
static bool awake = false;
static bool dimmed = false;
static uint8_t normalContrast = OLED_DEFAULT_CONTRAST;

// Render cache: text, overlay state and font of what is on the panel.
static Screen screen = Screen::None;
static char lastText[OLED_TEXT_BUFFER] = "";
static uint8_t lastMask = 0;
static const uint8_t *lastFont = nullptr;
static bool lastBar = false;

// Transients are built at runtime, so keep our own copy of the text.
static char transientText[OLED_TEXT_BUFFER] = "";
static uint8_t transientPriority = OLED_PRIORITY_NORMAL;
static const uint8_t *transientFont = nullptr;
static Countdown transientTimer = {0, 0, false};

// The splash runs once per boot, after the "Connected" message.
static bool splashShownThisBoot = false;
static bool splashPending = false;
static Countdown splashTimer = {0, 0, false};

static bool wasConnected = false;
static unsigned long connectionChangedMs = 0;

/*--------------------------- panel control --------------------------*/
static void wakePanel()
{
  if (awake)
    return;
  oled.setPowerSave(0);
  awake = true;
}

static void sleepPanel()
{
  if (awake)
  {
    oled.setPowerSave(1);
    awake = false;
  }
  screen = Screen::None;
}

static void applyDim(bool dim)
{
  if (dim == dimmed)
    return;
  dimmed = dim;
  oled.setContrast(dim ? OLED_DIM_CONTRAST : normalContrast);
}

static void invalidate()
{
  screen = Screen::None;
  lastText[0] = '\0';
}

/*---------------------------- button bar ----------------------------*/
static int16_t barSlotCenterX(uint8_t slot)
{
  return slot * BAR_SLOT_WIDTH + BAR_SLOT_WIDTH / 2;
}

// Each slot keeps its fixed position and stays blank until its button is pressed.
static void drawBarLabel(uint8_t slot, const char *label, bool pressed)
{
  if (!pressed)
    return;

  oled.setFont(FONT_BAR);
  int16_t x = barSlotCenterX(slot) - oled.getStrWidth(label) / 2;
  oled.drawStr(x, BAR_LABEL_BASELINE, label);
}

// Arrows appear only while their direction is pressed.
static void drawBarArrow(uint8_t slot, ButtonId direction, bool pressed)
{
  if (!pressed)
    return;

  int16_t cx = barSlotCenterX(slot);
  int16_t cy = BAR_CENTER_Y;
  int16_t tipX = cx;
  int16_t tipY = cy;
  int16_t baseAX = cx;
  int16_t baseAY = cy;
  int16_t baseBX = cx;
  int16_t baseBY = cy;

  switch (direction)
  {
  case BUTTON_UP:
    tipY = cy - BAR_ARROW_LENGTH;
    baseAX = cx - BAR_ARROW_HALF_BASE;
    baseAY = cy + BAR_ARROW_BASE_OFFSET;
    baseBX = cx + BAR_ARROW_HALF_BASE;
    baseBY = cy + BAR_ARROW_BASE_OFFSET;
    break;
  case BUTTON_DOWN:
    tipY = cy + BAR_ARROW_LENGTH;
    baseAX = cx - BAR_ARROW_HALF_BASE;
    baseAY = cy - BAR_ARROW_BASE_OFFSET;
    baseBX = cx + BAR_ARROW_HALF_BASE;
    baseBY = cy - BAR_ARROW_BASE_OFFSET;
    break;
  case BUTTON_LEFT:
    tipX = cx - BAR_ARROW_LENGTH;
    baseAX = cx + BAR_ARROW_BASE_OFFSET;
    baseAY = cy - BAR_ARROW_HALF_BASE;
    baseBX = cx + BAR_ARROW_BASE_OFFSET;
    baseBY = cy + BAR_ARROW_HALF_BASE;
    break;
  default: // BUTTON_RIGHT
    tipX = cx + BAR_ARROW_LENGTH;
    baseAX = cx - BAR_ARROW_BASE_OFFSET;
    baseAY = cy - BAR_ARROW_HALF_BASE;
    baseBX = cx - BAR_ARROW_BASE_OFFSET;
    baseBY = cy + BAR_ARROW_HALF_BASE;
    break;
  }

  oled.drawTriangle(tipX, tipY, baseAX, baseAY, baseBX, baseBY);
}

/* Debug overlay: A B C, the four directions as arrows, centre as a dot.
 * A slot is drawn only while its button is held, and slots never move, so
 * a combination of presses is read off the fixed positions. States are
 * the logical (orientation-mapped) ones and the raw ones, so a conflicting
 * pair is visible.
 */
static void drawButtonBar()
{
  drawBarLabel(0, "A", buttonHeld(BUTTON_A));
  drawBarLabel(1, "B", buttonHeld(BUTTON_B));
  drawBarLabel(2, "C", buttonHeld(BUTTON_C));
  drawBarArrow(3, BUTTON_UP, buttonHeld(BUTTON_UP));
  drawBarArrow(4, BUTTON_DOWN, buttonHeld(BUTTON_DOWN));
  drawBarArrow(5, BUTTON_LEFT, buttonHeld(BUTTON_LEFT));
  drawBarArrow(6, BUTTON_RIGHT, buttonHeld(BUTTON_RIGHT));

  if (buttonHeld(BUTTON_CENTER))
    oled.drawDisc(barSlotCenterX(7), BAR_CENTER_Y, BAR_DOT_RADIUS);
}

/*------------------------------ fonts -------------------------------*/
/* Selects and applies the largest ladder font in which every one of texts
 * fits maxWidth and lineCount stacked lines fit maxHeight. Screens that
 * must not change size between messages measure all their strings at once.
 */
static const uint8_t *pickFittingFont(const char *const *texts, uint8_t textCount, uint8_t lineCount,
                                      int16_t maxWidth, int16_t maxHeight, int16_t *ascentOut, int16_t *pitchOut)
{
  const uint8_t *chosenFont = FONT_LADDER[FONT_LADDER_COUNT - 1];

  for (uint8_t f = 0; f < FONT_LADDER_COUNT; f++)
  {
    oled.setFont(FONT_LADDER[f]);
    int16_t pitch = oled.getAscent() + 1;
    bool fits = pitch * lineCount <= maxHeight;
    for (uint8_t i = 0; i < textCount && fits; i++)
    {
      if (oled.getStrWidth(texts[i]) > maxWidth)
        fits = false;
    }

    if (fits)
    {
      chosenFont = FONT_LADDER[f];
      break;
    }
  }

  oled.setFont(chosenFont);
  if (ascentOut != nullptr)
    *ascentOut = oled.getAscent();
  if (pitchOut != nullptr)
    *pitchOut = oled.getAscent() + 1;
  return chosenFont;
}

// All mode names share one size, so the longest one decides it.
static const uint8_t *modeScreenFont()
{
  static const uint8_t *font = nullptr;
  if (font == nullptr)
  {
    const char *const modeNames[] = {getModeName(Mode::DMD2), getModeName(Mode::OsmAnd), getModeName(Mode::Media)};
    // The mode screen carries the button bar, so the text area is shorter.
    font = pickFittingFont(modeNames, 3, 1, OLED_WIDTH, OLED_HEIGHT - TEXT_TOP_WITH_BAR, nullptr, nullptr);
  }
  return font;
}

// "Connecting" and "Connected" share one size for the same reason.
static const uint8_t *connectionFont()
{
  static const uint8_t *font = nullptr;
  if (font == nullptr)
  {
    const char *const texts[] = {TEXT_CONNECTING, TEXT_CONNECTED};
    font = pickFittingFont(texts, 2, 1, OLED_WIDTH, OLED_HEIGHT, nullptr, nullptr);
  }
  return font;
}

/*---------------------------- rendering -----------------------------*/
// A stack of fixed lines in the largest font that fits, no button bar.
static void renderStack(const char *const *lines, uint8_t lineCount, bool leftAlign, Screen kind)
{
  int16_t ascent = 0;
  int16_t pitch = 0;
  pickFittingFont(lines, lineCount, lineCount, OLED_WIDTH, OLED_HEIGHT, &ascent, &pitch);

  int16_t totalHeight = pitch * (lineCount - 1) + ascent;
  int16_t top = (OLED_HEIGHT - totalHeight) / 2;
  if (top < 0)
    top = 0;

  oled.clearBuffer();
  oled.setFontPosTop();
  for (uint8_t i = 0; i < lineCount; i++)
  {
    int16_t x = 0;
    if (!leftAlign)
    {
      uint16_t width = oled.getStrWidth(lines[i]);
      x = width < OLED_WIDTH ? (OLED_WIDTH - width) / 2 : 0;
    }
    oled.drawStr(x, top + i * pitch, lines[i]);
  }
  oled.setFontPosBaseline();
  oled.sendBuffer();

  screen = kind;
}

static void renderSplash()
{
  if (screen == Screen::Splash)
    return;
  renderStack(SPLASH_TEXT, SPLASH_LINES, true, Screen::Splash);
}

// Breaks on '\n', then greedily wraps each part on spaces to fit the panel.
static uint8_t wrapText(const char *text, char lines[OLED_MAX_LINES][OLED_TEXT_BUFFER])
{
  char candidate[OLED_TEXT_BUFFER * 2];
  char word[OLED_TEXT_BUFFER];
  memset(lines, 0, OLED_MAX_LINES * OLED_TEXT_BUFFER);

  uint8_t lineIndex = 0;
  const char *cursor = text;
  while (*cursor != '\0' && lineIndex < OLED_MAX_LINES)
  {
    if (*cursor == '\n')
    {
      // Only advance for a hard break once the current line has content.
      if (lines[lineIndex][0] != '\0')
        lineIndex++;
      cursor++;
      continue;
    }

    if (*cursor == ' ')
    {
      cursor++;
      continue;
    }

    const char *wordEnd = cursor;
    while (*wordEnd != '\0' && *wordEnd != ' ' && *wordEnd != '\n')
      wordEnd++;

    size_t wordLength = (size_t)(wordEnd - cursor);
    if (wordLength > sizeof(word) - 1)
      wordLength = sizeof(word) - 1;
    memcpy(word, cursor, wordLength);
    word[wordLength] = '\0';
    cursor = wordEnd;

    if (lines[lineIndex][0] == '\0')
    {
      strncpy(lines[lineIndex], word, OLED_TEXT_BUFFER - 1);
      continue;
    }

    snprintf(candidate, sizeof(candidate), "%s %s", lines[lineIndex], word);
    if (oled.getStrWidth(candidate) <= OLED_WIDTH)
    {
      strncpy(lines[lineIndex], candidate, OLED_TEXT_BUFFER - 1);
      lines[lineIndex][OLED_TEXT_BUFFER - 1] = '\0';
      continue;
    }

    if (lineIndex + 1 >= OLED_MAX_LINES)
      break;
    lineIndex++;
    strncpy(lines[lineIndex], word, OLED_TEXT_BUFFER - 1);
  }

  if (lineIndex >= OLED_MAX_LINES)
    return OLED_MAX_LINES;
  return lines[lineIndex][0] == '\0' ? lineIndex : lineIndex + 1;
}

/* Centred, wrapped text, optionally under the button bar. Redraws only
 * when the text, the overlay state or the font changed.
 */
static void renderText(const char *text, bool showBar, const uint8_t *font)
{
  uint8_t mask = showBar ? inputsButtonMask() : 0;
  if (screen == Screen::Text && showBar == lastBar && mask == lastMask && font == lastFont &&
      strncmp(lastText, text, sizeof(lastText)) == 0)
    return;

  screen = Screen::Text;
  lastBar = showBar;
  lastMask = mask;
  lastFont = font;
  strncpy(lastText, text, sizeof(lastText) - 1);
  lastText[sizeof(lastText) - 1] = '\0';

  oled.clearBuffer();
  oled.setFontPosBaseline();
  if (showBar)
    drawButtonBar();

  if (font != nullptr)
  {
    // Screens that must not change size between messages pass their own font.
    oled.setFont(font);
  }
  else
  {
    oled.setFont(FONT_DEFAULT);
    // Fall back to a smaller font so longer messages can be wrapped instead of clipped.
    if (strchr(text, '\n') != nullptr || oled.getStrWidth(text) > OLED_WIDTH)
      oled.setFont(FONT_SMALL);
  }

  char lines[OLED_MAX_LINES][OLED_TEXT_BUFFER];
  uint8_t lineCount = wrapText(text, lines);

  int16_t lineHeight = oled.getMaxCharHeight();
  int16_t textTop = showBar ? TEXT_TOP_WITH_BAR : 0;
  int16_t textAreaHeight = OLED_HEIGHT - textTop;
  int16_t top = textTop + (textAreaHeight - lineCount * lineHeight) / 2;
  if (top < textTop)
    top = textTop;

  oled.setFontPosTop();
  for (uint8_t i = 0; i < lineCount; i++)
  {
    uint16_t lineWidth = oled.getStrWidth(lines[i]);
    int16_t x = lineWidth < OLED_WIDTH ? (OLED_WIDTH - lineWidth) / 2 : 0;
    oled.drawStr(x, top + lineHeight * i, lines[i]);
  }
  oled.setFontPosBaseline();
  oled.sendBuffer();
}

/*------------------------------- API --------------------------------*/
void oledBegin(bool enabledSetting, uint8_t contrast)
{
  oled.begin();
  normalContrast = contrast;
  oled.setContrast(contrast);
  awake = true;
  dimmed = false;
  enabled = true;
  if (!enabledSetting)
    oledSetEnabled(false);
}

void oledSetEnabled(bool on)
{
  enabled = on;
  invalidate();

  if (enabled)
  {
    wakePanel();
    dimmed = true; // force the contrast write
    applyDim(false);
    return;
  }

  oled.clearBuffer();
  oled.sendBuffer();
  sleepPanel();
}

void oledShowBootScreen()
{
  if (!enabled)
    return;

  wakePanel();
  char version[16];
  snprintf(version, sizeof(version), "v%s", FIRMWARE_VERSION);
  const char *const lines[] = {"Booting", version};
  renderStack(lines, 2, false, Screen::Boot);
}

void oledShowTransient(const char *text, uint16_t durationMs, uint8_t priority, const uint8_t *font)
{
  // Keep the message already on screen if it outranks this one.
  if (countdownRunning(transientTimer) && priority < transientPriority)
    return;

  transientPriority = priority;
  transientFont = font;
  strncpy(transientText, text, sizeof(transientText) - 1);
  transientText[sizeof(transientText) - 1] = '\0';
  countdownStart(transientTimer, durationMs);

  if (!enabled)
    return;
  wakePanel();
  renderText(transientText, false, transientFont);
}

void oledClearTransient()
{
  transientTimer.active = false;
}

void oledOnConnected()
{
  oledShowTransient(TEXT_CONNECTED, OLED_TRANSIENT_MS, OLED_PRIORITY_NORMAL, connectionFont());
  // The splash follows the "Connected" message, the first time only.
  splashPending = !splashShownThisBoot;
  splashTimer.active = false;
}

void oledUpdate(bool connected, const char *modeName, bool force)
{
  unsigned long now = millis();
  if (connected != wasConnected)
  {
    wasConnected = connected;
    connectionChangedMs = now;
  }

  if (!enabled)
    return;
  if (force)
    invalidate();

  // Quiet time since the last press or connection change.
  unsigned long sinceInput = now - inputsLastActivityMs();
  unsigned long sinceConnection = now - connectionChangedMs;
  unsigned long idleMs = sinceInput < sinceConnection ? sinceInput : sinceConnection;
  bool transientActive = countdownRunning(transientTimer);

  // Nobody is looking and nothing is happening: a dark panel cannot burn in.
  if (!connected && !transientActive && idleMs >= OLED_BLANK_DISCONNECTED_MS)
  {
    sleepPanel();
    return;
  }

  wakePanel();
  applyDim(idleMs >= OLED_DIM_AFTER_MS);

  // A transient message wins over the idle status, connected or not.
  if (transientActive)
  {
    renderText(transientText, false, transientFont);
    return;
  }

  // Then the splash, which sits between "Connected" and the mode screen
  // and is abandoned if the link drops.
  if (splashPending)
  {
    if (!connected)
    {
      splashPending = false;
      splashTimer.active = false;
    }
    else
    {
      if (!splashTimer.active)
      {
        countdownStart(splashTimer, OLED_SPLASH_MESSAGE_MS);
        splashShownThisBoot = true;
      }
      if (countdownRunning(splashTimer))
      {
        renderSplash();
        return;
      }
      splashPending = false;
      splashTimer.active = false;
    }
  }

  // The button bar is part of the mode screen only.
  if (connected)
    renderText(modeName, true, modeScreenFont());
  else
    renderText(TEXT_CONNECTING, false, connectionFont());
}
