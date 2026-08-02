package com.ggw.sysbar

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * Simple control screen: grant the overlay permission, start/stop the top bar, and show the
 * install-time RAM estimate (this app's current memory footprint).
 */
class MainActivity : AppCompatActivity() {

    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        status = findViewById(R.id.status)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), 42)
        }

        findViewById<Button>(R.id.grant).setOnClickListener {
            if (!canOverlay()) {
                startActivity(
                    Intent(
                        Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:$packageName")
                    )
                )
            }
        }
        findViewById<Button>(R.id.start).setOnClickListener {
            if (canOverlay()) {
                val i = Intent(this, OverlayService::class.java)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(i)
                else startService(i)
            } else {
                status.text = getString(R.string.need_overlay)
            }
        }
        findViewById<Button>(R.id.stop).setOnClickListener {
            stopService(Intent(this, OverlayService::class.java))
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun canOverlay(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(this)

    private fun refresh() {
        val ram = SysStats.ram(this)
        val rom = SysStats.rom()
        val appMb = SysStats.appMemoryMb(this)
        status.text = buildString {
            append("Overlay permission: ").append(if (canOverlay()) "granted" else "NOT granted").append('\n')
            append("RAM: ${ram.usedMb} / ${ram.totalMb} MB (${ram.percent}%)\n")
            append("ROM: ${"%.1f".format(rom.usedGb)} / ${"%.1f".format(rom.totalGb)} GB (${rom.percent}%)\n")
            append("Install-time RAM estimate (this app's footprint now): ${appMb} MB")
        }
    }
}
