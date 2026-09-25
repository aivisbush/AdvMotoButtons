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

/**
 * Connects to one paired device and tells whether it is a Moto Buttons
 * controller (has the update service), which firmware it runs (Firmware
 * Revision) and which board it is (PnP ID product).
 */
@SuppressLint("MissingPermission")
final class ControllerProbe {
    interface Callback {
        void onResult(BluetoothDevice device, boolean isController, String firmwareVersion, String board);
    }

    private static final long TIMEOUT_MS = 6000;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final BluetoothDevice device;
    private final Callback callback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic pnp;
    private boolean finished;
    private String version = "?";
    private String board = Board.ESP32C3_OLED; // older firmware did not report a board

    private ControllerProbe(BluetoothDevice device, Callback callback) {
        this.device = device;
        this.callback = callback;
    }

    static void probe(Context context, BluetoothDevice device, Callback callback) {
        ControllerProbe probe = new ControllerProbe(device, callback);
        probe.gatt = device.connectGatt(context, false, probe.gattCallback, BluetoothDevice.TRANSPORT_LE);
        probe.main.postDelayed(() -> probe.finish(false), TIMEOUT_MS);
    }

    private void finish(boolean isController) {
        main.post(() -> {
            if (finished)
                return;
            finished = true;
            if (gatt != null) {
                gatt.disconnect();
                gatt.close();
            }
            callback.onResult(device, isController, version, board);
        });
    }

    private void onRead(BluetoothGattCharacteristic characteristic, byte[] value, int status) {
        boolean ok = status == BluetoothGatt.GATT_SUCCESS && value != null;
        if (characteristic.getUuid().equals(Protocol.FIRMWARE_REVISION)) {
            if (ok)
                version = new String(value, StandardCharsets.UTF_8).trim();
            if (pnp == null || !gatt.readCharacteristic(pnp))
                finish(true);
        } else {
            // PnP ID: source(1) vendor(2) product(2) version(2), little endian.
            if (ok && value.length >= 5) {
                String id = Board.fromProductId((value[3] & 0xFF) | (value[4] & 0xFF) << 8);
                if (id != null)
                    board = id;
            }
            finish(true);
        }
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
            if (g.getService(Protocol.OTA_SERVICE) == null) {
                finish(false);
                return;
            }
            BluetoothGattService info = g.getService(Protocol.DEVICE_INFO_SERVICE);
            BluetoothGattCharacteristic revision = info == null ? null : info.getCharacteristic(Protocol.FIRMWARE_REVISION);
            pnp = info == null ? null : info.getCharacteristic(Protocol.PNP_ID);
            if (revision != null && g.readCharacteristic(revision))
                return;
            if (pnp == null || !g.readCharacteristic(pnp))
                finish(true);
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
