package com.bush.motobuttons;

/** One running firmware update: OtaClient (ESP32) or NrfDfuClient (nRF52840). */
interface FirmwareUpdate {
    interface Listener {
        void onProgress(int sent, int total);

        void onStatus(String text);

        void onFinished(boolean success, String message);
    }

    void start();

    void cancel();
}
