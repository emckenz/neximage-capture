package com.astro.neximage.probe

data class MetricsSnapshot(
    val fps: Double = 0.0,
    val usbMbps: Double = 0.0,
    val latencyMs: Double = 0.0,
    val droppedFrames: Long = 0,
    val totalFrames: Long = 0,
    val cpuPercent: Double = 0.0,
    val nativeHeapKb: Long = 0,
    val frameWidth: Int = 0,
    val frameHeight: Int = 0,
    val fourcc: String = "",
    val dropRate: Double = 0.0,
) {
    companion object {
        fun fromNative(raw: DoubleArray): MetricsSnapshot {
            if (raw.size < 11) return MetricsSnapshot()
            val total = raw[4].toLong()
            val dropped = raw[3].toLong()
            val dropRate = if (total > 0) (dropped.toDouble() / total) * 100.0 else 0.0
            return MetricsSnapshot(
                fps = raw[0],
                usbMbps = raw[1],
                latencyMs = raw[2],
                droppedFrames = dropped,
                totalFrames = total,
                cpuPercent = raw[5],
                nativeHeapKb = raw[6].toLong(),
                frameWidth = raw[7].toInt(),
                frameHeight = raw[8].toInt(),
                fourcc = fourccFromCode(raw[9].toInt()),
                dropRate = dropRate,
            )
        }

        private fun fourccFromCode(code: Int): String {
            val bytes = byteArrayOf(
                (code and 0xFF).toByte(),
                ((code shr 8) and 0xFF).toByte(),
                ((code shr 16) and 0xFF).toByte(),
                ((code shr 24) and 0xFF).toByte(),
            )
            return bytes.toString(Charsets.US_ASCII).trim('\u0000')
        }
    }
}
