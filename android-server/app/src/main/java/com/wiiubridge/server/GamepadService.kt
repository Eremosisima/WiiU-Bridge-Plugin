package com.wiiubridge.server

import android.annotation.SuppressLint
import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

/**
 * Service that reads Bluetooth gamepad input and forwards to MainActivity.
 * 
 * Android's InputDevice API automatically handles Bluetooth controllers
 * connected to the phone - no extra pairing code needed.
 */
class GamepadService : Service() {

    companion object {
        private const val TAG = "GamepadService"
        private const val SERVICE_NOTIFICATION_ID = 1
    }

    private var callback: ((Int, Float, Float, Float, Float) -> Unit)? = null

    // Controller state
    private var buttons = 0
    private var leftStickX = 0f
    private var leftStickY = 0f
    private var rightStickX = 0f
    private var rightStickY = 0f

    // Xbox 360 button masks (match our protocol)
    private const val BTN_A = 0x1000
    private const val BTN_B = 0x2000
    private const val BTN_X = 0x4000
    private const val BTN_Y = 0x8000
    private const val BTN_LB = 0x0100
    private const val BTN_RB = 0x0200
    private const val BTN_LT = 0x0040
    private const val BTN_RT = 0x0080
    private const val BTN_BACK = 0x0020
    private const val BTN_START = 0x0010
    private const val BTN_L3 = 0x0040
    private const val BTN_R3 = 0x0080
    private const val BTN_UP = 0x0001
    private const val BTN_DOWN = 0x0002
    private const val BTN_LEFT = 0x0004
    private const val BTN_RIGHT = 0x0008

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "GamepadService created")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    /**
     * Process gamepad input event from Android InputDevice API.
     * 
     * This is called by the InputManager when a Bluetooth controller
     * sends an event. Android handles all the BT protocol - we just
     * read the standardized input events.
     */
    fun processInputEvent(event: MotionEvent): Boolean {
        // Left stick (axes 0, 1)
        leftStickX = event.getAxisValue(MotionEvent.AXIS_X)
        leftStickY = event.getAxisValue(MotionEvent.AXIS_Y)

        // Right stick (axes 2, 14)
        rightStickX = event.getAxisValue(MotionEvent.AXIS_Z)
        rightStickY = event.getAxisValue(MotionEvent.AXIS_RZ)

        // Triggers (axes 3, 4) - normalize from -1..1 to 0..1
        val leftTrigger = (event.getAxisValue(MotionEvent.AXIS_LTRIGGER) + 1) / 2
        val rightTrigger = (event.getAxisValue(MotionEvent.AXIS_RTRIGGER) + 1) / 2

        // Update button state from motion event
        val newButtons = extractButtons(event)
        if (newButtons != buttons || leftStickX != 0f || leftStickY != 0f ||
            rightStickX != 0f || rightStickY != 0f) {
            buttons = newButtons
            callback?.invoke(buttons, leftStickX, leftStickY, rightStickX, rightStickY)
        }

        return true
    }

    private fun extractButtons(event: MotionEvent): Int {
        var btn = 0

        // D-Pad from hat
        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        if (hatX < -0.5f) btn = btn or BTN_LEFT
        if (hatX > 0.5f) btn = btn or BTN_RIGHT
        if (hatY < -0.5f) btn = btn or BTN_UP
        if (hatY > 0.5f) btn = btn or BTN_DOWN

        // Face buttons
        if (event.getButtonState() and MotionEvent.BUTTON_A != 0) btn = btn or BTN_A
        if (event.getButtonState() and MotionEvent.BUTTON_B != 0) btn = btn or BTN_B
        if (event.getButtonState() and MotionEvent.BUTTON_X != 0) btn = btn or BTN_X
        if (event.getButtonState() and MotionEvent.BUTTON_Y != 0) btn = btn or BTN_Y

        // Shoulders
        if (event.getButtonState() and MotionEvent.BUTTON_L1 != 0) btn = btn or BTN_LB
        if (event.getButtonState() and MotionEvent.BUTTON_R1 != 0) btn = btn or BTN_RB

        // Triggers as buttons (threshold 30%)
        if (event.getAxisValue(MotionEvent.AXIS_LTRIGGER) > 0.3f) btn = btn or BTN_LT
        if (event.getAxisValue(MotionEvent.AXIS_RTRIGGER) > 0.3f) btn = btn or BTN_RT

        // Thumbstick clicks
        if (event.getButtonState() and MotionEvent.BUTTON_THUMBL != 0) btn = btn or BTN_L3
        if (event.getButtonState() and MotionEvent.BUTTON_THUMBR != 0) btn = btn or BTN_R3

        // Start/Back
        if (event.getButtonState() and MotionEvent.BUTTON_START != 0) btn = btn or BTN_START
        if (event.getButtonState() and MotionEvent.BUTTON_SELECT != 0) btn = btn or BTN_BACK

        return btn
    }

    fun setCallback(cb: (Int, Float, Float, Float, Float) -> Unit) {
        callback = cb
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "GamepadService destroyed")
    }
}
