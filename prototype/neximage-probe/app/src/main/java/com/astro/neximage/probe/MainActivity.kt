package com.astro.neximage.probe

import android.os.Bundle
import android.os.Process
import android.widget.FrameLayout
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import java.io.RandomAccessFile

class MainActivity : ComponentActivity() {

    private lateinit var usbManager: UsbCameraManager
    private var previewView: PreviewSurfaceView? = null
    private val usbStateHolder = mutableStateOf<UsbCameraManager.UsbState>(UsbCameraManager.UsbState.Idle)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeBridge.nativeInit()

        usbManager = UsbCameraManager(this) { state ->
            runOnUiThread { usbStateHolder.value = state }
        }

        setContent {
            var usbState by usbStateHolder
            NexImageProbeApp(
                usbManager = usbManager,
                usbState = usbState,
                onPreviewCreated = { view -> previewView = view },
                onRefreshPreview = { previewView?.requestRenderFrame() },
            )
        }

        usbManager.start()
        intent?.let {
            usbManager.findNexImage()?.let { device -> usbManager.requestPermission(device) }
        }
    }

    override fun onDestroy() {
        usbManager.stop()
        NativeBridge.nativeRelease()
        super.onDestroy()
    }
}

@Composable
fun NexImageProbeApp(
    usbManager: UsbCameraManager,
    usbState: UsbCameraManager.UsbState,
    onPreviewCreated: (PreviewSurfaceView) -> Unit,
    onRefreshPreview: () -> Unit,
) {
    var metrics by remember { mutableStateOf(MetricsSnapshot()) }
    var formats by remember { mutableStateOf("") }
    var guidAnalysis by remember { mutableStateOf("") }
    var selectedFormat by remember { mutableStateOf("Y800") }

    // State updates happen via recomposition triggered by LaunchedEffect polling below

    LaunchedEffect(Unit) {
        guidAnalysis = NativeBridge.nativeAnalyzePixelFormat("47524247-0000-1000-8000-00aa00389b71")
    }

    LaunchedEffect(usbState) {
        if (usbState is UsbCameraManager.UsbState.Connected) {
            formats = usbManager.enumerateFormats()
        }
    }

    LaunchedEffect(Unit) {
        while (isActive) {
            metrics = MetricsSnapshot.fromNative(NativeBridge.nativeGetMetrics())
            onRefreshPreview()
            delay(250)
        }
    }

    MaterialTheme(
        colorScheme = MaterialTheme.colorScheme.copy(
            background = Color(0xFF0D0D0D),
            surface = Color(0xFF1A1A1A),
            primary = Color(0xFFE94560),
            onBackground = Color(0xFFCCCCCC),
            onSurface = Color(0xFFCCCCCC),
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(Color(0xFF0D0D0D))
                .padding(12.dp),
        ) {
            Text(
                text = "NexImage Probe — Validation USB/UVC",
                style = MaterialTheme.typography.titleLarge,
                color = Color(0xFFE94560),
            )
            Text(
                text = stateLabel(usbState),
                color = Color(0xFFAAAAAA),
                modifier = Modifier.padding(vertical = 4.dp),
            )

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(280.dp)
                    .background(Color(0xFF111111)),
            ) {
                AndroidView(
                    factory = { context ->
                        PreviewSurfaceView(context).also { view ->
                            onPreviewCreated(view)
                            val params = FrameLayout.LayoutParams(
                                FrameLayout.LayoutParams.MATCH_PARENT,
                                FrameLayout.LayoutParams.MATCH_PARENT,
                            )
                            view.layoutParams = params
                        }
                    },
                    modifier = Modifier.fillMaxSize(),
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                FormatButton("Y800", selectedFormat) { selectedFormat = "Y800" }
                FormatButton("GRBG", selectedFormat) { selectedFormat = "GRBG" }
                FormatButton("GREY", selectedFormat) { selectedFormat = "GREY" }
            }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = {
                        usbManager.findNexImage()?.let { usbManager.requestPermission(it) }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF333355)),
                ) { Text("Scan USB") }

                Button(
                    onClick = {
                        usbManager.startStream(selectedFormat, 640, 480)
                    },
                    enabled = usbState is UsbCameraManager.UsbState.Connected ||
                        usbState is UsbCameraManager.UsbState.Streaming,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFE94560)),
                ) { Text("Start 640×480") }

                Button(
                    onClick = { usbManager.startStream(selectedFormat, 3872, 2764) },
                    enabled = usbState is UsbCameraManager.UsbState.Connected ||
                        usbState is UsbCameraManager.UsbState.Streaming,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF553333)),
                ) { Text("Full frame") }

                Button(
                    onClick = { usbManager.stopStream() },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF444444)),
                ) { Text("Stop") }
            }

            Spacer(modifier = Modifier.height(8.dp))

            MetricsPanel(metrics)

            Spacer(modifier = Modifier.height(8.dp))

            Column(
                modifier = Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState()),
            ) {
                InfoBlock("Formats UVC détectés", formats.ifBlank { "(connectez la caméra)" })
                InfoBlock("Analyse GUID 47524247…", guidAnalysis)
            }
        }
    }
}

