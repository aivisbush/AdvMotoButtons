package com.bush.motobuttons;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Results for the Windows tool, which reads them over adb:
 * .../Android/data/com.bush.motobuttons/files/status.json
 * {"seq":N,"state":"scanning|scan_done|updating|done|failed","progress":0-100,
 *  "message":"...","controllers":[{"name":"...","address":"...","version":"..."}]}
 * seq echoes the command so the tool never reads a stale answer.
 */
final class StatusFile {
    static final String NAME = "status.json";

    private final File file;
    private final File temp;

    StatusFile(Context context) {
        File dir = context.getExternalFilesDir(null);
        file = new File(dir, NAME);
        temp = new File(dir, NAME + ".tmp");
    }

    void write(int seq, String state, int progress, String message, JSONArray controllers) {
        if (seq < 0)
            return;
        try {
            JSONObject json = new JSONObject();
            json.put("seq", seq);
            json.put("state", state);
            json.put("progress", progress);
            json.put("message", message == null ? "" : message);
            json.put("controllers", controllers == null ? new JSONArray() : controllers);
            try (FileOutputStream out = new FileOutputStream(temp)) {
                out.write(json.toString().getBytes(StandardCharsets.UTF_8));
            }
            if (!temp.renameTo(file)) {
                file.delete();
                temp.renameTo(file);
            }
        } catch (Exception ignored) {
        }
    }
}
