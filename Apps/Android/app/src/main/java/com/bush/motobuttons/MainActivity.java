package com.bush.motobuttons;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.RadioGroup;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.android.material.radiobutton.MaterialRadioButton;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Finds the paired Moto Buttons controller, checks the project site for newer
 * firmware and installs it over Bluetooth; also checks DMD2 support.
 */
@SuppressLint({"MissingPermission", "SetTextI18n"})
public class MainActivity extends AppCompatActivity {
    private static final int REQUEST_PERMISSION = 1;
    private static final int REQUEST_FILE = 2;

    private View mainView, doneView;
    private TextView controllerStatus, compareText, fileText, progressText, statusText, doneText;
    private TextView dmdStatus, dmdHelp;
    private LinearProgressIndicator searchProgress, updateProgress;
    private RadioGroup controllerGroup;
    private Button searchButton, updateButton, dmdButton;

    private final ExecutorService background = Executors.newSingleThreadExecutor();
    private final Map<String, Controller> controllers = new LinkedHashMap<>();
    private final Deque<BluetoothDevice> probeQueue = new ArrayDeque<>();
    private boolean searching;

    private FirmwareFile localFirmware;   // chosen with "Use a .bin file"
    private Map<String, RemoteFirmware.Channels> releases; // latest per board on the project site
    private boolean wantBeta; // "Beta firmware" menu switch
    private String remoteError;
    private boolean downloading;
    private FirmwareUpdate ota;

    private static final class Controller {
        final BluetoothDevice device;
        final String version;
        final String board;
        final boolean canUpdate; // has the Bluetooth update service

