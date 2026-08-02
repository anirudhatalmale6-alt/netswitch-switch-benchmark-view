import SwiftUI
#if canImport(ActivityKit)
import ActivityKit
#endif

@main
struct SysBarApp: App {
    var body: some Scene {
        WindowGroup { DashboardView() }
    }
}

final class StatsModel: ObservableObject {
    @Published var snap = SysStats.snapshot()
    private var timer: Timer?
    #if canImport(ActivityKit)
    private var activity: Any?
    #endif

    func start() {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            guard let self = self else { return }
            self.snap = SysStats.snapshot()
            self.updateActivity()
        }
    }

    func stop() { timer?.invalidate(); timer = nil }

    // Start the Live Activity (the always-visible bar on Lock Screen / Dynamic Island).
    func startBar() {
        #if canImport(ActivityKit)
        if #available(iOS 16.1, *) {
            guard ActivityAuthorizationInfo().areActivitiesEnabled else { return }
            let s = snap
            let state = SysBarAttributes.ContentState(
                line: SysStats.line(s),
                ramPct: s.ramTotalMB > 0 ? (s.ramTotalMB - s.ramAppAvailMB) / s.ramTotalMB * 100 : 0,
                cpuPct: s.cpuAppPct)
            let attr = SysBarAttributes(title: "GGW SysBar")
            do {
                if #available(iOS 16.2, *) {
                    activity = try Activity.request(
                        attributes: attr,
                        content: .init(state: state, staleDate: nil))
                } else {
                    activity = try Activity.request(attributes: attr, contentState: state)
                }
            } catch { print("Live Activity start failed: \(error)") }
        }
        #endif
    }

    func stopBar() {
        #if canImport(ActivityKit)
        if #available(iOS 16.1, *), let a = activity as? Activity<SysBarAttributes> {
            Task { await a.end(nil, dismissalPolicy: .immediate) }
            activity = nil
        }
        #endif
    }

    private func updateActivity() {
        #if canImport(ActivityKit)
        if #available(iOS 16.1, *), let a = activity as? Activity<SysBarAttributes> {
            let s = snap
            let state = SysBarAttributes.ContentState(
                line: SysStats.line(s),
                ramPct: s.ramTotalMB > 0 ? (s.ramTotalMB - s.ramAppAvailMB) / s.ramTotalMB * 100 : 0,
                cpuPct: s.cpuAppPct)
            Task {
                if #available(iOS 16.2, *) {
                    await a.update(.init(state: state, staleDate: nil))
                } else {
                    await a.update(using: state)
                }
            }
        }
        #endif
    }
}

struct DashboardView: View {
    @StateObject private var model = StatsModel()

    var body: some View {
        let s = model.snap
        let ramUsed = max(0, s.ramTotalMB - s.ramAppAvailMB)
        return NavigationView {
            List {
                Section("Memory (RAM)") {
                    row("Device total", String(format: "%.0f MB", s.ramTotalMB))
                    row("Available to app", String(format: "%.0f MB", s.ramAppAvailMB))
                    row("Used (device−avail)", String(format: "%.0f MB", ramUsed))
                    row("Install-time footprint", String(format: "%.1f MB", s.appFootprintMB))
                }
                Section("Storage (ROM)") {
                    row("Total", String(format: "%.1f GB", s.romTotalGB))
                    row("Free", String(format: "%.1f GB", s.romFreeGB))
                }
                Section("CPU") {
                    row("This app", String(format: "%.0f %% (app)", s.cpuAppPct))
                    Text("iOS does not expose device-wide CPU% to sandboxed apps — the (app) tag is honest, same as the Android build.")
                        .font(.footnote).foregroundColor(.secondary)
                }
                Section("Always-on bar") {
                    Button("Start bar (Live Activity)") { model.startBar() }
                    Button("Stop bar") { model.stopBar() }
                    Text("The bar shows on the Lock Screen and Dynamic Island over every app. iOS has no floating overlay for third-party apps, so this is the always-visible surface.")
                        .font(.footnote).foregroundColor(.secondary)
                }
            }
            .navigationTitle("GGW SysBar")
        }
        .onAppear { model.start() }
        .onDisappear { model.stop() }
    }

    private func row(_ k: String, _ v: String) -> some View {
        HStack { Text(k); Spacer(); Text(v).foregroundColor(.secondary).monospacedDigit() }
    }
}
