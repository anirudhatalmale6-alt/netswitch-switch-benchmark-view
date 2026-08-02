import Foundation
#if canImport(ActivityKit)
import ActivityKit

// The Live Activity is the iOS equivalent of the Android always-on top bar:
// it stays on the Lock Screen and in the Dynamic Island regardless of which app
// is on screen. iOS has no floating-window overlay for third-party apps, so this
// is the App-Store-legal "always visible" surface.
@available(iOS 16.1, *)
struct SysBarAttributes: ActivityAttributes {
    public struct ContentState: Codable, Hashable {
        var line: String       // the compact RAM/ROM/CPU string
        var ramPct: Double
        var cpuPct: Double
    }
    var title: String          // "GGW SysBar"
}
#endif
