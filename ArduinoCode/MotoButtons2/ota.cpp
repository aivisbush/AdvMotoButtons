#include "ota.h"
#include "config.h"
#include "debug.h"
#include "oled.h"
#include <NimBLEDevice.h>
#include <Update.h>

// The Android app uses the same UUIDs.
static const char OTA_SERVICE_UUID[] = "6a2a0000-7a4e-4b5c-9d3f-2f6d6f746f62";
static const char OTA_CONTROL_UUID[] = "6a2a0001-7a4e-4b5c-9d3f-2f6d6f746f62";
static const char OTA_DATA_UUID[] = "6a2a0002-7a4e-4b5c-9d3f-2f6d6f746f62";

enum OtaCommand : uint8_t
{
  OTA_CMD_START = 0x01,
  OTA_CMD_FINISH = 0x02,
  OTA_CMD_ABORT = 0x03
};

enum OtaEvent : uint8_t
{
  OTA_EVT_STARTED = 0x01,
  OTA_EVT_FINISHED = 0x02,
  OTA_EVT_ABORTED = 0x03,
  OTA_EVT_DATA_ERROR = 0x04
};

enum OtaStatus : uint8_t
{
  OTA_OK = 0,
  OTA_ERR_NOT_STARTED = 1,
  OTA_ERR_BEGIN = 2,       // image too big or partition unavailable
  OTA_ERR_WRITE = 3,
  OTA_ERR_SIZE = 4,        // more or fewer bytes than announced
  OTA_ERR_VERIFY = 5,      // MD5 or image check failed
  OTA_ERR_BAD_COMMAND = 6
};

enum class OtaState : uint8_t
{
  Idle,
  Receiving,
  Done,
  Failed
};

static NimBLECharacteristic *controlChar = nullptr;

// Written from the BLE task, read by the loop.
static volatile OtaState state = OtaState::Idle;
static volatile uint32_t imageSize = 0;
static volatile uint32_t bytesWritten = 0;
static volatile unsigned long stateChangedMs = 0;

static void setState(OtaState newState)
{
  state = newState;
  stateChangedMs = millis();
}

static void notifyStatus(OtaEvent event, OtaStatus status, uint32_t value)
{
  if (controlChar == nullptr)
    return;
  uint8_t message[6] = {event, status, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
  controlChar->setValue(message, sizeof(message));
  controlChar->notify();
}

static uint32_t readU32(const uint8_t *data)
{
  return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void abortUpdate()
{
  if (Update.isRunning())
    Update.abort();
}

static void startUpdate(const uint8_t *data, size_t length)
{
  abortUpdate();
  if (length != 1 + 4 + 16)
  {
    notifyStatus(OTA_EVT_STARTED, OTA_ERR_BAD_COMMAND, 0);
    return;
  }

  uint32_t size = readU32(data + 1);
  char md5Hex[33];
  for (uint8_t i = 0; i < 16; i++)
    snprintf(&md5Hex[i * 2], 3, "%02x", data[5 + i]);

  if (size == 0 || !Update.begin(size, U_FLASH) || !Update.setMD5(md5Hex))
  {
    debugPrintf("OTA: begin failed for %lu bytes: %s\n", (unsigned long)size, Update.errorString());
    abortUpdate();
    setState(OtaState::Failed);
    notifyStatus(OTA_EVT_STARTED, OTA_ERR_BEGIN, size);
    return;
  }

  imageSize = size;
  bytesWritten = 0;
  setState(OtaState::Receiving);
  debugPrintf("OTA: receiving %lu bytes.\n", (unsigned long)size);
  notifyStatus(OTA_EVT_STARTED, OTA_OK, size);
}

static void finishUpdate()
{
  if (state != OtaState::Receiving)
  {
    notifyStatus(OTA_EVT_FINISHED, OTA_ERR_NOT_STARTED, 0);
    return;
  }
  if (bytesWritten != imageSize)
  {
    abortUpdate();
    setState(OtaState::Failed);
    notifyStatus(OTA_EVT_FINISHED, OTA_ERR_SIZE, bytesWritten);
    return;
  }
  if (!Update.end())
  {
    debugPrintf("OTA: verify failed: %s\n", Update.errorString());
    setState(OtaState::Failed);
    notifyStatus(OTA_EVT_FINISHED, OTA_ERR_VERIFY, bytesWritten);
    return;
  }

  debugPrintln("OTA: image verified, restarting.");
  setState(OtaState::Done);
  notifyStatus(OTA_EVT_FINISHED, OTA_OK, bytesWritten);
}

class ControlCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
  {
    NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0)
      return;

    switch (value.data()[0])
    {
    case OTA_CMD_START:
      startUpdate(value.data(), value.length());
      break;
    case OTA_CMD_FINISH:
      finishUpdate();
      break;
    case OTA_CMD_ABORT:
      abortUpdate();
      setState(OtaState::Idle);
      notifyStatus(OTA_EVT_ABORTED, OTA_OK, bytesWritten);
      break;
    default:
      notifyStatus(OTA_EVT_DATA_ERROR, OTA_ERR_BAD_COMMAND, 0);
      break;
    }
  }
};

class DataCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
  {
    if (state != OtaState::Receiving)
    {
      notifyStatus(OTA_EVT_DATA_ERROR, OTA_ERR_NOT_STARTED, 0);
      return;
    }

    NimBLEAttValue value = characteristic->getValue();
    size_t length = value.length();
    if (bytesWritten + length > imageSize)
    {
      abortUpdate();
      setState(OtaState::Failed);
      notifyStatus(OTA_EVT_DATA_ERROR, OTA_ERR_SIZE, bytesWritten);
      return;
    }
    if (Update.write((uint8_t *)value.data(), length) != length)
    {
      debugPrintf("OTA: write failed at %lu: %s\n", (unsigned long)bytesWritten, Update.errorString());
      abortUpdate();
      setState(OtaState::Failed);
      notifyStatus(OTA_EVT_DATA_ERROR, OTA_ERR_WRITE, bytesWritten);
      return;
    }
    bytesWritten += length;
  }
};

void otaCreateService(NimBLEServer *server)
{
  // A run-time read keeps the board tag in the .bin (the update app checks it);
  // an unreferenced constant would be dropped by the linker.
  const char *volatile boardTagProbe = BOARD_ID_TAG;
  (void)boardTagProbe;

  NimBLEService *service = server->createService(OTA_SERVICE_UUID);
  controlChar = service->createCharacteristic(
    OTA_CONTROL_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::NOTIFY);
  controlChar->setCallbacks(new ControlCallbacks());
  NimBLECharacteristic *dataChar =
    service->createCharacteristic(OTA_DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
  dataChar->setCallbacks(new DataCallbacks());
}

void otaOnDisconnect()
{
  if (state != OtaState::Receiving)
    return;
  abortUpdate();
  setState(OtaState::Idle);
  debugPrintln("OTA: phone disconnected, update abandoned.");
}

bool otaActive()
{
  return state == OtaState::Receiving;
}

void otaUpdate()
{
  static uint8_t shownPercent = 255;
  static OtaState shownState = OtaState::Idle;
  OtaState now = state;

  if (now == OtaState::Receiving && imageSize > 0)
  {
    uint8_t percent = (uint8_t)((uint64_t)bytesWritten * 100 / imageSize);
    if (percent != shownPercent || shownState != now)
    {
      char text[12];
      snprintf(text, sizeof(text), "Update\n%u%%", percent);
      oledShowTransient(text, OTA_SCREEN_MS, OLED_PRIORITY_HIGH);
      shownPercent = percent;
    }
  }
  else if (now != shownState)
  {
    if (now == OtaState::Done)
      oledShowTransient("Update\nOK", OTA_SCREEN_MS, OLED_PRIORITY_HIGH);
    else if (now == OtaState::Failed)
      oledShowTransient("Update\nfailed", OTA_SCREEN_MS, OLED_PRIORITY_HIGH);
    shownPercent = 255;
  }
  shownState = now;

  // Restart only after the phone has had time to read the result.
  if (now == OtaState::Done && millis() - stateChangedMs >= OTA_RESTART_DELAY_MS)
    ESP.restart();
}
