package com.ggw.sysbar

import android.app.ActivityManager
import android.content.Context
import android.os.Environment
import android.os.Process
import android.os.StatFs
import android.os.SystemClock
import java.io.RandomAccessFile

/**
 * All device metrics in one place, no external libraries.
 *
 *  - RAM: ActivityManager.MemoryInfo (used / total, %)
 *  - ROM: StatFs on the internal data partition (used / total)
 *  - CPU%: reads /proc/stat when the OS allows it (system-wide); otherwise falls back to this
 *          process's own CPU time from /proc/self/stat. Android 8+ often restricts /proc/stat to
 *          apps, so the value is labelled honestly in the overlay ("sys" vs "app").
 */
object SysStats {

    data class Ram(val usedMb: Long, val totalMb: Long, val percent: Int)
    data class Rom(val usedGb: Double, val totalGb: Double, val percent: Int)

    fun ram(ctx: Context): Ram {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mi = ActivityManager.MemoryInfo()
        am.getMemoryInfo(mi)
        val total = mi.totalMem
        val used = total - mi.availMem
        val pct = if (total > 0) ((used * 100) / total).toInt() else 0
        return Ram(used / (1024 * 1024), total / (1024 * 1024), pct)
    }

    fun rom(): Rom {
        val sf = StatFs(Environment.getDataDirectory().path)
        val total = sf.blockCountLong * sf.blockSizeLong
        val free = sf.availableBlocksLong * sf.blockSizeLong
        val used = total - free
        val gb = 1024.0 * 1024.0 * 1024.0
        val pct = if (total > 0) ((used * 100) / total).toInt() else 0
        return Rom(used / gb, total / gb, pct)
    }

    /** This app's current memory footprint in MB — the "install-time RAM estimate". */
    fun appMemoryMb(ctx: Context): Int {
        val am = ctx.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val infos = am.getProcessMemoryInfo(intArrayOf(Process.myPid()))
        val pssKb = if (infos.isNotEmpty()) infos[0].totalPss else 0
        return pssKb / 1024
    }

    // ---- CPU sampler: keeps the previous snapshot so a delta can be taken each tick ----
    class CpuSampler {
        private var prevBusy = 0L
        private var prevTotal = 0L
        private var prevProc = 0L
        private var prevElapsed = 0L
        private val cores = Runtime.getRuntime().availableProcessors().coerceAtLeast(1)

        /** Returns Pair(percent, source) where source is "sys" or "app". */
        fun sample(): Pair<Int, String> {
            readSystem()?.let { return it }
            return readProcess()
        }

        private fun readSystem(): Pair<Int, String>? {
            return try {
                RandomAccessFile("/proc/stat", "r").use { raf ->
                    val line = raf.readLine() ?: return null
                    if (!line.startsWith("cpu ")) return null
                    val p = line.trim().split(Regex("\\s+"))
                    // cpu user nice system idle iowait irq softirq steal ...
                    val nums = p.drop(1).mapNotNull { it.toLongOrNull() }
                    if (nums.size < 4) return null
                    val idle = nums[3] + (if (nums.size > 4) nums[4] else 0L)
                    val total = nums.sum()
                    val busy = total - idle
                    val dBusy = busy - prevBusy
                    val dTotal = total - prevTotal
                    prevBusy = busy; prevTotal = total
                    if (dTotal <= 0L) return 0 to "sys"
                    ((dBusy * 100) / dTotal).toInt().coerceIn(0, 100) to "sys"
                }
            } catch (e: Exception) {
                null
            }
        }

        private fun readProcess(): Pair<Int, String> {
            return try {
                RandomAccessFile("/proc/self/stat", "r").use { raf ->
                    val parts = (raf.readLine() ?: "").split(" ")
                    // fields 14 (utime) and 15 (stime) are 1-indexed in proc(5)
                    val utime = parts.getOrNull(13)?.toLongOrNull() ?: 0L
                    val stime = parts.getOrNull(14)?.toLongOrNull() ?: 0L
                    val proc = utime + stime
                    val now = SystemClock.elapsedRealtime()
                    val hz = 100L // typical CLK_TCK on Android
                    val dProc = proc - prevProc
                    val dMs = now - prevElapsed
                    prevProc = proc; prevElapsed = now
                    if (dMs <= 0L) return 0 to "app"
                    val pct = (dProc * 1000L * 100L) / (hz * dMs * cores)
                    pct.toInt().coerceIn(0, 100) to "app"
                }
            } catch (e: Exception) {
                0 to "app"
            }
        }
    }
}
