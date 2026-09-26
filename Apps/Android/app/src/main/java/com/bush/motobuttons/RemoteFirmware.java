package com.bush.motobuttons;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * The latest firmware per board and channel, published on the project's GitHub Pages site:
 * SITE_URL/firmware/latest.json =
 *   {"boards":{"esp32c3-oled":{"prod":{"version":"2.4.0","file":"esp32c3-oled/prod/MotoButtons2-2.4.0.bin",
 *                                     "size":..,"md5":".."},"beta":{...}}}}
 * The nRF52840 entries point to a DFU package (.zip) instead of a .bin.
 * Blocking calls; run them off the main thread.
 */
final class RemoteFirmware {
    final String board;
    final String version;
    final String url;
    final int size;
    final String md5;

    private RemoteFirmware(String board, String version, String url, int size, String md5) {
        this.board = board;
        this.version = version;
        this.url = url;
        this.size = size;
        this.md5 = md5;
    }

    /** Production and beta release of one board; either may be null. */
    static final class Channels {
        RemoteFirmware prod;
        RemoteFirmware beta;

        /** The release to offer: the beta only when wanted and newer than production. */
        RemoteFirmware pick(boolean wantBeta) {
            if (wantBeta && beta != null && (prod == null || FirmwareFile.compareVersions(beta.version, prod.version) > 0))
                return beta;
            return prod;
        }
    }

    final boolean isBeta() {
        return version.contains("-");
    }

    /** Latest releases per board id. */
    static Map<String, Channels> fetchLatest() throws Exception {
        String base = BuildConfig.SITE_URL + "firmware/";
        JSONObject json = new JSONObject(new String(get(base + "latest.json", 64 * 1024), StandardCharsets.UTF_8));
        JSONObject boards = json.getJSONObject("boards");
        Map<String, Channels> result = new HashMap<>();
        for (Iterator<String> it = boards.keys(); it.hasNext(); ) {
            String board = it.next();
            JSONObject entry = boards.getJSONObject(board);
            Channels channels = new Channels();
            if (entry.has("version")) {
                channels.prod = parse(board, base, entry); // single-channel format
            } else {
                if (entry.has("prod"))
                    channels.prod = parse(board, base, entry.getJSONObject("prod"));
                if (entry.has("beta"))
                    channels.beta = parse(board, base, entry.getJSONObject("beta"));
            }
            result.put(board, channels);
        }
        return result;
    }

    private static RemoteFirmware parse(String board, String base, JSONObject entry) throws Exception {
        return new RemoteFirmware(board, entry.getString("version"), base + entry.getString("file"),
            entry.getInt("size"), entry.getString("md5").toLowerCase());
    }

    /** Downloads and checks size, MD5, the embedded board and version tags. */
    FirmwareFile download() throws Exception {
        byte[] bytes = get(url, Protocol.MAX_IMAGE_SIZE + 1);
        if (bytes.length != size)
            throw new Exception("download incomplete (" + bytes.length + " of " + size + " bytes)");
        StringBuilder hex = new StringBuilder();
        for (byte b : OtaClient.md5(bytes))
            hex.append(String.format("%02x", b));
        if (!hex.toString().equals(md5))
            throw new Exception("download damaged (checksum mismatch)");
        String[] error = new String[1];
        FirmwareFile file = FirmwareFile.parse(bytes, "v" + version, error);
        if (file == null)
            throw new Exception(error[0]);
        if (!file.fitsBoard(board))
            throw new Exception("the published file is for " + Board.displayName(file.board) + ", not " + Board.displayName(board));
        return file;
    }

    private static byte[] get(String address, int maxBytes) throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
        connection.setConnectTimeout(10000);
        connection.setReadTimeout(20000);
        connection.setUseCaches(false);
        try {
            if (connection.getResponseCode() != 200)
                throw new Exception("server answered " + connection.getResponseCode());
            try (InputStream in = connection.getInputStream()) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                    if (out.size() > maxBytes)
                        throw new Exception("file too big");
                }
                return out.toByteArray();
            }
        } finally {
            connection.disconnect();
        }
    }
}
