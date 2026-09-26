package com.bush.motobuttons;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * A controller firmware file: an ESP32 app image (.bin) or an nRF52840 DFU
 * package (.zip), with its validity check and embedded version and board tags.
 */
final class FirmwareFile {
    // config.h: FIRMWARE_VERSION_TAG = "MBFWVER=<version>", BOARD_ID_TAG = "MBBOARD=<board>"
    private static final String VERSION_TAG = "MBFWVER=";
    private static final String BOARD_TAG = "MBBOARD=";

    final byte[] bytes;
    final String name;
    final String version; // null when the image has no tag
    final String board;   // null when the image has no tag (before 2.4.0)
    final boolean dfuPackage; // nRF52840 .zip for Nordic DFU

    private FirmwareFile(byte[] bytes, String name, String version, String board, boolean dfuPackage) {
        this.bytes = bytes;
        this.name = name;
        this.version = version;
        this.board = board;
        this.dfuPackage = dfuPackage;
    }

    /** Returns null and sets error[0] when the bytes are not a usable image. */
    static FirmwareFile parse(byte[] bytes, String name, String[] error) {
        if (bytes.length > 4 && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 3 && bytes[3] == 4)
            return parseDfuPackage(bytes, name, error);
        if (bytes.length == 0 || (bytes[0] & 0xFF) != Protocol.IMAGE_MAGIC) {
            error[0] = name + " is not a controller firmware file.";
            return null;
        }
        if (bytes.length > Protocol.MAX_IMAGE_SIZE) {
            error[0] = name + " is too big. Use the app .bin, not the merged one.";
            return null;
        }
        return new FirmwareFile(bytes, name, findTag(bytes, VERSION_TAG), findTag(bytes, BOARD_TAG), false);
    }

    /** A DFU package holds manifest.json, the application .bin and its .dat init packet. */
    private static FirmwareFile parseDfuPackage(byte[] bytes, String name, String[] error) {
        byte[] app = null;
        boolean manifest = false;
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(bytes))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entry.getName().equals("manifest.json"))
                    manifest = true;
                else if (entry.getName().endsWith(".bin"))
                    app = readEntry(zip);
            }
        } catch (Exception e) {
            error[0] = name + " is not a readable .zip file.";
            return null;
        }
        String board = app == null ? null : findTag(app, BOARD_TAG);
        if (!manifest || board == null) {
            error[0] = name + " is not a controller firmware package.";
            return null;
        }
        return new FirmwareFile(bytes, name, findTag(app, VERSION_TAG), board, true);
    }

    private static byte[] readEntry(ZipInputStream zip) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = zip.read(buffer)) > 0) {
            out.write(buffer, 0, n);
            if (out.size() > Protocol.MAX_IMAGE_SIZE)
                throw new Exception("too big");
        }
        return out.toByteArray();
    }

    /** True when this image may go to a controller of that board. */
    boolean fitsBoard(String controllerBoard) {
        return board == null ? Board.ESP32C3_OLED.equals(controllerBoard) : board.equals(controllerBoard);
    }

    static String findTag(byte[] bytes, String prefix) {
        byte[] tag = prefix.getBytes(StandardCharsets.US_ASCII);
        outer:
        for (int i = 0; i + tag.length < bytes.length; i++) {
            for (int j = 0; j < tag.length; j++) {
                if (bytes[i + j] != tag[j])
                    continue outer;
            }
            int start = i + tag.length;
            int end = start;
            while (end < bytes.length && end - start < 24 && (Character.isLetterOrDigit(bytes[end]) || bytes[end] == '.' || bytes[end] == '-'))
                end++;
            return end > start ? new String(bytes, start, end - start, StandardCharsets.US_ASCII) : null;
        }
        return null;
    }

    /**
     * Negative when a is older than b. Dotted numbers (2.2.10 > 2.2.9); a
     * pre-release is older than its release (2.4.1-beta.2 < 2.4.1) and
     * betas compare by their number (beta.2 > beta.1).
     */
    static int compareVersions(String a, String b) {
        String[] sa = a.split("-", 2);
        String[] sb = b.split("-", 2);
        int core = compareDotted(sa[0], sb[0]);
        if (core != 0)
            return core;
        if (sa.length == 1 || sb.length == 1)
            return Integer.compare(sb.length, sa.length); // the release beats its pre-release
        return compareDotted(sa[1].replaceAll("[^0-9.]", ""), sb[1].replaceAll("[^0-9.]", ""));
    }

    private static int compareDotted(String a, String b) {
        String[] pa = a.split("\\.");
        String[] pb = b.split("\\.");
        for (int i = 0; i < Math.max(pa.length, pb.length); i++) {
            int va = i < pa.length ? number(pa[i]) : 0;
            int vb = i < pb.length ? number(pb[i]) : 0;
            if (va != vb)
                return Integer.compare(va, vb);
        }
        return 0;
    }

    private static int number(String part) {
        int value = 0;
        for (char c : part.toCharArray()) {
            if (!Character.isDigit(c))
                break;
            value = value * 10 + (c - '0');
        }
        return value;
    }
}
