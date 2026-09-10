#include "keymap.h"
#include "inputs.h"
#include "chords.h"
#include "ble_hid.h"
#include "debug.h"

enum class KeyKind : uint8_t
{
  None,     // input does nothing in this mode
  Held,     // keyboard key down while held, up on release; may repeat
  Tap,      // keyboard key pressed once per press, held for KEY_TAP_MS
  Consumer  // consumer-control pulse once per press; may repeat while held
};

struct KeyBinding
{
  KeyKind kind;
  uint16_t code;      // HID keyboard usage or consumer usage
  uint16_t repeatMs;  // 0 = no firmware repeat
};

struct ModeKeymap
{
  Mode mode;
  const char *name;
  bool consumerPage;              // true: all bindings are consumer usages
  KeyBinding keys[BUTTON_COUNT];  // in ButtonId order: UP DOWN LEFT RIGHT CENTER A B C
};

static const ModeKeymap KEYMAPS[] = {
  // DMD2 owns repeat, repeat speed and long press itself, and needs one
  // clean key down and key up per physical press to do it.
  {Mode::DMD2, "DMD", false, {
    {KeyKind::Held, HID_KEY_ARROW_UP, 0},
    {KeyKind::Held, HID_KEY_ARROW_DOWN, 0},
    {KeyKind::Held, HID_KEY_ARROW_LEFT, 0},
    {KeyKind::Held, HID_KEY_ARROW_RIGHT, 0},
    {KeyKind::Held, HID_KEY_F5, 0},
    {KeyKind::Held, HID_KEY_F6, 0},
    {KeyKind::Held, HID_KEY_F7, 0},
    {KeyKind::Held, HID_KEY_ENTER, 0},
  }},
  // OsmAnd does not repeat keys itself, so the firmware does.
  {Mode::OsmAnd, "OsmAnd", false, {
    {KeyKind::Held, HID_KEY_ARROW_UP, DIRECTION_REPEAT_INTERVAL_MS},
    {KeyKind::Held, HID_KEY_ARROW_DOWN, DIRECTION_REPEAT_INTERVAL_MS},
    {KeyKind::Held, HID_KEY_ARROW_LEFT, DIRECTION_REPEAT_INTERVAL_MS},
    {KeyKind::Held, HID_KEY_ARROW_RIGHT, DIRECTION_REPEAT_INTERVAL_MS},
    {KeyKind::None, HID_KEY_NONE, 0},                         // centre unbound
    {KeyKind::Held, HID_KEY_EQUAL, ABC_REPEAT_INTERVAL_MS},   // zoom in
    {KeyKind::Held, HID_KEY_MINUS, ABC_REPEAT_INTERVAL_MS},   // zoom out
    {KeyKind::Tap, HID_KEY_C, 0},                             // move to my location
  }},
  // Media keys go out on the consumer page, which iOS requires.
  {Mode::Media, "Media", true, {
    {KeyKind::Consumer, HID_USAGE_CONSUMER_VOLUME_INCREMENT, VOLUME_REPEAT_INTERVAL_MS},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_VOLUME_DECREMENT, VOLUME_REPEAT_INTERVAL_MS},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_SCAN_PREVIOUS, 0},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_SCAN_NEXT, 0},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_MUTE, 0},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_PLAY_PAUSE, 0},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT, ABC_REPEAT_INTERVAL_MS},
    {KeyKind::Consumer, HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT, ABC_REPEAT_INTERVAL_MS},
  }},
};
static const uint8_t KEYMAP_COUNT = sizeof(KEYMAPS) / sizeof(KEYMAPS[0]);

// Engine state.
static bool activeNow[BUTTON_COUNT] = {false};
static bool activeBefore[BUTTON_COUNT] = {false};
static uint8_t lastSentReport[KEY_REPORT_SIZE] = {HID_KEY_NONE};
static unsigned long lastRepeatMs = 0;
static bool tapActive = false;
static uint8_t tapCode = HID_KEY_NONE;
static unsigned long tapStartMs = 0;
static unsigned long consumerLastPulseMs[BUTTON_COUNT] = {0};

static const ModeKeymap &keymapFor(Mode mode)
{
  for (uint8_t i = 0; i < KEYMAP_COUNT; i++)
  {
    if (KEYMAPS[i].mode == mode)
      return KEYMAPS[i];
  }
  return KEYMAPS[0];
}

const char *getModeName(Mode mode)
{
  return keymapFor(mode).name;
}

Mode getNextMode(Mode mode)
{
  for (uint8_t i = 0; i < KEYMAP_COUNT; i++)
  {
    if (KEYMAPS[i].mode == mode)
      return KEYMAPS[(i + 1) % KEYMAP_COUNT].mode;
  }
  return KEYMAPS[0].mode;
}

/* Which inputs count as pressed for reporting purposes.
 * Directions: held, unless the opposite direction is held too.
 * Centre: held.
 * A/B/C: held alone for at least the chord grace period, and not while a
 * fired chord is waiting for its buttons to be released. Two of them held
 * together are always a chord in progress and never keys.
 */
