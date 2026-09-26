package com.bush.motobuttons;

import java.util.UUID;

/** Firmware update protocol, mirrors ArduinoCode/MotoButtons2/ota.cpp. */
final class Protocol {
    static final UUID OTA_SERVICE = UUID.fromString("6a2a0000-7a4e-4b5c-9d3f-2f6d6f746f62");
    static final UUID OTA_CONTROL = UUID.fromString("6a2a0001-7a4e-4b5c-9d3f-2f6d6f746f62");
    static final UUID OTA_DATA = UUID.fromString("6a2a0002-7a4e-4b5c-9d3f-2f6d6f746f62");

    static final UUID DEVICE_INFO_SERVICE = UUID.fromString("0000180a-0000-1000-8000-00805f9b34fb");
    static final UUID FIRMWARE_REVISION = UUID.fromString("00002a26-0000-1000-8000-00805f9b34fb");
    static final UUID PNP_ID = UUID.fromString("00002a50-0000-1000-8000-00805f9b34fb");
    static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    // Nordic legacy DFU service of the nRF52840 firmware (Bluefruit BLEDfu).
    static final UUID NORDIC_DFU_SERVICE = UUID.fromString("00001530-1212-efde-1523-785feabcd123");

    static final byte CMD_START = 0x01;
    static final byte CMD_FINISH = 0x02;
    static final byte CMD_ABORT = 0x03;

    static final int EVT_STARTED = 0x01;
    static final int EVT_FINISHED = 0x02;
    static final int EVT_ABORTED = 0x03;
    static final int EVT_DATA_ERROR = 0x04;

    static final int MTU = 247;
    // ESP32 app image: first byte 0xE9, must fit one 1.25 MB OTA slot.
    static final int IMAGE_MAGIC = 0xE9;
    static final int MAX_IMAGE_SIZE = 0x140000;

    static String statusText(int status) {
        switch (status) {
            case 0: return "OK";
            case 1: return "update not started";
            case 2: return "image too big or no free partition";
            case 3: return "flash write failed";
            case 4: return "size mismatch";
            case 5: return "image check failed (MD5 or not a valid firmware)";
            case 6: return "bad command";
            default: return "error " + status;
        }
    }

    private Protocol() {
    }
}