@Composable
private fun FormatButton(label: String, selected: String, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        colors = ButtonDefaults.buttonColors(
            containerColor = if (label == selected) Color(0xFFE94560) else Color(0xFF2A2A2A),
        ),
    ) { Text(label) }
}

@Composable
private fun MetricsPanel(metrics: MetricsSnapshot) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF151515))
            .padding(12.dp),
    ) {
        Text("Métriques temps réel", color = Color(0xFFE94560))
        MetricRow("FPS", "%.1f".format(metrics.fps))
        MetricRow("Débit USB", "%.1f Mbps".format(metrics.usbMbps))
        MetricRow("Latence", "%.1f ms".format(metrics.latencyMs))
        MetricRow("Frames", "${metrics.totalFrames} (perdues: ${metrics.droppedFrames}, ${"%.2f".format(metrics.dropRate)}%)")
        MetricRow("CPU process", "%.1f %%".format(metrics.cpuPercent))
        MetricRow("Heap natif", "${metrics.nativeHeapKb} KB")
        MetricRow("Frame", "${metrics.frameWidth}×${metrics.frameHeight} ${metrics.fourcc}")
    }
}

@Composable
private fun MetricRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = Color(0xFF888888))
        Text(value, fontFamily = FontFamily.Monospace, color = Color(0xFFDDDDDD))
    }
}

@Composable
private fun InfoBlock(title: String, body: String) {
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        Text(title, color = Color(0xFFE94560))
        Text(body, fontFamily = FontFamily.Monospace, color = Color(0xFF999999), fontSize = MaterialTheme.typography.bodySmall.fontSize)
    }
}

private fun stateLabel(state: UsbCameraManager.UsbState): String = when (state) {
    UsbCameraManager.UsbState.Idle -> "En attente — branchez la NexImage 10 via USB-C OTG"
    UsbCameraManager.UsbState.WaitingPermission -> "Autorisation USB requise…"
    is UsbCameraManager.UsbState.Connected -> "Connecté: ${state.deviceName} (${state.vid.toString(16)}:${state.pid.toString(16)})"
    is UsbCameraManager.UsbState.Streaming -> "Streaming ${state.format}…"
    is UsbCameraManager.UsbState.Error -> "Erreur: ${state.message}"
}

/** Read process CPU usage from /proc/self/stat */
fun readProcessCpuPercent(): Double {
    return runCatching {
        val stat = RandomAccessFile("/proc/self/stat", "r").use { it.readLine() }
        val parts = stat.split(" ")
        val utime = parts[13].toLong()
        val stime = parts[14].toLong()
        val total = utime + stime
        val clockTicks = 100L
        (total.toDouble() / clockTicks / Process.myPid().coerceAtLeast(1)) * 100.0
    }.getOrDefault(0.0)
}
