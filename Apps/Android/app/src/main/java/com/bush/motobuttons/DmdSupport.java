package com.bush.motobuttons;

import android.accessibilityservice.AccessibilityServiceInfo;
import android.content.Context;
import android.content.pm.PackageManager;
import android.view.accessibility.AccessibilityManager;

/**
 * DMD2 reads the controller as a DMD Remote through THORK's DMD Manage app,
 * whose accessibility service passes the keys on. This only checks it.
 */
final class DmdSupport {
    static final String PACKAGE = "com.thorkracing.wireddevices";
    static final String DOWNLOAD_PAGE = "https://docs.dmdnavigation.com/otherapps/manage-app/";

    enum State { NOT_INSTALLED, ACCESSIBILITY_OFF, READY }

    static State state(Context context) {
        try {
            context.getPackageManager().getPackageInfo(PACKAGE, 0);
        } catch (PackageManager.NameNotFoundException e) {
            return State.NOT_INSTALLED;
        }
        AccessibilityManager manager = context.getSystemService(AccessibilityManager.class);
        if (manager != null) {
            for (AccessibilityServiceInfo info : manager.getEnabledAccessibilityServiceList(AccessibilityServiceInfo.FEEDBACK_ALL_MASK)) {
                if (info.getId() != null && info.getId().startsWith(PACKAGE + "/"))
                    return State.READY;
            }
        }
        return State.ACCESSIBILITY_OFF;
    }

    private DmdSupport() {
    }
}
