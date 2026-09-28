package com.bush.motobuttons;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.accessibilityservice.GestureDescription;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Path;
import android.graphics.Rect;
import android.os.Build;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;

import androidx.annotation.RequiresApi;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * LVM GEO has no key that centres the map, only its on-screen GPS button.
 * While LVM GEO is in front, this service takes the controller's C key and
 * presses that button instead.
 */
public class LvmGeoService extends AccessibilityService {
    static final String PACKAGE = "com.lvm.mobile.du";
    private static final String TAG = "LvmGeoService";
    // HTML id of the GPS button in LVM GEO's web page (MapToolbar.tsx, LVM GEO 5.4.2).
    private static final String LOCATION_BUTTON_ID = "location-button";
    // PnP ID vendors of our firmware: Espressif (ESP32 boards), Nordic (nRF52840).
    private static final int VENDOR_ESPRESSIF = 0x303A;
    private static final int VENDOR_NORDIC = 0x1915;
    private static final String CONTROLLER_NAME = "DMD-Remote3";
    private static final long TAP_MS = 50;

    private boolean swallowUp; // the C key down went to the GPS button, so its key up does too

    enum State { NOT_INSTALLED, SERVICE_OFF, READY }

    static State state(Context context) {
        try {
            context.getPackageManager().getPackageInfo(PACKAGE, 0);
        } catch (PackageManager.NameNotFoundException e) {
            return State.NOT_INSTALLED;
        }
        // The id may be "package/.Class" or "package/package.Class".
        ComponentName self = new ComponentName(context, LvmGeoService.class);
        AccessibilityManager manager = context.getSystemService(AccessibilityManager.class);
        if (manager != null) {
            for (AccessibilityServiceInfo info : manager.getEnabledAccessibilityServiceList(AccessibilityServiceInfo.FEEDBACK_ALL_MASK)) {
                if (info.getId() != null && self.equals(ComponentName.unflattenFromString(info.getId())))
                    return State.READY;
            }
        }
        return State.SERVICE_OFF;
    }

    @Override
    protected boolean onKeyEvent(KeyEvent event) {
        if (event.getKeyCode() != KeyEvent.KEYCODE_C || !fromController(event))
            return false;
        if (event.getAction() == KeyEvent.ACTION_UP) {
            boolean swallow = swallowUp;
            swallowUp = false;
            return swallow;
        }
        if (event.getAction() != KeyEvent.ACTION_DOWN || !lvmGeoInFront())
            return false;
        if (event.getRepeatCount() == 0)
            pressLocationButton();
        swallowUp = true;
        return true;
    }

    private static boolean fromController(KeyEvent event) {
        InputDevice device = event.getDevice();
        if (device == null)
            return false;
        boolean ourVendor = device.getVendorId() == VENDOR_ESPRESSIF || device.getVendorId() == VENDOR_NORDIC;
        return (ourVendor && Board.fromProductId(device.getProductId()) != null)
            || (device.getName() != null && device.getName().contains(CONTROLLER_NAME));
    }

    private boolean lvmGeoInFront() {
        AccessibilityNodeInfo root = getRootInActiveWindow();
        return root != null && PACKAGE.contentEquals(root.getPackageName());
    }

    /* The button toggles: white, it starts following the position (the map
     * centres); filled blue, it is following already and a press would turn
     * GPS off. So press it only when it is not filled blue.
     */
    private void pressLocationButton() {
        AccessibilityNodeInfo button = findLocationButton(getRootInActiveWindow());
        if (button == null) {
            Log.w(TAG, "GPS button not found");
            return;
        }
        Rect bounds = new Rect();
        button.getBoundsInScreen(bounds);
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            tap(bounds);
            return;
        }
        takeScreenshot(Display.DEFAULT_DISPLAY, getMainExecutor(), new TakeScreenshotCallback() {
            @Override
            public void onSuccess(ScreenshotResult screenshot) {
                boolean following = isFilledBlue(screenshot, bounds);
                screenshot.getHardwareBuffer().close();
                if (!following)
                    tap(bounds);
            }

            @Override
            public void onFailure(int errorCode) {
                Log.w(TAG, "Screenshot failed: " + errorCode);
                tap(bounds);
            }
        });
    }

    /** Colour of the button's face between the icon and the rim. */
    @RequiresApi(Build.VERSION_CODES.R)
    private static boolean isFilledBlue(ScreenshotResult screenshot, Rect bounds) {
        Bitmap hardware = Bitmap.wrapHardwareBuffer(screenshot.getHardwareBuffer(), screenshot.getColorSpace());
        if (hardware == null)
            return false;
        Bitmap bitmap = hardware.copy(Bitmap.Config.ARGB_8888, false);
        hardware.recycle();
        int x = bounds.left + bounds.width() * 15 / 100;
        int y = bounds.centerY();
        boolean inside = x >= 0 && y >= 0 && x < bitmap.getWidth() && y < bitmap.getHeight();
        int pixel = inside ? bitmap.getPixel(x, y) : Color.WHITE;
        bitmap.recycle();
        return Color.blue(pixel) > 150 && Color.red(pixel) < 100;
    }

    // A real tap: the page ignores the accessibility click action.
    private void tap(Rect bounds) {
        Path path = new Path();
        path.moveTo(bounds.exactCenterX(), bounds.exactCenterY());
        dispatchGesture(new GestureDescription.Builder()
            .addStroke(new GestureDescription.StrokeDescription(path, 0, TAP_MS))
            .build(), null, null);
    }

    /** The web page's GPS button, found by its HTML id, or null. */
    private static AccessibilityNodeInfo findLocationButton(AccessibilityNodeInfo root) {
        if (root == null)
            return null;
        Deque<AccessibilityNodeInfo> pending = new ArrayDeque<>();
        pending.add(root);
        while (!pending.isEmpty()) {
            AccessibilityNodeInfo node = pending.poll();
            String id = node.getViewIdResourceName();
            if (id != null && (id.equals(LOCATION_BUTTON_ID) || id.endsWith("/" + LOCATION_BUTTON_ID)))
                return node;
            for (int i = 0; i < node.getChildCount(); i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child != null)
                    pending.add(child);
            }
        }
        return null;
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
    }

    @Override
    public void onInterrupt() {
    }
}
