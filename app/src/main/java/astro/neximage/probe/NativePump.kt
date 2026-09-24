package astro.neximage.probe

import java.nio.ByteBuffer

object NativePump {
    init {
        System.loadLibrary("neximage")
    }

    external fun parseConfig(raw: ByteArray, vendor: Int, product: Int): String

    external fun start(
        fd: Int,
        interfaceNumber: Int,
        alt: Int,
        endpoint: Int,
        iso: Int,
        packetSize: Int,
        packetsPerUrb: Int,
        urbCount: Int,
        frameBytes: Int,
        width: Int,
        height: Int,
    ): String

    external fun stop()

    external fun poll(buffer: ByteBuffer, stats: LongArray): Boolean
}
