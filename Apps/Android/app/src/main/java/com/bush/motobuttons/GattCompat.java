package com.bush.motobuttons;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.os.Build;

/** GATT writes across the Android 13 API change. */
@SuppressLint("MissingPermission")
final class GattCompat {
    @SuppressWarnings("deprecation")
    static boolean write(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, byte[] value) {
        int type = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
        if (Build.VERSION.SDK_INT >= 33)
            return gatt.writeCharacteristic(characteristic, value, type) == BluetoothGatt.GATT_SUCCESS;
        characteristic.setWriteType(type);
        characteristic.setValue(value);
        return gatt.writeCharacteristic(characteristic);
    }

    @SuppressWarnings("deprecation")
    static boolean enableNotifications(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
        BluetoothGattDescriptor cccd = characteristic.getDescriptor(Protocol.CCCD);
        if (cccd == null || !gatt.setCharacteristicNotification(characteristic, true))
            return false;
        byte[] enable = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE;
        if (Build.VERSION.SDK_INT >= 33)
            return gatt.writeDescriptor(cccd, enable) == BluetoothGatt.GATT_SUCCESS;
        cccd.setValue(enable);
        return gatt.writeDescriptor(cccd);
    }

    private GattCompat() {
    }
}
