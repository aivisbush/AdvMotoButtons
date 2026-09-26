#include "ble_hid.h"
#include "keymap.h"
#include "ota.h"
#include "debug.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

extern "C" int ble_svc_gap_device_appearance_set(uint16_t appearance);

static const uint8_t KEYBOARD_REPORT_ID = 1;
static const uint8_t CONSUMER_REPORT_ID = 2;
static const uint8_t MAX_KEYBOARD_KEYS = 16;
static_assert(KEY_REPORT_SIZE == 6, "Report Count in KEYBOARD_DESCRIPTOR_HEAD");

// Keyboard: an array over the listed usages, each report value is the
// usage's index + 1 (0 = no key). The usage list is filled in at start-up.
static const uint8_t KEYBOARD_DESCRIPTOR_HEAD[] = {
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x06,       // Usage (Keyboard)
  0xA1, 0x01,       // Collection (Application)
  0x85, 0x01,       //   Report ID (1)
  0x05, 0x07,       //   Usage Page (Keyboard)
  0x75, 0x08,       //   Report Size (8)
  0x95, 0x06,       //   Report Count (6)
  0x15, 0x01,       //   Logical Minimum (1)
  0x25              //   Logical Maximum (key count follows)
};
static const uint8_t KEYBOARD_DESCRIPTOR_TAIL[] = {
  0x81, 0x00,       //   Input (Data, Array, Absolute)
  0xC0              // End Collection
};

static const uint8_t CONSUMER_DESCRIPTOR[] = {
  0x05, 0x0C,       // Usage Page (Consumer)
  0x09, 0x01,       // Usage (Consumer Control)
  0xA1, 0x01,       // Collection (Application)
  0x85, 0x02,       //   Report ID (2)
  0x15, 0x00,       //   Logical Minimum (0)
  0x26, 0xFF, 0x03, //   Logical Maximum (1023)
  0x19, 0x00,       //   Usage Minimum (Unassigned)
  0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
  0x75, 0x10,       //   Report Size (16)
  0x95, 0x01,       //   Report Count (1)
  0x81, 0x00,       //   Input (Data, Array, Absolute)
  0xC0              // End Collection
};

static uint8_t keyboardKeys[MAX_KEYBOARD_KEYS];
static uint8_t keyboardKeyCount = 0;
static uint8_t reportDescriptor[sizeof(KEYBOARD_DESCRIPTOR_HEAD) + 1 + 2 * MAX_KEYBOARD_KEYS +
                                sizeof(KEYBOARD_DESCRIPTOR_TAIL) + sizeof(CONSUMER_DESCRIPTOR)];

static uint16_t buildReportDescriptor()
{
  keyboardKeyCount = keymapKeyboardKeys(keyboardKeys, MAX_KEYBOARD_KEYS);
  uint16_t length = 0;
  memcpy(reportDescriptor, KEYBOARD_DESCRIPTOR_HEAD, sizeof(KEYBOARD_DESCRIPTOR_HEAD));
  length += sizeof(KEYBOARD_DESCRIPTOR_HEAD);
  reportDescriptor[length++] = keyboardKeyCount;
  for (uint8_t i = 0; i < keyboardKeyCount; i++)
  {
    reportDescriptor[length++] = 0x09; // Usage
    reportDescriptor[length++] = keyboardKeys[i];
  }
  memcpy(reportDescriptor + length, KEYBOARD_DESCRIPTOR_TAIL, sizeof(KEYBOARD_DESCRIPTOR_TAIL));
  length += sizeof(KEYBOARD_DESCRIPTOR_TAIL);
  memcpy(reportDescriptor + length, CONSUMER_DESCRIPTOR, sizeof(CONSUMER_DESCRIPTOR));
  length += sizeof(CONSUMER_DESCRIPTOR);
  debugPrintf("HID keyboard declares %u keys.\n", keyboardKeyCount);
  return length;
}

static uint8_t keyboardIndex(uint8_t key)
{
  for (uint8_t i = 0; i < keyboardKeyCount; i++)
  {
    if (keyboardKeys[i] == key)
      return i + 1;
  }
  return 0;
}

// HID information flags: the controller wakes the host and is normally
// connectable, i.e. advertising whenever it is not connected.
static const uint8_t HID_INFO_REMOTE_WAKE = 0x01;
static const uint8_t HID_INFO_NORMALLY_CONNECTABLE = 0x02;

static NimBLEHIDDevice *hidDevice = nullptr;
static NimBLECharacteristic *keyboardInput = nullptr;
static NimBLECharacteristic *consumerInput = nullptr;
static NimBLEServer *bleServer = nullptr;

static volatile bool connected = false;
static bool whitelistActive = false;
static unsigned long advertisingStartedMs = 0;

static bool whitelistWanted()
{
  return BLE_WHITELIST_BONDED && NimBLEDevice::getWhiteListCount() > 0;
}

static void startAdvertising(bool bondedOnly)
{
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setScanFilter(bondedOnly, bondedOnly);
  whitelistActive = bondedOnly;
  advertisingStartedMs = millis();

  bool started = NimBLEDevice::startAdvertising();
  debugPrintf("BLE advertising %s%s.\n", bondedOnly ? "to bonded phones only" : "openly",
              started ? "" : " - FAILED to start");
}

