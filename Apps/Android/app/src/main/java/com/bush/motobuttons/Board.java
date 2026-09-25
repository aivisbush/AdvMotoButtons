package com.bush.motobuttons;

/**
 * Controller boards. The firmware reports its board as the product ID of
 * the Bluetooth PnP ID (config.h BOARD_PRODUCT_ID) and tags its .bin with
 * MBBOARD=<id>. Firmware before 2.4.0 did not say; it only ran on the
 * ESP32-C3 OLED board, whose product ID it already used.
 */
final class Board {
    static final String ESP32C3_OLED = "esp32c3-oled";

    static String fromProductId(int productId) {
        switch (productId) {
            case 0x4001: return ESP32C3_OLED;
            case 0x4002: return "esp32c3";
            case 0x4003: return "esp32c6";
            case 0x4004: return "nrf52840";
            default: return null;
        }
    }

    static String displayName(String id) {
        if (id == null)
            return "Unknown board";
        switch (id) {
            case ESP32C3_OLED: return "ESP32-C3 OLED";
            case "esp32c3": return "ESP32-C3";
            case "esp32c6": return "ESP32-C6";
            case "nrf52840": return "nRF52840";
            default: return id;
        }
    }

    private Board() {
    }
}