static void computeActive()
{
  bool a = buttonHeld(BUTTON_A);
  bool b = buttonHeld(BUTTON_B);
  bool c = buttonHeld(BUTTON_C);
  bool suppress = chordsSuppressButtons();

  for (uint8_t id = BUTTON_UP; id <= BUTTON_RIGHT; id++)
    activeNow[id] = directionActive((ButtonId)id);
  activeNow[BUTTON_CENTER] = buttonHeld(BUTTON_CENTER);
  activeNow[BUTTON_A] = a && !b && !c && !suppress && buttonHeldMs(BUTTON_A) >= CHORD_GRACE_MS;
  activeNow[BUTTON_B] = b && !a && !c && !suppress && buttonHeldMs(BUTTON_B) >= CHORD_GRACE_MS;
  activeNow[BUTTON_C] = c && !a && !b && !suppress && buttonHeldMs(BUTTON_C) >= CHORD_GRACE_MS;
}

static bool risingEdge(uint8_t id)
{
  return activeNow[id] && !activeBefore[id];
}

static void pushKey(uint8_t report[KEY_REPORT_SIZE], uint8_t &count, uint8_t code)
{
  if (count < KEY_REPORT_SIZE && code != HID_KEY_NONE)
    report[count++] = code;
}

static bool reportIsEmpty(const uint8_t report[KEY_REPORT_SIZE])
{
  for (uint8_t i = 0; i < KEY_REPORT_SIZE; i++)
  {
    if (report[i] != HID_KEY_NONE)
      return false;
  }
  return true;
}

static void sendReport(const uint8_t report[KEY_REPORT_SIZE])
{
  bleSendKeyboardReport(report);
  memcpy(lastSentReport, report, KEY_REPORT_SIZE);
  lastRepeatMs = millis();
}

// DMD2 and OsmAnd: the report mirrors the active inputs.
static void updateKeyboardMode(const ModeKeymap &map)
{
  uint8_t report[KEY_REPORT_SIZE] = {HID_KEY_NONE};
  uint8_t count = 0;
  uint16_t repeatMs = 0;
  unsigned long now = millis();

  for (uint8_t id = 0; id < BUTTON_COUNT; id++)
  {
    const KeyBinding &key = map.keys[id];
    if (key.kind == KeyKind::Held && activeNow[id])
    {
      pushKey(report, count, (uint8_t)key.code);
      if (key.repeatMs > 0 && (repeatMs == 0 || key.repeatMs < repeatMs))
        repeatMs = key.repeatMs;
    }
    else if (key.kind == KeyKind::Tap && risingEdge(id))
    {
      tapActive = true;
      tapCode = (uint8_t)key.code;
      tapStartMs = now;
    }
  }

  if (tapActive)
  {
    if (now - tapStartMs < KEY_TAP_MS)
      pushKey(report, count, tapCode);
    else
      tapActive = false;
  }

  if (memcmp(report, lastSentReport, KEY_REPORT_SIZE) != 0)
  {
    sendReport(report);
    debugPrintf("%s key report sent (%u keys).\n", map.name, count);
    return;
  }

  // Firmware repeat: a short release, then the same report again.
  if (!reportIsEmpty(report) && repeatMs > 0 && now - lastRepeatMs >= repeatMs)
  {
    const uint8_t released[KEY_REPORT_SIZE] = {HID_KEY_NONE};
    bleSendKeyboardReport(released);
    delay(REPEAT_RELEASE_GAP_MS);
    sendReport(report);
  }
}

// Media: one consumer pulse per press, repeating for the volume-like keys.
static void updateConsumerMode(const ModeKeymap &map)
{
  unsigned long now = millis();

  for (uint8_t id = 0; id < BUTTON_COUNT; id++)
  {
    const KeyBinding &key = map.keys[id];
    if (key.kind != KeyKind::Consumer || !activeNow[id])
      continue;

    bool firstPulse = risingEdge(id);
    bool repeatDue = key.repeatMs > 0 && now - consumerLastPulseMs[id] >= key.repeatMs;
    if (!firstPulse && !repeatDue)
      continue;

    bleSendConsumerPulse(key.code);
    consumerLastPulseMs[id] = now;
    debugPrintf("%s key %s sent.\n", map.name, buttons[id].name);
  }
}

void keymapUpdate(bool connected, Mode mode)
{
  computeActive();

  if (!connected)
  {
    // The host's idea of the keys is unknown while disconnected: treat
    // everything as released so the real state goes out on reconnect,
    // and swallow edges so nothing fires just because the link came back.
    memset(lastSentReport, HID_KEY_NONE, sizeof(lastSentReport));
    tapActive = false;
    memcpy(activeBefore, activeNow, sizeof(activeBefore));
    return;
  }

  const ModeKeymap &map = keymapFor(mode);
  if (map.consumerPage)
    updateConsumerMode(map);
  else
    updateKeyboardMode(map);

  memcpy(activeBefore, activeNow, sizeof(activeBefore));
}

void keymapReleaseAll()
{
  const uint8_t released[KEY_REPORT_SIZE] = {HID_KEY_NONE};
  if (!reportIsEmpty(lastSentReport))
    sendReport(released);
  tapActive = false;
}
