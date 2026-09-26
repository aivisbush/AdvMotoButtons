package com.bush.motobuttons;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothDevice;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import java.io.File;
import java.io.FileOutputStream;

import no.nordicsemi.android.dfu.DfuProgressListener;
import no.nordicsemi.android.dfu.DfuProgressListenerAdapter;
import no.nordicsemi.android.dfu.DfuServiceController;
import no.nordicsemi.android.dfu.DfuServiceInitiator;
import no.nordicsemi.android.dfu.DfuServiceListenerHelper;

/**
 * Sends a DFU package (.zip) to the nRF52840 controller with Nordic's DFU
 * library: the firmware's DFU service restarts the board into its bootloader,
 * which takes the image over the same bond and starts it.
 */
@SuppressLint("MissingPermission")
final class NrfDfuClient implements FirmwareUpdate {
    // Gives up when the transfer makes no progress for this long.
    private static final long STALL_TIMEOUT_MS = 30000;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Runnable stalled = () -> {
        if (this.controller != null)
            this.controller.abort();
        finish(false, "The controller stopped responding. Switch it off and on, then try again.");
    };
    private final Context context;
    private final BluetoothDevice device;
    private final byte[] zip;
    private final Listener listener;
    private DfuServiceController controller;
    private boolean finished;

    NrfDfuClient(Context context, BluetoothDevice device, byte[] zip, Listener listener) {
        this.context = context.getApplicationContext();
        this.device = device;
        this.zip = zip;
        this.listener = listener;
    }

    @Override
    public void start() {
        File file = new File(context.getCacheDir(), "nrf-update.zip");
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(zip);
        } catch (Exception e) {
            finish(false, "Could not prepare the update: " + e.getMessage());
            return;
        }
        listener.onStatus("Connecting to " + device.getName() + "...");
        DfuServiceListenerHelper.registerProgressListener(context, progress);
        kick();
        controller = new DfuServiceInitiator(device.getAddress())
            .setDeviceName(device.getName())
            .setKeepBond(true)
            .setForeground(false)
            .setDisableNotification(true)
            .setPacketsReceiptNotificationsEnabled(true)
            // The bootloader's legacy DFU takes 20-byte packets only; a larger MTU stalls it.
            .disableMtuRequest()
            .setZip(file.getAbsolutePath())
            .start(context, DfuService.class);
    }

    @Override
    public void cancel() {
        if (controller != null && !finished)
            controller.abort();
        finish(false, "cancelled");
    }

    private void finish(boolean success, String message) {
        if (finished)
            return;
        finished = true;
        main.removeCallbacks(stalled);
        DfuServiceListenerHelper.unregisterProgressListener(context, progress);
        listener.onFinished(success, message);
    }

    private void kick() {
        main.removeCallbacks(stalled);
        main.postDelayed(stalled, STALL_TIMEOUT_MS);
    }

    private final DfuProgressListener progress = new DfuProgressListenerAdapter() {
        @Override
        public void onEnablingDfuMode(String deviceAddress) {
            kick();
            listener.onStatus("Restarting the controller into update mode...");
        }

        @Override
        public void onDfuProcessStarting(String deviceAddress) {
            kick();
            listener.onStatus("Starting the update...");
        }

        @Override
        public void onProgressChanged(String deviceAddress, int percent, float speed, float avgSpeed, int currentPart, int partsTotal) {
            kick();
            listener.onProgress((int) ((long) zip.length * percent / 100), zip.length);
        }

        @Override
        public void onFirmwareValidating(String deviceAddress) {
            kick();
            listener.onStatus("Checking the firmware...");
        }

        @Override
        public void onDfuCompleted(String deviceAddress) {
            finish(true, "Update complete.");
        }

        @Override
        public void onDfuAborted(String deviceAddress) {
            finish(false, "Update cancelled.");
        }

        @Override
        public void onError(String deviceAddress, int error, int errorType, String message) {
            finish(false, "Update failed: " + message);
        }
    };
}
