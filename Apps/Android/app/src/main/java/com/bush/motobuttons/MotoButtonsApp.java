package com.bush.motobuttons;

import android.app.Application;

import com.google.android.material.color.DynamicColors;

/** Material You: wallpaper colours on Android 12+, the brand palette before. */
public class MotoButtonsApp extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        DynamicColors.applyToActivitiesIfAvailable(this);
    }
}
