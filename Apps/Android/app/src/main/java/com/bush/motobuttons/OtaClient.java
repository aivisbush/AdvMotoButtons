package com.bush.motobuttons;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import java.security.MessageDigest;
import java.util.Arrays;

/**
 * Sends one firmware image to one controller: connect, larger MTU,
 * notifications on, START, data chunks written with response, FINISH.
 * The controller verifies the MD5 and restarts into the new image.
 */
@SuppressLint("MissingPermission")
final class OtaClient {
    interface Listener {
        void onProgress(int sent, int total);

        void onStatus(String text);

        void onFinished(boolean success, String message);
    }

    private static final long IDLE_TIMEOUT_MS = 15000;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Context context;
    private final BluetoothDevice device;
    private final byte[] image;
    private final Listener listener;

    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic control;
    private BluetoothGattCharacteristic data;
    private int chunkSize = 20;
    private int offset;
    private boolean finished;
    // Data starts only after both: the START write completed and the
    // controller's STARTED notification (either may arrive first).
    private boolean startWritten;
    private boolean startAccepted;
    private boolean sending;
    private final Runnable idleTimeout = () -> fail("the controller stopped responding");

    OtaClient(Context context, BluetoothDevice device, byte[] image, Listener listener) {
        this.context = context.getApplicationContext();
        this.device = device;
        this.image = image;
        this.listener = listener;
    }

    void start() {
        status("Connecting to " + device.getName() + "...");
        kick();
        gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE);
    }

    void cancel() {
        if (control != null && gatt != null && !finished)
            GattCompat.write(gatt, control, new byte[]{Protocol.CMD_ABORT});
        fail("cancelled");
    }

    static byte[] md5(byte[] bytes) {
        try {
            return MessageDigest.getInstance("MD5").digest(bytes);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }

    private void kick() {
        main.removeCallbacks(idleTimeout);
        main.postDelayed(idleTimeout, IDLE_TIMEOUT_MS);
    }

    private void status(String text) {
        main.post(() -> listener.onStatus(text));
    }

    private void fail(String reason) {
        end(false, "Update failed: " + reason);
    }

    private void end(boolean success, String message) {
        main.post(() -> {
            if (finished)
                return;
            finished = true;
            main.removeCallbacks(idleTimeout);
            if (gatt != null) {
                gatt.disconnect();
                gatt.close();
            }
            listener.onFinished(success, message);
        });
    }

    private void sendStart() {
        byte[] digest = md5(image);
        byte[] command = new byte[1 + 4 + 16];
        command[0] = Protocol.CMD_START;
        int size = image.length;
        command[1] = (byte) size;
        command[2] = (byte) (size >> 8);
        command[3] = (byte) (size >> 16);
        command[4] = (byte) (size >> 24);
        System.arraycopy(digest, 0, command, 5, 16);
        status("Starting update (" + size + " bytes)...");
        if (!GattCompat.write(gatt, control, command))
            fail("could not send START");
    }

    private void sendNextChunk() {
        if (offset >= image.length) {
            status("Verifying on the controller...");
            if (!GattCompat.write(gatt, control, new byte[]{Protocol.CMD_FINISH}))
                fail("could not send FINISH");
            return;
        }
        int end = Math.min(offset + chunkSize, image.length);
        byte[] chunk = Arrays.copyOfRange(image, offset, end);
        if (!GattCompat.write(gatt, data, chunk)) {
            fail("write failed at byte " + offset);
            return;
        }
        offset = end;
    }

    private void beginDataIfReady() {
        if (sending || !startWritten || !startAccepted)
            return;
        sending = true;
        status("Sending firmware...");
        offset = 0;
        sendNextChunk();
    }

    private void onNotification(byte[] value) {
        if (value == null || value.length < 2)
            return;
        kick();
        int event = value[0] & 0xFF;
        int result = value[1] & 0xFF;
        switch (event) {
            case Protocol.EVT_STARTED:
                if (result != 0) {
                    fail(Protocol.statusText(result));
                    return;
                }
                startAccepted = true;
                beginDataIfReady();
                break;
            case Protocol.EVT_FINISHED:
                if (result == 0)
                    end(true, "Update done. The controller restarts with the new firmware.");
                else
                    fail(Protocol.statusText(result));
                break;
            case Protocol.EVT_ABORTED:
                fail("aborted by the controller");
                break;
            case Protocol.EVT_DATA_ERROR:
                fail(Protocol.statusText(result));
                break;
            default:
                break;
        }
    }

    private final BluetoothGattCallback callback = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int statusCode, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                kick();
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                g.requestMtu(Protocol.MTU);
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                fail("disconnected");
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int mtu, int statusCode) {
            kick();
            chunkSize = Math.max(20, mtu - 3);
            g.discoverServices();
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int statusCode) {
            kick();
            BluetoothGattService service = g.getService(Protocol.OTA_SERVICE);
            if (service == null) {
                fail("this device has no Moto Buttons update service");
                return;
            }
            control = service.getCharacteristic(Protocol.OTA_CONTROL);
            data = service.getCharacteristic(Protocol.OTA_DATA);
            if (control == null || data == null || !GattCompat.enableNotifications(g, control))
                fail("update service incomplete");
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor descriptor, int statusCode) {
            kick();
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                fail("could not enable notifications (is the controller paired?)");
                return;
            }
            sendStart();
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic characteristic, int statusCode) {
            kick();
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                fail("write rejected (status " + statusCode + ")");
                return;
            }
            if (characteristic.getUuid().equals(Protocol.OTA_DATA)) {
                int sent = offset;
                main.post(() -> listener.onProgress(sent, image.length));
                sendNextChunk();
            } else if (!startWritten) {
                startWritten = true;
                beginDataIfReady();
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic characteristic, byte[] value) {
            onNotification(value);
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic characteristic) {
            onNotification(characteristic.getValue());
        }
    };
}
