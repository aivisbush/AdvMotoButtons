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
 * controller, which firmware it runs (Firmware Revision) and which board it
 * is (PnP ID product). A controller either has the update service or, like
 * the nRF52840 build, reports one of our boards in its PnP ID.
 */
@SuppressLint("MissingPermission")
final class ControllerProbe {
    interface Callback {
        void onResult(BluetoothDevice device, boolean isController, String firmwareVersion, String board, boolean canUpdate);
    }

    private static final long TIMEOUT_MS = 6000;
    // PnP ID vendors of our firmware: Espressif (ESP32 boards), Nordic (nRF52840).
    private static final int VENDOR_ESPRESSIF = 0x303A;
    private static final int VENDOR_NORDIC = 0x1915;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final BluetoothDevice device;
    private final Callback callback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic pnp;
    private boolean finished;
    private boolean hasUpdateService;
    private boolean boardReported;
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
            boolean isController = complete && (hasUpdateService || boardReported);
            callback.onResult(device, isController, version, board, hasUpdateService);
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
                int vendor = (value[1] & 0xFF) | (value[2] & 0xFF) << 8;
                String id = Board.fromProductId((value[3] & 0xFF) | (value[4] & 0xFF) << 8);
                if (id != null) {
                    board = id;
                    boardReported = vendor == VENDOR_ESPRESSIF || vendor == VENDOR_NORDIC;
                }
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
            hasUpdateService = g.getService(Protocol.OTA_SERVICE) != null;
            BluetoothGattService info = g.getService(Protocol.DEVICE_INFO_SERVICE);
            BluetoothGattCharacteristic revision = info == null ? null : info.getCharacteristic(Protocol.FIRMWARE_REVISION);
            pnp = info == null ? null : info.getCharacteristic(Protocol.PNP_ID);
            if (!hasUpdateService && pnp == null) {
                finish(false); // no update service and no board: not ours
                return;
            }
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
