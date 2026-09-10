#include "ble_hid.h"
#include "debug.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

static const uint8_t KEYBOARD_REPORT_ID = 1;
static const uint8_t CONSUMER_REPORT_ID = 2;

// Separate keyboard and consumer-control input reports.
static const uint8_t HID_REPORT_DESCRIPTOR[] = {
  0x05, 0x01,       // Usage Page (Generic Desktop)
  0x09, 0x06,       // Usage (Keyboard)
  0xA1, 0x01,       // Collection (Application)
  0x85, 0x01,       //   Report ID (1)
  0x05, 0x07,       //   Usage Page (Keyboard)
  0x19, 0xE0,       //   Usage Minimum (Left Control)
  0x29, 0xE7,       //   Usage Maximum (Right GUI)
  0x15, 0x00,       //   Logical Minimum (0)
  0x25, 0x01,       //   Logical Maximum (1)
  0x75, 0x01,       //   Report Size (1)
  0x95, 0x08,       //   Report Count (8)
  0x81, 0x02,       //   Input (Data, Variable, Absolute)
  0x95, 0x01,       //   Report Count (1)
  0x75, 0x08,       //   Report Size (8)
  0x81, 0x01,       //   Input (Constant)
  0x95, 0x06,       //   Report Count (6)
  0x75, 0x08,       //   Report Size (8)
  0x15, 0x00,       //   Logical Minimum (0)
  0x25, 0x65,       //   Logical Maximum (101)
  0x05, 0x07,       //   Usage Page (Keyboard)
  0x19, 0x00,       //   Usage Minimum (Reserved)
  0x29, 0x65,       //   Usage Maximum (Keyboard Application)
  0x81, 0x00,       //   Input (Data, Array, Absolute)
  0xC0,             // End Collection

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
  NimBLEDevice::setPower(BLE_TX_POWER_DBM);
  NimBLEDevice::setSecurityAuth(true, false, true); // bonding, no MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new MotoButtonsServerCallbacks());

  hidDevice = new NimBLEHIDDevice(bleServer);
  hidDevice->setManufacturer(BLE_MANUFACTURER);
  hidDevice->setPnp(0x02, 0x303A, 0x4001, 0x0200); // USB-IF source, Espressif VID
  hidDevice->setHidInfo(0x00, HID_INFO_REMOTE_WAKE | HID_INFO_NORMALLY_CONNECTABLE);
  hidDevice->setReportMap((uint8_t *)HID_REPORT_DESCRIPTOR, sizeof(HID_REPORT_DESCRIPTOR));
  keyboardInput = hidDevice->getInputReport(KEYBOARD_REPORT_ID);
  consumerInput = hidDevice->getInputReport(CONSUMER_REPORT_ID);
  hidDevice->setBatteryLevel(100);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
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

  // Modifiers, reserved byte, then the six key slots.
  uint8_t report[2 + KEY_REPORT_SIZE] = {0};
  memcpy(&report[2], keys, KEY_REPORT_SIZE);
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
