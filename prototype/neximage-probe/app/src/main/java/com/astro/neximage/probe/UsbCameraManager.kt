package com.astro.neximage.probe

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log

class UsbCameraManager(
    private val context: Context,
    private val onStateChanged: (UsbState) -> Unit,
) {
    sealed class UsbState {
        data object Idle : UsbState()
        data object WaitingPermission : UsbState()
        data class Connected(val deviceName: String, val vid: Int, val pid: Int) : UsbState()
        data class Streaming(val format: String) : UsbState()
        data class Error(val message: String) : UsbState()
    }

    companion object {
        private const val TAG = "UsbCameraManager"
        private const val ACTION_USB_PERMISSION = "com.astro.neximage.probe.USB_PERMISSION"
        const val NEXIMAGE_VID = 0x199e
        const val NEXIMAGE_PID = 0x8619
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var connection: UsbDeviceConnection? = null
    private var currentDevice: UsbDevice? = null

    private val permissionIntent = PendingIntent.getBroadcast(
        context,
        0,
        Intent(ACTION_USB_PERMISSION),
        PendingIntent.FLAG_MUTABLE,
    )

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && device != null) {
                        openDevice(device)
                    } else {
                        onStateChanged(UsbState.Error("Permission USB refusée"))
                    }
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    findNexImage()?.let { requestPermission(it) }
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    stopAndClose()
                    onStateChanged(UsbState.Idle)
                }
            }
        }
    }

    fun start() {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(receiver, filter)
        }
        findNexImage()?.let { requestPermission(it) } ?: onStateChanged(UsbState.Idle)
    }

    fun stop() {
        runCatching { context.unregisterReceiver(receiver) }
        stopAndClose()
    }

    fun findNexImage(): UsbDevice? {
        return usbManager.deviceList.values.firstOrNull { device ->
            device.vendorId == NEXIMAGE_VID && device.productId == NEXIMAGE_PID
        } ?: usbManager.deviceList.values.firstOrNull { device ->
            device.deviceClass == UsbConstants.USB_CLASS_VIDEO ||
                device.interfaceCount.let { count ->
                    (0 until count).any { i ->
                        device.getInterface(i).interfaceClass == UsbConstants.USB_CLASS_VIDEO
                    }
                }
        }
    }

    fun requestPermission(device: UsbDevice) {
        if (usbManager.hasPermission(device)) {
            openDevice(device)
        } else {
            onStateChanged(UsbState.WaitingPermission)
            usbManager.requestPermission(device, permissionIntent)
        }
    }

    private fun openDevice(device: UsbDevice) {
        stopAndClose()
        val conn = usbManager.openDevice(device) ?: run {
            onStateChanged(UsbState.Error("Impossible d'ouvrir le périphérique USB"))
            return
        }
        connection = conn
        currentDevice = device

        val fd = conn.fileDescriptor
        val result = NativeBridge.nativeOpen(
            fd = fd,
            busNum = 0,
            devAddr = device.deviceId,
            vid = device.vendorId,
            pid = device.productId,
            preferredFormat = "Y800",
        )
        if (result != 0) {
            onStateChanged(UsbState.Error("nativeOpen failed: $result"))
            return
        }

        Log.i(TAG, "USB opened: ${device.deviceName} ${device.vendorId}:${device.productId} fd=$fd")
        onStateChanged(
            UsbState.Connected(
                deviceName = device.productName ?: device.deviceName,
                vid = device.vendorId,
                pid = device.productId,
            ),
        )
    }

    fun startStream(format: String = "Y800", width: Int = 640, height: Int = 480): Boolean {
        val result = NativeBridge.nativeStartStream(width, height, format)
        return if (result == 0) {
            onStateChanged(UsbState.Streaming(format))
            true
        } else {
            onStateChanged(UsbState.Error("nativeStartStream failed: $result (format=$format)"))
            false
        }
    }

    fun stopStream() {
        NativeBridge.nativeStopStream()
        currentDevice?.let {
            onStateChanged(UsbState.Connected(it.productName ?: it.deviceName, it.vendorId, it.productId))
        }
    }

    fun enumerateFormats(): String = NativeBridge.nativeEnumerateFormats()

    private fun stopAndClose() {
        NativeBridge.nativeStopStream()
        NativeBridge.nativeClose()
        connection?.close()
        connection = null
        currentDevice = null
    }
}
