package astro.neximage.probe

import android.Manifest
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Debug
import android.net.Uri
import android.provider.Settings
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.concurrent.thread

class MainActivity : ComponentActivity() {
    private lateinit var usb: UsbManager
    private var connection: UsbDeviceConnection? = null
    private var preview: RawPreview? = null
    private var pollRunning = false
    private var latest = ByteArray(0)
    private var latestMeta = ""

    private var status by mutableStateOf("Branchez la NexImage 10 en USB-C.")
    private var formats by mutableStateOf(listOf<VideoFormat>())
    private var report by mutableStateOf(ProbeReport())
    private var streaming by mutableStateOf(false)
    private var exposure by mutableStateOf(333)
    private var gain by mutableStateOf(0)
    private var exposureRange by mutableStateOf(1..300000)
    private var gainRange by mutableStateOf(0..480)
    private var parsed: JSONObject? = null

    private val permissionAction = "astro.neximage.probe.USB_PERMISSION"
    private var pendingDevice: UsbDevice? = null

    private val requestCameraPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        val device = pendingDevice
        if (granted && device != null) {
            requestUsbPermission(device)
        } else if (!granted) {
            status = cameraPermissionDeniedMessage()
        }
    }

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != permissionAction) return
            val device = if (Build.VERSION.SDK_INT >= 33) {
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            }
            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && device != null) {
                pendingDevice = null
                open(device)
            } else {
                status = when {
                    !hasCameraPermission() -> cameraPermissionDeniedMessage()
                    else -> usbPermissionDeniedMessage()
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        usb = getSystemService(UsbManager::class.java)
        val filter = IntentFilter(permissionAction)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(permissionReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(permissionReceiver, filter)
        }
        setContent { ProbeScreen() }
        if (usb.deviceList.isNotEmpty()) {
            ensureCameraPermission(prompt = true)
        }
        handleUsbIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleUsbIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        val device = pendingDevice ?: return
        if (!hasCameraPermission() || usb.hasPermission(device)) return
        requestUsbPermission(device)
    }

    override fun onDestroy() {
        stopStream()
        unregisterReceiver(permissionReceiver)
        connection?.close()
        super.onDestroy()
    }

    @Composable
    private fun ProbeScreen() {
        val night = darkColorScheme(
            background = Color(0xFF070708),
            surface = Color(0xFF120C0C),
            primary = Color(0xFF8F2D2D),
            onPrimary = Color(0xFFF3E6DC),
            onBackground = Color(0xFFE6D2C0),
            onSurface = Color(0xFFE6D2C0),
        )
        MaterialTheme(colorScheme = night) {
            BoxWithConstraints(
                Modifier.fillMaxSize().background(Color(0xFF070708)).padding(12.dp),
            ) {
                val wide = maxWidth > 700.dp
                if (wide && streaming) {
                    Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        PreviewPane(Modifier.weight(1.4f).fillMaxHeight())
                        Controls(Modifier.weight(1f).verticalScroll(rememberScrollState()))
                    }
                } else {
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                        if (streaming) PreviewPane(Modifier.fillMaxWidth().height(280.dp))
                        Controls(Modifier.fillMaxWidth())
                    }
                }
            }
        }
    }

    @Composable
    private fun PreviewPane(modifier: Modifier) {
        AndroidView(
            modifier = modifier,
            factory = { ctx -> RawPreview(ctx).also { preview = it } },
        )
    }

    @Composable
    private fun Controls(modifier: Modifier) {
        Column(modifier.padding(4.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("NexImage Probe", color = Color(0xFFE6D2C0), style = MaterialTheme.typography.titleLarge)
            Text("v${BuildConfig.VERSION_NAME}", color = Color(0xFF9A8575))
            Text(status, color = Color(0xFFD8C4B0))
            Text(report.summary(), color = Color(0xFFC9B8A4))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NightButton("Chercher") { scan() }
                if (streaming) NightButton("Stop") { stopStream() }
            }
            if (!hasCameraPermission()) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    NightButton("Autoriser caméra") { ensureCameraPermission(prompt = true) }
                    NightButton("Paramètres app") { openAppSettings() }
                }
                Text(
                    "Sans v0.1.3+, la permission Caméra n'apparaît pas dans Paramètres. " +
                        "Utilisez le bouton ci-dessus ou réinstallez la dernière APK.",
                    color = Color(0xFFAA9484),
                )
            }
            formats.forEach { fmt ->
                NightButton("${fmt.fcc}  ${fmt.width}×${fmt.height}  ${fmt.bpp} bit  ${fmt.guid.take(8)}") {
                    startFormat(fmt)
                }
            }
            if (streaming) {
                Text("Exposition ${exposure} µs (unité caméra)")
                Slider(
                    value = exposure.toFloat(),
                    valueRange = exposureRange.first.toFloat()..exposureRange.last.toFloat(),
                    onValueChange = { exposure = it.toInt() },
                    onValueChangeFinished = { writeControl(camera = true, value = exposure) },
                )
                Text("Gain $gain")
                Slider(
                    value = gain.toFloat(),
                    valueRange = gainRange.first.toFloat()..gainRange.last.toFloat(),
                    onValueChange = { gain = it.toInt() },
                    onValueChangeFinished = { writeControl(camera = false, value = gain) },
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    NightButton("Motif ${phaseName(preview?.phase ?: 0)}") {
                        val view = preview ?: return@NightButton
                        view.phase = (view.phase + 1) and 3
                    }
                    NightButton("Sauver une trame") { saveFrame() }
                }
            }
        }
    }

    @Composable
    private fun NightButton(label: String, onClick: () -> Unit) {
        Button(
            onClick = onClick,
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3A1515), contentColor = Color(0xFFF3E6DC)),
        ) { Text(label) }
    }

    private fun handleUsbIntent(intent: Intent?) {
        val attached = if (Build.VERSION.SDK_INT >= 33) {
            intent?.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
        if (attached != null) {
            requestAccess(attached)
            return
        }
        scan()
    }

    private fun scan() {
        if (!ensureCameraPermission(prompt = true)) return
        val devices = usb.deviceList.values
        if (devices.isEmpty()) {
            status = "Aucun périphérique USB. OTG activé, câble SuperSpeed, VBUS 770 mA."
            return
        }
        val device = devices.firstOrNull { it.vendorId == 0x199e && it.productId == 0x8619 }
            ?: devices.first()
        requestAccess(device)
    }

    private fun requestAccess(device: UsbDevice) {
        pendingDevice = device
        status = "Trouvé ${device.deviceName} VID %04x PID %04x".format(device.vendorId, device.productId)
        if (!hasCameraPermission()) {
            status += "\nAutorisez Caméra (obligatoire pour UVC)…"
            requestCameraPermission.launch(Manifest.permission.CAMERA)
            return
        }
        if (usb.hasPermission(device)) {
            pendingDevice = null
            open(device)
            return
        }
        requestUsbPermission(device)
    }

    private fun requestUsbPermission(device: UsbDevice) {
        if (!hasCameraPermission()) {
            requestCameraPermission.launch(Manifest.permission.CAMERA)
            return
        }
        pendingDevice = device
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        val pending = PendingIntent.getBroadcast(
            this,
            device.deviceId,
            Intent(permissionAction).setPackage(packageName),
            flags,
        )
        usb.requestPermission(device, pending)
        status = "Trouvé ${device.deviceName}. Acceptez la fenêtre « Accès USB »…"
    }

    private fun hasCameraPermission(): Boolean {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED
    }

    private fun ensureCameraPermission(prompt: Boolean): Boolean {
        if (hasCameraPermission()) return true
        if (prompt) {
            status = "Autorisez Caméra dans la fenêtre système (obligatoire pour UVC)…"
            requestCameraPermission.launch(Manifest.permission.CAMERA)
        }
        return false
    }

    private fun openAppSettings() {
        startActivity(
            Intent(
                Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.fromParts("package", packageName, null),
            ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    private fun cameraPermissionDeniedMessage() = buildString {
        append(
            "Permission Caméra refusée. Android l'exige pour les caméras USB (UVC) " +
                "avant même d'afficher la demande USB.",
        )
        append("\nAppuyez sur « Autoriser caméra » dans l'app, ou réinstallez v0.1.4+.")
        append("\nSur une ancienne APK, Caméra n'apparaît pas dans Paramètres.")
    }

    private fun usbPermissionDeniedMessage() = buildString {
        append("Permission USB refusée. Appuyez sur Chercher et acceptez « Accès USB ».")
        append("\nSi rien n'apparaît : débranchez/rebranchez la caméra.")
        append("\nParamètres USB « Appareil connecté » = normal avec un adaptateur OTG.")
        append("\nVérifiez aussi Paramètres → Sécurité → Auto Blocker (désactivé pour USB).")
    }

    private fun open(device: UsbDevice) {
        connection?.close()
        val conn = usb.openDevice(device) ?: run {
            status = "openDevice a échoué."
            return
        }
        connection = conn
        val raw = conn.rawDescriptors
        val json = NativePump.parseConfig(raw, device.vendorId, device.productId)
        val root = JSONObject(json)
        parsed = root
        val list = mutableListOf<VideoFormat>()
        val arr = root.getJSONArray("formats")
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            list += VideoFormat(
                fcc = o.getString("fcc"),
                guid = o.getString("guid"),
                width = o.getInt("w"),
                height = o.getInt("h"),
                bpp = o.getInt("bpp"),
                phase = o.getInt("phase"),
                interval = o.getInt("interval"),
                formatIndex = o.getInt("format"),
                frameIndex = o.getInt("frame"),
                maxBytes = o.getInt("maxBytes"),
                kind = o.getInt("kind"),
            )
        }
        formats = list
        val power2 = root.optInt("usb2mA")
        val power3 = root.optInt("usb3mA")
        status = "Descripteurs lus. bMaxPower = $power2 mA si USB2, $power3 mA si USB3. " +
            "${list.size} format(s). La DFK 33UJ003 demande environ 770 mA."
    }

    private fun startFormat(fmt: VideoFormat) {
        val conn = connection ?: return
        val root = parsed ?: return
        val vs = root.optInt("vs", -1)
        if (vs < 0) {
            status = "Interface de streaming UVC absente."
            return
        }
        stopStream()
        val negotiated = negotiate(conn, vs, fmt) ?: run {
            status = "PROBE/COMMIT refusé pour ${fmt.fcc}."
            return
        }
        val alt = chooseAlt(root, negotiated.payload)
        if (alt == null) {
            status = "Aucun alternate setting vidéo."
            return
        }
        val expected = when {
            fmt.maxBytes > 0 -> fmt.maxBytes
            fmt.bpp >= 16 -> fmt.width * fmt.height * 2
            else -> fmt.width * fmt.height
        }
        val packet = if (alt.iso) alt.packet.coerceAtLeast(1) else negotiated.payload.coerceIn(512, 512 * 1024)
        val error = NativePump.start(
            fd = conn.fileDescriptor,
            interfaceNumber = vs,
            alt = alt.alt,
            endpoint = alt.addr,
            iso = if (alt.iso) 1 else 0,
            packetSize = packet,
            packetsPerUrb = 8,
            urbCount = 8,
            frameBytes = expected,
            width = fmt.width,
            height = fmt.height,
        )
        if (error.isNotEmpty()) {
            status = error
            return
        }
        preview?.phase = (fmt.phase - 1).coerceAtLeast(0)
        streaming = true
        readControlRanges(root)
        status = "Flux ${fmt.fcc} ${fmt.width}×${fmt.height} via ${if (alt.iso) "iso" else "bulk"} alt ${alt.alt}, paquet $packet."
        latestMeta = JSONObject()
            .put("fcc", fmt.fcc)
            .put("guid", fmt.guid)
            .put("width", fmt.width)
            .put("height", fmt.height)
            .put("bpp", fmt.bpp)
            .put("phase", fmt.phase)
            .put("bytes", expected)
            .toString()
        startPoll(fmt, expected)
    }

    private fun startPoll(fmt: VideoFormat, expected: Int) {
        pollRunning = true
        val buffer = ByteBuffer.allocateDirect(expected.coerceAtLeast(1)).order(ByteOrder.nativeOrder())
        val stats = LongArray(16)
        thread(name = "uvc-poll", isDaemon = true) {
            var windowNs = System.nanoTime()
            var framesThen = 0L
            var bytesThen = 0L
            var cpuThen = readSelfCpu()
            while (pollRunning) {
                val fresh = NativePump.poll(buffer, stats)
                val now = System.nanoTime()
                if (fresh) {
                    val len = stats[9].toInt()
                    if (len > 0) {
                        val copy = ByteArray(len)
                        buffer.position(0)
                        buffer.get(copy, 0, len)
                        buffer.position(0)
                        latest = copy
                        preview?.submit(buffer, fmt.width, fmt.height, minOf(len, fmt.width * fmt.height))
                    }
                }
                if (now - windowNs >= 1_000_000_000L) {
                    val seconds = (now - windowNs) / 1_000_000_000.0
                    val cpuNow = readSelfCpu()
                    val cpuDelta = (cpuNow - cpuThen) / seconds
                    report = ProbeReport(
                        fps = (stats[1] - framesThen) / seconds,
                        mbps = ((stats[4] - bytesThen) * 8.0) / seconds / 1_000_000.0,
                        framesOk = stats[1],
                        framesDropped = stats[2],
                        packets = stats[3],
                        errBits = stats[5],
                        badHeaders = stats[6],
                        speed = stats[7].toInt(),
                        pts = stats[8],
                        urbErrors = stats[10],
                        bayerMilli = stats[11],
                        mosaic = stats[12] != 0L,
                        lastLength = stats[13],
                        previewAgeMs = preview?.ageMs() ?: -1L,
                        cpuTicksPerSec = cpuDelta,
                        nativeMb = Debug.getNativeHeapAllocatedSize() / (1024.0 * 1024.0),
                        javaMb = (Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()) / (1024.0 * 1024.0),
                    )
                    windowNs = now
                    framesThen = stats[1]
                    bytesThen = stats[4]
                    cpuThen = cpuNow
                }
                Thread.sleep(2)
            }
        }
    }

    private fun stopStream() {
        pollRunning = false
        if (streaming) {
            NativePump.stop()
            streaming = false
        }
    }

    private fun saveFrame() {
        val bytes = latest
        if (bytes.isEmpty()) {
            status = "Pas encore de trame."
            return
        }
        val dir = getExternalFilesDir(null) ?: filesDir
        val stamp = System.currentTimeMillis()
        File(dir, "frame-$stamp.raw").writeBytes(bytes)
        File(dir, "frame-$stamp.json").writeText(latestMeta)
        status = "Trame sauvée (${bytes.size} octets) dans ${dir.absolutePath}"
    }

    private fun negotiate(conn: UsbDeviceConnection, vs: Int, fmt: VideoFormat): Negotiated? {
        for (size in intArrayOf(34, 26)) {
            val probe = ByteArray(size)
            probe[2] = fmt.formatIndex.toByte()
            probe[3] = fmt.frameIndex.toByte()
            putLe32(probe, 4, fmt.interval)
            val set = conn.controlTransfer(0x21, 0x01, 0x0100, vs, probe, size, 1000)
            if (set < 0) continue
            val got = ByteArray(size)
            val n = conn.controlTransfer(0xA1, 0x81, 0x0100, vs, got, size, 1000)
            if (n < 26) continue
            val commit = conn.controlTransfer(0x21, 0x01, 0x0200, vs, got, n, 1000)
            if (commit < 0) continue
            return Negotiated(le32(got, 18), le32(got, 22))
        }
        return null
    }

    private fun readControlRanges(root: JSONObject) {
        val conn = connection ?: return
        val vc = root.optInt("vc", 0)
        val camera = root.optInt("cameraTerminal")
        val processing = root.optInt("processingUnit")
        if (camera != 0) {
            exposureRange = readRange(conn, vc, camera, 0x04, 4) ?: exposureRange
            exposure = readCurrent(conn, vc, camera, 0x04, 4) ?: exposure
        }
        if (processing != 0) {
            gainRange = readRange(conn, vc, processing, 0x04, 2) ?: gainRange
            gain = readCurrent(conn, vc, processing, 0x04, 2) ?: gain
        }
    }

    private fun writeControl(camera: Boolean, value: Int) {
        val conn = connection ?: return
        val root = parsed ?: return
        val vc = root.optInt("vc", 0)
        val unit = if (camera) root.optInt("cameraTerminal") else root.optInt("processingUnit")
        if (unit == 0) return
        val len = if (camera) 4 else 2
        val buf = ByteArray(len)
        putLe32(buf, 0, value)
        conn.controlTransfer(0x21, 0x01, 0x0400, (unit shl 8) or vc, buf, len, 500)
    }

    private fun readRange(conn: UsbDeviceConnection, vc: Int, unit: Int, control: Int, len: Int): IntRange? {
        val min = readUvc(conn, 0x82, vc, unit, control, len) ?: return null
        val max = readUvc(conn, 0x83, vc, unit, control, len) ?: return null
        if (max <= min) return null
        return min..max
    }

    private fun readCurrent(conn: UsbDeviceConnection, vc: Int, unit: Int, control: Int, len: Int): Int? {
        return readUvc(conn, 0x81, vc, unit, control, len)
    }

    private fun readUvc(conn: UsbDeviceConnection, request: Int, vc: Int, unit: Int, control: Int, len: Int): Int? {
        val buf = ByteArray(len)
        val n = conn.controlTransfer(0xA1, request, control shl 8, (unit shl 8) or vc, buf, len, 500)
        if (n < len) return null
        return le32(buf, 0)
    }

    private fun chooseAlt(root: JSONObject, payload: Int): AltChoice? {
        val alts = root.getJSONArray("alts")
        var best: AltChoice? = null
        var bestBytes = Int.MAX_VALUE
        var largest: AltChoice? = null
        var largestBytes = -1
        for (i in 0 until alts.length()) {
            val o = alts.getJSONObject(i)
            if (o.optInt("hasEp") != 1) continue
            val choice = AltChoice(
                alt = o.getInt("alt"),
                addr = o.getInt("addr"),
                iso = o.getInt("iso") == 1,
                packet = o.getInt("packet"),
            )
            val bytes = o.getInt("packet") * o.optInt("transactions", 1) *
                (o.optInt("burst") + 1) * (o.optInt("mult") + 1)
            if (bytes > largestBytes) {
                largestBytes = bytes
                largest = choice
            }
            if (bytes >= payload && bytes < bestBytes) {
                bestBytes = bytes
                best = choice
            }
        }
        return best ?: largest
    }

    private fun readSelfCpu(): Double {
        return try {
            val text = File("/proc/self/stat").readText()
            val rest = text.substringAfter(") ").split(" ")
            val ticks = rest[11].toLong() + rest[12].toLong()
            ticks.toDouble()
        } catch (_: Exception) {
            0.0
        }
    }

    private fun phaseName(phase: Int) = arrayOf("GRBG", "GBRG", "RGGB", "BGGR")[phase and 3]
}

private data class VideoFormat(
    val fcc: String,
    val guid: String,
    val width: Int,
    val height: Int,
    val bpp: Int,
    val phase: Int,
    val interval: Int,
    val formatIndex: Int,
    val frameIndex: Int,
    val maxBytes: Int,
    val kind: Int,
)

private data class AltChoice(val alt: Int, val addr: Int, val iso: Boolean, val packet: Int)
private data class Negotiated(val frameBytes: Int, val payload: Int)

private data class ProbeReport(
    val fps: Double = 0.0,
    val mbps: Double = 0.0,
    val framesOk: Long = 0,
    val framesDropped: Long = 0,
    val packets: Long = 0,
    val errBits: Long = 0,
    val badHeaders: Long = 0,
    val speed: Int = -1,
    val pts: Long = 0,
    val urbErrors: Long = 0,
    val bayerMilli: Long = 0,
    val mosaic: Boolean = false,
    val lastLength: Long = 0,
    val previewAgeMs: Long = -1,
    val cpuTicksPerSec: Double = 0.0,
    val nativeMb: Double = 0.0,
    val javaMb: Double = 0.0,
) {
    fun summary(): String {
        val speedName = when (speed) {
            1 -> "Low"
            2 -> "Full"
            3 -> "High (USB2)"
            5 -> "Super (USB3)"
            6 -> "SuperPlus"
            else -> "vitesse $speed"
        }
        return (
            "fps %.1f   %.1f Mbit/s   $speedName\n" +
                "OK $framesOk   perdues $framesDropped   ERR $errBits   URB $urbErrors\n" +
                "paquets $packets   en-têtes $badHeaders   PTS $pts\n" +
                "preview ${previewAgeMs} ms   dernière trame $lastLength octets   mosaïque ${if (mosaic) "oui" else "non"} (×${bayerMilli / 1000.0})\n" +
                "CPU %.0f ticks/s   natif %.1f Mio   Java %.1f Mio"
            ).format(fps, mbps, cpuTicksPerSec, nativeMb, javaMb)
    }
}

private fun le32(b: ByteArray, o: Int): Int {
    var v = 0
    val n = minOf(4, b.size - o)
    for (i in 0 until n) v = v or ((b[o + i].toInt() and 0xff) shl (8 * i))
    return v
}

private fun putLe32(b: ByteArray, o: Int, v: Int) {
    for (i in 0 until minOf(4, b.size - o)) b[o + i] = (v shr (8 * i)).toByte()
}
