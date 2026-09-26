package com.bush.motobuttons;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.UUID;

/**
 * Connects to one paired device and tells whether it is a Moto Buttons
 * controller, which firmware it runs (Firmware Revision), which board it
 * is (PnP ID product) and its Manufacturer Name and Model Number. A
 * controller either has an update service or, like the nRF52840 build
 * before 2.3.0, reports one of our boards in its PnP ID.
 */
@SuppressLint("MissingPermission")
final class ControllerProbe {
    /** What the probe found; manufacturer and model are null when not reported. */
    static final class Result {
        boolean isController;
        boolean canUpdate;
        String version = "?";
        String board = Board.ESP32C3_OLED; // older firmware did not report a board
        String manufacturer;
        String model;
    }

    interface Callback {
        void onResult(BluetoothDevice device, Result result);
    }

    private static final long TIMEOUT_MS = 6000;
    // PnP ID vendors of our firmware: Espressif (ESP32 boards), Nordic (nRF52840).
    private static final int VENDOR_ESPRESSIF = 0x303A;
    private static final int VENDOR_NORDIC = 0x1915;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final BluetoothDevice device;
    private final Callback callback;
    private final Result result = new Result();
    private final Deque<BluetoothGattCharacteristic> reads = new ArrayDeque<>();
    private BluetoothGatt gatt;
    private boolean finished;
    private boolean boardReported;

    private ControllerProbe(BluetoothDevice device, Callback callback) {
        this.device = device;
        this.callback = callback;
    }

    static void probe(Context context, BluetoothDevice device, Callback callback) {
        ControllerProbe probe = new ControllerProbe(device, callback);
        probe.gatt = device.connectGatt(context, false, probe.gattCallback, BluetoothDevice.TRANSPORT_LE);
        probe.main.postDelayed(() -> probe.finish(false), TIMEOUT_MS);
    }

    /** complete: the reads ran; otherwise the probe failed or timed out. */
    private void finish(boolean complete) {
        main.post(() -> {
            if (finished)
                return;
            finished = true;
            if (gatt != null) {
                gatt.disconnect();
                gatt.close();
            }
            result.isController = complete && (result.canUpdate || boardReported);
            callback.onResult(device, result);
        });
    }

    private void readNext() {
        while (!reads.isEmpty()) {
            if (gatt.readCharacteristic(reads.poll()))
                return;
        }
        finish(true);
    }

    private static String text(byte[] value) {
        String s = new String(value, StandardCharsets.UTF_8).trim();
        return s.isEmpty() ? null : s;
    }

    private void onRead(BluetoothGattCharacteristic characteristic, byte[] value, int status) {
        if (status == BluetoothGatt.GATT_SUCCESS && value != null) {
            UUID uuid = characteristic.getUuid();
            if (uuid.equals(Protocol.FIRMWARE_REVISION)) {
                String version = text(value);
                if (version != null)
                    result.version = version;
            } else if (uuid.equals(Protocol.MANUFACTURER_NAME)) {
                result.manufacturer = text(value);
            } else if (uuid.equals(Protocol.MODEL_NUMBER)) {
                result.model = text(value);
            } else if (uuid.equals(Protocol.PNP_ID) && value.length >= 5) {
                // PnP ID: source(1) vendor(2) product(2) version(2), little endian.
                int vendor = (value[1] & 0xFF) | (value[2] & 0xFF) << 8;
                String id = Board.fromProductId((value[3] & 0xFF) | (value[4] & 0xFF) << 8);
                if (id != null) {
                    result.board = id;
                    boardReported = vendor == VENDOR_ESPRESSIF || vendor == VENDOR_NORDIC;
                }
            }
        }
        readNext();
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED)
                g.discoverServices();
            else if (newState == BluetoothProfile.STATE_DISCONNECTED)
                finish(false);
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            result.canUpdate = g.getService(Protocol.OTA_SERVICE) != null
                || g.getService(Protocol.NORDIC_DFU_SERVICE) != null;
            BluetoothGattService info = g.getService(Protocol.DEVICE_INFO_SERVICE);
            if (info == null) {
                finish(result.canUpdate); // no update service and no device information: not ours
                return;
            }
            for (UUID uuid : new UUID[]{Protocol.FIRMWARE_REVISION, Protocol.PNP_ID,
                Protocol.MANUFACTURER_NAME, Protocol.MODEL_NUMBER}) {
                BluetoothGattCharacteristic characteristic = info.getCharacteristic(uuid);
                if (characteristic != null)
                    reads.add(characteristic);
            }
            readNext();
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic characteristic, byte[] value, int status) {
            onRead(characteristic, value, status);
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic characteristic, int status) {
            onRead(characteristic, characteristic.getValue(), status);
        }
    };
}
