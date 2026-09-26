package com.bush.motobuttons;

import android.app.Activity;

import no.nordicsemi.android.dfu.DfuBaseService;

/** Runs Nordic's DFU transfer for the nRF52840 controller. */
public class DfuService extends DfuBaseService {
    @Override
    protected Class<? extends Activity> getNotificationTarget() {
        return MainActivity.class;
    }

    @Override
    protected boolean isDebug() {
        return BuildConfig.DEBUG;
    }
}
