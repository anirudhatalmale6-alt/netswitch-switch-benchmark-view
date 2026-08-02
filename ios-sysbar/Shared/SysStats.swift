import Foundation

// Honest system readers for iOS. Everything here uses public APIs only.
// Notes on iOS platform limits (same spirit as the Android build's honesty tags):
//  - Device-wide CPU% is NOT available to a sandboxed app without special
//    entitlements. We report THIS APP's CPU% and tag it (app) — nothing faked.
//  - System-wide "free RAM" is not reliably exposed; we report the device total
//    and the memory available to this app, both public and truthful.
struct SysSnapshot {
    var ramTotalMB: Double = 0     // device physical RAM
    var ramAppAvailMB: Double = 0  // memory this app may still allocate
    var appFootprintMB: Double = 0 // this app's resident footprint (install-time RAM estimate)
    var romTotalGB: Double = 0
    var romFreeGB: Double = 0
    var cpuAppPct: Double = 0      // this app's CPU across its threads
}

enum SysStats {

    static func snapshot() -> SysSnapshot {
        var s = SysSnapshot()
        s.ramTotalMB = Double(ProcessInfo.processInfo.physicalMemory) / (1024 * 1024)
        s.ramAppAvailMB = appAvailableMemoryMB()
        s.appFootprintMB = appFootprintMB()
        let (t, f) = romGB()
        s.romTotalGB = t
        s.romFreeGB = f
        s.cpuAppPct = appCpuPercent()
        return s
    }

    // Memory still available to this app (iOS 13+). 0 if the API is unavailable.
    static func appAvailableMemoryMB() -> Double {
        if #available(iOS 13.0, *) {
            return Double(os_proc_available_memory()) / (1024 * 1024)
        }
        return 0
    }

    // This app's physical footprint via TASK_VM_INFO.phys_footprint.
    static func appFootprintMB() -> Double {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size /
                                           MemoryLayout<natural_t>.size)
        let kr = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        guard kr == KERN_SUCCESS else { return 0 }
        return Double(info.phys_footprint) / (1024 * 1024)
    }

    // Internal storage total/free in GB (important-usage free is what iOS shows users).
    static func romGB() -> (Double, Double) {
        let url = URL(fileURLWithPath: NSHomeDirectory())
        let keys: Set<URLResourceKey> = [.volumeTotalCapacityKey,
                                         .volumeAvailableCapacityForImportantUsageKey]
        guard let v = try? url.resourceValues(forKeys: keys) else { return (0, 0) }
        let total = Double(v.volumeTotalCapacity ?? 0) / 1e9
        let free = Double(v.volumeAvailableCapacityForImportantUsage ?? 0) / 1e9
        return (total, free)
    }

    // This app's CPU% summed across its live threads (0..100 per core-second).
    static func appCpuPercent() -> Double {
        var threads: thread_act_array_t?
        var count: mach_msg_type_number_t = 0
        guard task_threads(mach_task_self_, &threads, &count) == KERN_SUCCESS,
              let threads = threads else { return 0 }
        defer {
            vm_deallocate(mach_task_self_, vm_address_t(UInt(bitPattern: threads)),
                          vm_size_t(Int(count) * MemoryLayout<thread_t>.stride))
        }
        var total: Double = 0
        for i in 0..<Int(count) {
            var ti = thread_basic_info()
            // THREAD_BASIC_INFO_COUNT is a C macro Swift doesn't import; derive it.
            var tc = mach_msg_type_number_t(MemoryLayout<thread_basic_info>.size /
                                            MemoryLayout<integer_t>.size)
            let kr = withUnsafeMutablePointer(to: &ti) {
                $0.withMemoryRebound(to: integer_t.self, capacity: Int(tc)) {
                    thread_info(threads[i], thread_flavor_t(THREAD_BASIC_INFO), $0, &tc)
                }
            }
            if kr == KERN_SUCCESS, (ti.flags & TH_FLAGS_IDLE) == 0 {
                total += Double(ti.cpu_usage) / Double(TH_USAGE_SCALE) * 100.0
            }
        }
        return total
    }

    // One compact line for the Live Activity / bar.
    static func line(_ s: SysSnapshot) -> String {
        let ramUsed = max(0, s.ramTotalMB - s.ramAppAvailMB)
        let ramPct = s.ramTotalMB > 0 ? ramUsed / s.ramTotalMB * 100 : 0
        return String(format: "RAM %.0f/%.0fMB %.0f%%  ROM %.0f/%.0fGB  CPU %.0f%%(app)",
                      ramUsed, s.ramTotalMB, ramPct, s.romFreeGB, s.romTotalGB, s.cpuAppPct)
    }
}