        Controller(BluetoothDevice device, String version, String board, boolean canUpdate) {
            this.device = device;
            this.version = version;
            this.board = board;
            this.canUpdate = canUpdate;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mainView = findViewById(R.id.mainView);
        doneView = findViewById(R.id.doneView);
        controllerStatus = findViewById(R.id.controllerStatus);
        searchProgress = findViewById(R.id.searchProgress);
        controllerGroup = findViewById(R.id.controllerGroup);
        searchButton = findViewById(R.id.searchButton);
        compareText = findViewById(R.id.compareText);
        fileText = findViewById(R.id.fileText);
        updateProgress = findViewById(R.id.updateProgress);
        progressText = findViewById(R.id.progressText);
        updateButton = findViewById(R.id.updateButton);
        statusText = findViewById(R.id.statusText);
        dmdStatus = findViewById(R.id.dmdStatus);
        dmdHelp = findViewById(R.id.dmdHelp);
        dmdButton = findViewById(R.id.dmdButton);
        doneText = findViewById(R.id.doneText);

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.inflateMenu(R.menu.main);
        wantBeta = getPreferences(MODE_PRIVATE).getBoolean("beta", false);
        toolbar.getMenu().findItem(R.id.action_beta).setChecked(wantBeta);
        toolbar.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == R.id.action_use_file) {
                chooseFile();
                return true;
            }
            if (item.getItemId() == R.id.action_beta) {
                wantBeta = !item.isChecked();
                item.setChecked(wantBeta);
                getPreferences(MODE_PRIVATE).edit().putBoolean("beta", wantBeta).apply();
                refreshUpdateCard();
                return true;
            }
            return false;
        });
        searchButton.setOnClickListener(v -> startSearch());
        updateButton.setOnClickListener(v -> confirmUpdate());
        controllerGroup.setOnCheckedChangeListener((group, id) -> refreshUpdateCard());
        findViewById(R.id.closeButton).setOnClickListener(v -> finishAndRemoveTask());
        findViewById(R.id.removeButton).setOnClickListener(v -> removeThisApp());

        if (!hasBluetoothPermission())
            requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT}, REQUEST_PERMISSION);
        checkForUpdate();
        startSearch();
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshDmdCard(); // the user may come back from Settings or the installer
    }

    /* ---------------------------- controllers --------------------------- */

    private boolean hasBluetoothPermission() {
        return Build.VERSION.SDK_INT < 31
            || checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQUEST_PERMISSION && hasBluetoothPermission())
            startSearch();
    }

    private void startSearch() {
        if (!hasBluetoothPermission()) {
            controllerStatus.setText("Allow nearby devices access to find the controller.");
            return;
        }
        BluetoothManager manager = getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            controllerStatus.setText("Turn Bluetooth on, then search again.");
            return;
        }
        if (searching)
            return;

        searching = true;
        controllers.clear();
        controllerGroup.removeAllViews();
        probeQueue.clear();
        // Controllers are LE peripherals (generic HID class); connected ones first.
        List<BluetoothDevice> connected = manager.getConnectedDevices(BluetoothProfile.GATT);
        for (BluetoothDevice device : adapter.getBondedDevices()) {
            if (device.getType() == BluetoothDevice.DEVICE_TYPE_CLASSIC || !isPeripheral(device))
                continue;
            if (connected.contains(device))
                probeQueue.addFirst(device);
            else
                probeQueue.addLast(device);
        }
        searchProgress.setVisibility(View.VISIBLE);
        searchButton.setEnabled(false);
        controllerStatus.setText("Looking for your controller...");
        refreshUpdateCard();
        probeNext();
    }

    private static boolean isPeripheral(BluetoothDevice device) {
        BluetoothClass cls = device.getBluetoothClass();
        if (cls == null)
            return true;
        int major = cls.getMajorDeviceClass();
        return major == BluetoothClass.Device.Major.PERIPHERAL || major == BluetoothClass.Device.Major.UNCATEGORIZED;
    }

    private void probeNext() {
        BluetoothDevice next = probeQueue.poll();
        if (next == null) {
            searchFinished();
            return;
        }
        ControllerProbe.probe(this, next, (device, result) -> {
            if (result.isController)
                addController(device, result);
            probeNext();
        });
    }

    private void searchFinished() {
        searching = false;
        searchProgress.setVisibility(View.GONE);
        searchButton.setEnabled(true);
        controllerStatus.setText(controllers.isEmpty()
            ? "No controller found. Switch it on and pair it: Bluetooth settings > Scan > tap it > Pair."
            : controllers.size() == 1 ? "Connected." : controllers.size() + " controllers found. Choose one.");
        refreshUpdateCard();
    }

    private void addController(BluetoothDevice device, ControllerProbe.Result result) {
        controllers.put(device.getAddress(), new Controller(device, result.version, result.board, result.canUpdate));
        MaterialRadioButton button = new MaterialRadioButton(this);
        button.setId(View.generateViewId());
        button.setTag(device.getAddress());
        // Name and version, board, then model and manufacturer when the controller reports them.
        StringBuilder text = new StringBuilder(device.getName() + "  ·  v" + result.version + "\n" + Board.displayName(result.board));
        String maker = result.model != null && result.manufacturer != null ? result.model + "  ·  " + result.manufacturer
            : result.model != null ? result.model : result.manufacturer;
        if (maker != null)
            text.append("\n").append(maker);
        button.setText(text);
        controllerGroup.addView(button);
        if (controllers.size() == 1)
            button.setChecked(true);
    }

    private Controller selectedController() {
        View checked = controllerGroup.findViewById(controllerGroup.getCheckedRadioButtonId());
        return checked == null ? null : controllers.get((String) checked.getTag());
    }

    /* ------------------------------ firmware ---------------------------- */

    private void checkForUpdate() {
        background.execute(() -> {
            Map<String, RemoteFirmware.Channels> latest = null;
            String error = null;
            try {
                latest = RemoteFirmware.fetchLatest();
            } catch (Exception e) {
                error = e.getMessage();
            }
            Map<String, RemoteFirmware.Channels> found = latest;
            String failure = error;
            runOnUiThread(() -> {
                releases = found;
                remoteError = failure;
                refreshUpdateCard();
            });
        });
    }

    private void chooseFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_FILE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_FILE || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;
        Uri uri = data.getData();
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            setLocalFirmware(readAll(in), displayName(uri));
        } catch (Exception e) {
            statusText.setText("Could not read the file: " + e.getMessage());
        }
    }

    private void setLocalFirmware(byte[] bytes, String name) {
        String[] error = new String[1];
        FirmwareFile file = FirmwareFile.parse(bytes, name, error);
        if (file == null) {
            statusText.setText(error[0]);
            return;
        }
        localFirmware = file;
        statusText.setText("");
        refreshUpdateCard();
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst())
                return cursor.getString(0);
        } catch (Exception ignored) {
        }
        return uri.getLastPathSegment();
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = in.read(buffer)) > 0)
            out.write(buffer, 0, n);
        return out.toByteArray();
    }

    /** The site's latest release for this controller's board, or null. */
    private RemoteFirmware releaseFor(Controller controller) {
        RemoteFirmware.Channels channels = controller == null || releases == null ? null : releases.get(controller.board);
        return channels == null ? null : channels.pick(wantBeta);
    }

    /** Version the update would install: a chosen file wins over the site. */
    private String targetVersion(Controller controller) {
        if (localFirmware != null)
            return localFirmware.version;
        RemoteFirmware release = releaseFor(controller);
        return release != null ? release.version : null;
    }

    /* ------------------------------- update ----------------------------- */

    private void refreshUpdateCard() {
        Controller controller = selectedController();
        boolean busy = ota != null || downloading;
        if (controller != null && !controller.canUpdate && !busy) {
            fileText.setText(Board.displayName(controller.board));
            compareText.setText("Controller firmware v" + controller.version
                + ". Bluetooth updates are not available for this board yet; update it over USB.");
            updateButton.setVisibility(View.GONE);
            return;
        }
        RemoteFirmware release = releaseFor(controller);
        String target = targetVersion(controller);
        boolean haveTarget = localFirmware != null || release != null;
        String board = controller == null ? "" : Board.displayName(controller.board) + "  ·  ";
        fileText.setText(localFirmware != null ? "File: " + localFirmware.name
            : release != null ? board + (release.isBeta() ? "Latest beta: v" : "Latest release: v") + release.version
            : remoteError != null ? "Could not check for updates (" + remoteError + ")."
            : releases == null ? "Checking for updates..."
            : controller != null ? board + "No release published for this board yet." : "");
        if (busy)
            return;

        updateButton.setVisibility(View.GONE);
        if (controller == null) {
            compareText.setText(searching ? "Waiting for the controller..." : "Connect the controller to check its firmware.");
            return;
        }
        if (localFirmware != null && !localFirmware.fitsBoard(controller.board)) {
            compareText.setText("This file is for " + Board.displayName(localFirmware.board)
                + ", but the controller is " + Board.displayName(controller.board) + ".");
            return;
        }
        if (!haveTarget) {
            compareText.setText("Controller firmware v" + controller.version + ".");
            return;
        }
        int cmp = target == null ? -1 : FirmwareFile.compareVersions(controller.version, target);
        if (cmp < 0) {
            compareText.setText(target == null ? "Controller v" + controller.version + ". The file's version is unknown."
                : "Update available: v" + controller.version + "  →  v" + target);
            updateButton.setText(target == null ? getString(R.string.update_firmware) : "Update to v" + target);
            updateButton.setVisibility(View.VISIBLE);
        } else if (cmp == 0) {
            compareText.setText("Up to date (v" + controller.version + ").");
            if (localFirmware != null) {
                updateButton.setText("Reinstall v" + target);
                updateButton.setVisibility(View.VISIBLE);
            }
        } else {
            compareText.setText("Controller v" + controller.version + " is newer than v" + target + ".");
            if (localFirmware != null) {
                updateButton.setText("Install older v" + target);
                updateButton.setVisibility(View.VISIBLE);
            }
        }
    }

    private void confirmUpdate() {
        Controller controller = selectedController();
        if (controller == null)
            return;
        String target = targetVersion(controller);
        new MaterialAlertDialogBuilder(this)
            .setTitle("Update firmware?")
            .setMessage("Install " + (target == null ? "the firmware" : "v" + target) + " on " + controller.device.getName()
                + "?\n\nKeep the phone close and the controller powered until it restarts, about a minute and a half.")
            .setPositiveButton("Update", (d, w) -> {
                if (localFirmware != null)
                    startUpdate(controller, localFirmware);
                else
                    downloadAndUpdate(controller);
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void downloadAndUpdate(Controller controller) {
        RemoteFirmware remote = releaseFor(controller);
        if (remote == null)
            return;
        downloading = true;
        updateButton.setVisibility(View.GONE);
        updateProgress.setVisibility(View.VISIBLE);
        updateProgress.setIndeterminate(true);
        progressText.setText("Downloading v" + remote.version + "...");
        background.execute(() -> {
            FirmwareFile file = null;
            String error = null;
            try {
                file = remote.download();
            } catch (Exception e) {
                error = e.getMessage();
            }
            FirmwareFile downloaded = file;
            String failure = error;
            runOnUiThread(() -> {
                downloading = false;
                updateProgress.setIndeterminate(false);
                if (downloaded == null) {
                    updateProgress.setVisibility(View.GONE);
                    progressText.setText("");
                    statusText.setText("Download failed: " + failure);
                    refreshUpdateCard();
                } else {
                    startUpdate(controller, downloaded);
                }
            });
        });
    }

    private void startUpdate(Controller controller, FirmwareFile firmware) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        updateButton.setVisibility(View.GONE);
        updateProgress.setVisibility(View.VISIBLE);
        updateProgress.setIndeterminate(false);
        updateProgress.setProgressCompat(0, false);
        progressText.setText("");
        statusText.setText("");
        String version = firmware.version;
        FirmwareUpdate.Listener listener = new FirmwareUpdate.Listener() {
            @Override
            public void onProgress(int sent, int total) {
                int percent = (int) (100L * sent / total);
                updateProgress.setProgressCompat(percent, true);
                progressText.setText("Sending  " + sent / 1024 + " / " + total / 1024 + " KB");
            }

            @Override
            public void onStatus(String text) {
                progressText.setText(text);
            }

            @Override
            public void onFinished(boolean success, String message) {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                ota = null;
                updateProgress.setVisibility(View.GONE);
                progressText.setText("");
                if (success) {
                    showDone(controller.device.getName(), version);
                } else {
                    statusText.setText(message);
                    refreshUpdateCard();
                }
            }
        };
        ota = firmware.dfuPackage
            ? new NrfDfuClient(this, controller.device, firmware.bytes, listener)
            : new OtaClient(this, controller.device, firmware.bytes, listener);
        refreshUpdateCard();
        ota.start();
    }

    private void showDone(String name, String version) {
        doneText.setText(name + (version == null ? " has the new firmware" : " now runs v" + version)
            + ".\nIt restarts by itself and reconnects in a few seconds.");
        mainView.setVisibility(View.GONE);
        doneView.setVisibility(View.VISIBLE);
    }

    private void removeThisApp() {
        startActivity(new Intent(Intent.ACTION_DELETE, Uri.parse("package:" + getPackageName())));
    }

    /* ------------------------------ DMD2 -------------------------------- */

    private void refreshDmdCard() {
        switch (DmdSupport.state(this)) {
            case NOT_INSTALLED:
                dmdStatus.setText("DMD Manage is not installed.");
                dmdHelp.setText("Only needed to use the controller as a DMD Remote in DMD2. Install it from THORK Racing's page.");
                dmdButton.setText("Get DMD Manage");
                dmdButton.setOnClickListener(v -> startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(DmdSupport.DOWNLOAD_PAGE))));
                dmdButton.setVisibility(View.VISIBLE);
                break;
            case ACCESSIBILITY_OFF:
                dmdStatus.setText("DMD Manage is installed but not switched on.");
                dmdHelp.setText("Accessibility > Installed apps > Manage > On. If it is greyed out: App info > ⋮ > Allow restricted settings, then try again.");
                dmdButton.setText("Open accessibility settings");
                dmdButton.setOnClickListener(v -> new MaterialAlertDialogBuilder(this)
                    .setTitle("Switch on DMD Manage")
                    .setMessage("1. In Accessibility, open Installed apps > Manage and switch it on.\n\n"
                        + "2. If the switch is greyed out, open App info first, tap ⋮ (top right) > Allow restricted settings, then come back.")
                    .setPositiveButton("Accessibility", (d, w) -> startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)))
                    .setNeutralButton("App info", (d, w) -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.parse("package:" + DmdSupport.PACKAGE))))
                    .show());
                dmdButton.setVisibility(View.VISIBLE);
                break;
            default:
                dmdStatus.setText("Ready. DMD2 sees the controller as DMD Remote 3.");
                dmdHelp.setText("");
                dmdButton.setVisibility(View.GONE);
                break;
        }
    }

    @Override
    protected void onDestroy() {
        if (ota != null)
            ota.cancel();
        background.shutdownNow();
        super.onDestroy();
    }
}