// Every phone bonded so far is allowed to connect.
static void loadBondedPhonesIntoWhitelist()
{
  int bondCount = NimBLEDevice::getNumBonds();
  for (int i = 0; i < bondCount; i++)
    NimBLEDevice::whiteListAdd(NimBLEDevice::getBondedAddress(i));
  debugPrintf("BLE bonds: %d\n", bondCount);
}

// The connection interval decides how often a report can leave. The OsmAnd
// tap timings in config.h should be whole multiples of it.
static void logConnectionParams(const NimBLEConnInfo &connInfo)
{
  unsigned long intervalHundredths = (unsigned long)connInfo.getConnInterval() * 125; // 1.25 ms units
  debugPrintf("BLE connection interval %lu.%02lu ms, slave latency %u, supervision timeout %u ms\n",
              intervalHundredths / 100, intervalHundredths % 100, connInfo.getConnLatency(),
              connInfo.getConnTimeout() * 10);
}

class MotoButtonsServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
  {
    connected = true;
    logConnectionParams(connInfo);
  }

  void onConnParamsUpdate(NimBLEConnInfo &connInfo) override
  {
    logConnectionParams(connInfo);
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
  {
    connected = false;
    otaOnDisconnect();
    startAdvertising(whitelistWanted());
  }

  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
  {
    if (!connInfo.isEncrypted())
    {
      // A phone that failed to pair would otherwise sit on the single
      // connection slot and block the real one.
      debugPrintln("BLE pairing failed; disconnecting.");
      NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
      return;
    }

    if (BLE_WHITELIST_BONDED && connInfo.isBonded())
    {
      NimBLEAddress identity = connInfo.getIdAddress();
      if (!NimBLEDevice::onWhiteList(identity))
        NimBLEDevice::whiteListAdd(identity);
    }
  }
};

void bleBegin()
{
  NimBLEDevice::init(BLE_DEVICE_NAME);
  ble_svc_gap_device_appearance_set(BLE_APPEARANCE);
  NimBLEDevice::setPower(BLE_TX_POWER_DBM);
  NimBLEDevice::setMTU(BLE_MTU);
  NimBLEDevice::setSecurityAuth(true, false, true); // bonding, no MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new MotoButtonsServerCallbacks());

  hidDevice = new NimBLEHIDDevice(bleServer);
  hidDevice->setManufacturer(BLE_MANUFACTURER);
  hidDevice->setPnp(0x02, 0x303A, BOARD_PRODUCT_ID, 0x0200); // USB-IF source, Espressif VID; product = board
  debugPrintf("Board %s\n", BOARD_ID_TAG + 8);
  hidDevice->setHidInfo(0x00, HID_INFO_REMOTE_WAKE | HID_INFO_NORMALLY_CONNECTABLE);
  hidDevice->setReportMap(reportDescriptor, buildReportDescriptor());
  keyboardInput = hidDevice->getInputReport(KEYBOARD_REPORT_ID);
  consumerInput = hidDevice->getInputReport(CONSUMER_REPORT_ID);
  hidDevice->setBatteryLevel(100);
  // Firmware Revision String, read by the update app.
  hidDevice->getDeviceInfoService()
    ->createCharacteristic((uint16_t)0x2A26, NIMBLE_PROPERTY::READ)
    ->setValue(std::string(FIRMWARE_VERSION));
  // Model Number String, shown by the update app.
  hidDevice->getDeviceInfoService()
    ->createCharacteristic((uint16_t)0x2A24, NIMBLE_PROPERTY::READ)
    ->setValue(std::string(BLE_DEVICE_MODEL));
  otaCreateService(bleServer);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(BLE_APPEARANCE);
  advertising->addServiceUUID(hidDevice->getHidService()->getUUID());
  advertising->enableScanResponse(true);
  advertising->setPreferredParams(0x06, 0x12);

  NimBLEAdvertisementData scanResponse;
  scanResponse.setName(BLE_DEVICE_NAME);
  advertising->setScanResponseData(scanResponse);

  if (BLE_WHITELIST_BONDED)
    loadBondedPhonesIntoWhitelist();
  startAdvertising(whitelistWanted());
}

void bleUpdate()
{
  if (connected || !whitelistActive)
    return;

  // No bonded phone turned up: open advertising so a phone that lost its
  // bond, or a new one, can still pair. The next disconnect re-arms it.
  if (millis() - advertisingStartedMs >= BLE_WHITELIST_OPEN_AFTER_MS)
  {
    debugPrintln("No bonded phone connected in time; opening pairing.");
    NimBLEDevice::stopAdvertising();
    startAdvertising(false);
  }
}

bool bleConnected()
{
  return connected;
}

void bleSendKeyboardReport(const uint8_t keys[KEY_REPORT_SIZE])
{
  if (!connected || keyboardInput == nullptr)
    return;

  uint8_t report[KEY_REPORT_SIZE];
  for (uint8_t i = 0; i < KEY_REPORT_SIZE; i++)
    report[i] = keyboardIndex(keys[i]);
  keyboardInput->setValue(report, sizeof(report));
  keyboardInput->notify();
}

void bleSendConsumerPulse(uint16_t usage)
{
  if (!connected || consumerInput == nullptr)
    return;

  uint8_t report[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  consumerInput->setValue(report, sizeof(report));
  consumerInput->notify();
  delay(REPEAT_RELEASE_GAP_MS);

  const uint8_t releaseReport[2] = {0, 0};
  consumerInput->setValue(releaseReport, sizeof(releaseReport));
  consumerInput->notify();
}

bool bleClearBonds()
{
  return NimBLEDevice::deleteAllBonds();
}
