package com.wiiubridge.server

import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * WiiU Bridge Server for Android
 * 
 * Receives input from Bluetooth controllers connected to the phone
 * and sends it to the Wii U plugin via UDP.
 * 
 * Protocol (20 bytes):
 * [Magic: 4B][Version: 4B][LX: 2B][LY: 2B][RX: 2B][RY: 2B][Buttons: 4B]
 */
class MainActivity : Activity() {

    companion object {
        private const val TAG = "WiiUBridge"
        private const val PROTOCOL_MAGIC = 0x57425244 // "WBRD"
        private const val PROTOCOL_VERSION = 1
        private const val PACKET_SIZE = 20
        private const val DEFAULT_PORT = 12345
        private const val UPDATE_INTERVAL_MS = 16L // ~60Hz
    }

    // UI
    private lateinit var tvStatus: TextView
    private lateinit var tvController: TextView
    private lateinit var tvWiiU: TextView
    private lateinit var btnConnect: Button
    private lateinit var btnDisconnect: Button

    // Bluetooth
    private var bluetoothAdapter: BluetoothAdapter? = null
    private var connectedDevice: BluetoothDevice? = null

    // Network
    private var udpSocket: DatagramSocket? = null
    private var wiiuAddress: InetAddress? = null
    private var wiiuPort: Int = DEFAULT_PORT
    private var isSending = false

    // Controller state
    private var buttons: Int = 0
    private var leftStickX: Short = 0
    private var leftStickY: Short = 0
    private var rightStickX: Short = 0
    private var rightStickY: Short = 0

    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        tvStatus = findViewById(R.id.tvStatus)
        tvController = findViewById(R.id.tvController)
        tvWiiU = findViewById(R.id.tvWiiU)
        btnConnect = findViewById(R.id.btnConnect)
        btnDisconnect = findViewById(R.id.btnDisconnect)

        bluetoothAdapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

        btnConnect.setOnClickListener { startBridge() }
        btnDisconnect.setOnClickListener { stopBridge() }

        updateUI()
    }

    private fun startBridge() {
        val wiiuIp = tvWiiU.text.toString().trim()
        if (wiiuIp.isEmpty()) {
            Toast.makeText(this, "Enter Wii U IP address", Toast.LENGTH_SHORT).show()
            return
        }

        try {
            wiiuAddress = InetAddress.getByName(wiiuIp)
        } catch (e: Exception) {
            Toast.makeText(this, "Invalid IP address", Toast.LENGTH_SHORT).show()
            return
        }

        // Start UDP socket
        try {
            udpSocket = DatagramSocket()
            isSending = true
            startSending()
            updateUI()
            Toast.makeText(this, "Bridge started", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create socket", e)
            Toast.makeText(this, "Failed to start: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopBridge() {
        isSending = false
        udpSocket?.close()
        udpSocket = null
        updateUI()
    }

    private fun startSending() {
        Thread {
            while (isSending) {
                try {
                    val packet = createPacket()
                    val datagramPacket = DatagramPacket(
                        packet, packet.size,
                        wiiuAddress, wiiuPort
                    )
                    udpSocket?.send(datagramPacket)
                } catch (e: Exception) {
                    Log.e(TAG, "Send failed", e)
                }
                Thread.sleep(UPDATE_INTERVAL_MS)
            }
        }.start()
    }

    private fun createPacket(): ByteArray {
        val buffer = ByteBuffer.allocate(PACKET_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(PROTOCOL_MAGIC)
        buffer.putInt(PROTOCOL_VERSION)
        buffer.putShort(leftStickX)
        buffer.putShort(leftStickY)
        buffer.putShort(rightStickX)
        buffer.putShort(rightStickY)
        buffer.putInt(buttons)
        return buffer.array()
    }

    /**
     * Called by the gamepad service when controller state changes
     */
    fun onControllerInput(
        newButtons: Int,
        newLX: Float, newLY: Float,
        newRX: Float, newRY: Float
    ) {
        buttons = newButtons
        leftStickX = (newLX * 2047).toInt().toShort()
        leftStickY = (newLY * 2047).toInt().toShort()
        rightStickX = (newRX * 2047).toInt().toShort()
        rightStickY = (newRY * 2047).toInt().toShort()

        handler.post { updateUI() }
    }

    private fun updateUI() {
        tvStatus.text = if (isSending) "Connected" else "Disconnected"
        tvStatus.setTextColor(if (isSending) 0xFF00FF00.toInt() else 0xFFFF0000.toInt())

        val btnNames = mutableListOf<String>()
        if (buttons and 0x1000 != 0) btnNames.add("A")
        if (buttons and 0x2000 != 0) btnNames.add("B")
        if (buttons and 0x4000 != 0) btnNames.add("X")
        if (buttons and 0x8000 != 0) btnNames.add("Y")
        if (buttons and 0x0001 != 0) btnNames.add("↑")
        if (buttons and 0x0002 != 0) btnNames.add("↓")
        if (buttons and 0x0004 != 0) btnNames.add("←")
        if (buttons and 0x0008 != 0) btnNames.add("→")
        if (buttons and 0x0100 != 0) btnNames.add("LB")
        if (buttons and 0x0200 != 0) btnNames.add("RB")
        if (buttons and 0x0040 != 0) btnNames.add("LT")
        if (buttons and 0x0080 != 0) btnNames.add("RT")

        tvController.text = "Buttons: ${btnNames.joinToString(", ")}\n" +
                "LStick: ($leftStickX, $leftStickY)\n" +
                "RStick: ($rightStickX, $rightStickY)"
    }

    override fun onDestroy() {
        super.onDestroy()
        stopBridge()
    }
}
