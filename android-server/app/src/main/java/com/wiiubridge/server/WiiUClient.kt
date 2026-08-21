package com.wiiubridge.server

import android.util.Log
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * UDP Client that sends controller data to Wii U.
 * 
 * UDP is chosen over TCP because:
 * - Lower latency (no handshake)
 * - Tolerant of packet loss (next update replaces old state)
 * - Better for real-time input (60Hz updates)
 * 
 * The Wii U plugin listens on the same port and receives these packets.
 */
class WiiUClient(
    private var targetAddress: InetAddress,
    private var targetPort: Int = 12345
) {
    companion object {
        private const val TAG = "WiiUClient"
        private const val MAGIC = 0x57425244 // "WBRD"
        private const val VERSION = 1
        private const val PACKET_SIZE = 20
    }

    private var socket: DatagramSocket? = null
    private var isRunning = false

    /**
     * Create packet from controller state.
     * Format: [Magic: 4B][Version: 4B][LX: 2B][LY: 2B][RX: 2B][RY: 2B][Buttons: 4B]
     */
    fun createPacket(
        buttons: Int,
        leftX: Float, leftY: Float,
        rightX: Float, rightY: Float
    ): ByteArray {
        val buffer = ByteBuffer.allocate(PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(MAGIC)
        buffer.putInt(VERSION)
        buffer.putShort((leftX * 2047).toInt().coerceIn(-2048, 2047).toShort())
        buffer.putShort((leftY * 2047).toInt().coerceIn(-2048, 2047).toShort())
        buffer.putShort((rightX * 2047).toInt().coerceIn(-2048, 2047).toShort())
        buffer.putShort((rightY * 2047).toInt().coerceIn(-2048, 2047).toShort())
        buffer.putInt(buttons)
        return buffer.array()
    }

    /**
     * Send a single packet to Wii U.
     */
    fun sendPacket(
        buttons: Int,
        leftX: Float, leftY: Float,
        rightX: Float, rightY: Float
    ): Boolean {
        return try {
            val packet = createPacket(buttons, leftX, leftY, rightX, rightY)
            val datagramPacket = DatagramPacket(
                packet, packet.size,
                targetAddress, targetPort
            )
            socket?.send(datagramPacket)
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to send packet", e)
            false
        }
    }

    /**
     * Start continuous sending at specified rate.
     */
    fun startContinuousSend(
        getState: () -> Pair<Int, FloatArray>,
        rateHz: Int = 60
    ) {
        isRunning = true
        val intervalMs = 1000L / rateHz

        Thread {
            socket = DatagramSocket()
            Log.d(TAG, "UDP client started, target: $targetAddress:$targetPort")

            while (isRunning) {
                val (btns, sticks) = getState()
                sendPacket(btns, sticks[0], sticks[1], sticks[2], sticks[3])
                Thread.sleep(intervalMs)
            }
        }.start()
    }

    /**
     * Stop sending.
     */
    fun stop() {
        isRunning = false
        socket?.close()
        socket = null
    }

    /**
     * Update target address/port.
     */
    fun setTarget(address: InetAddress, port: Int) {
        targetAddress = address
        targetPort = port
    }
}
