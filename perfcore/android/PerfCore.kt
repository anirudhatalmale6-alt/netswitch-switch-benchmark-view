package com.ai2orbit.perfcore

/**
 * Kotlin surface for the native perfcore engine (the CPU-DRAMM performance model).
 * The heavy work runs in native C++ (libperfcore.so) — the same engine file the iOS
 * app uses — so the numbers are the real silicon, measured beneath the runtime, and
 * comparable between Android and iOS.
 *
 * Usage:
 *   val pc = PerfCore()
 *   val gbps = pc.drammGbps(8)          // DRAMM priority-table bandwidth
 *   val headroom = pc.frictionHeadroom() // x gained by removing bus friction
 *   val watts = pc.powerWatts(5.0, 3.0)  // P = V*I
 *   val peak = pc.peakOpsPerSec()        // max throughput reached
 */
class PerfCore {
    external fun drammGbps(repeat: Int): Double
    external fun frictionMops(level: Double): Double
    external fun frictionHeadroom(): Double
    external fun powerWatts(volts: Double, amps: Double): Double
    external fun peakOpsPerSec(): Double
    external fun selfTest(): Int

    companion object {
        init { System.loadLibrary("perfcore") }
    }
}
