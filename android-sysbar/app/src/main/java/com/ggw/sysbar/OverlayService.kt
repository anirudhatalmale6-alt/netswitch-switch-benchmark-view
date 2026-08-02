package com.ggw.sysbar

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.WindowManager
import android.widget.TextView

/**
 * Foreground service that draws a tiny always-on-top bar with RAM / ROM / CPU%, refreshed once a
 * second, on top of whatever app is in the foreground. Uses TYPE_APPLICATION_OVERLAY on API 26+
 * and TYPE_PHONE below that so it works on older versions too.
 */
class OverlayService : Service() {

    private lateinit var wm: WindowManager
    private var view: TextView? = null
    private val handler = Handler(Looper.getMainLooper())
    private val cpu = SysStats.CpuSampler()

    private val tick = object : Runnable {
        override fun run() {
            update()
            handler.postDelayed(this, 1000L)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification())
        wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        addOverlay()
        handler.post(tick)
    }

    private fun addOverlay() {
        if (view != null) return
        val tv = TextView(this).apply {
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.argb(150, 0, 0, 0)) // semi-transparent so it's readable anywhere
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 9f)   // small font per the request
            setPadding(12, 2, 12, 2)
            text = "SysBar starting…"
        }
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else
            @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE

        val lp = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT
        )
        lp.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        lp.y = 0
        try {
            wm.addView(tv, lp)
            view = tv
        } catch (e: Exception) {
            // Overlay permission not granted — stop gracefully.
            stopSelf()
        }
    }

    private fun update() {
        val v = view ?: return
        val ram = SysStats.ram(this)
        val rom = SysStats.rom()
        val (cpuPct, src) = cpu.sample()
        v.text = "RAM ${ram.usedMb}/${ram.totalMb}MB ${ram.percent}%  " +
            "ROM ${"%.1f".format(rom.usedGb)}/${"%.1f".format(rom.totalGb)}GB  " +
            "CPU ${cpuPct}%($src)"
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacks(tick)
        view?.let { runCatching { wm.removeView(it) } }
        view = null
    }

    private fun buildNotification(): Notification {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            val ch = NotificationChannel(CHANNEL, "SysBar overlay", NotificationManager.IMPORTANCE_MIN)
            ch.setShowBadge(false)
            nm.createNotificationChannel(ch)
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            Notification.Builder(this, CHANNEL) else @Suppress("DEPRECATION") Notification.Builder(this)
        return builder
            .setContentTitle("SysBar running")
            .setContentText("RAM / ROM / CPU on top bar")
            .setSmallIcon(R.drawable.ic_bar)
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL = "sysbar"
        private const val NOTIF_ID = 1001
    }
}
