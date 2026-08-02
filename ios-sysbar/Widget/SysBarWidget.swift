import WidgetKit
import SwiftUI
import ActivityKit

// Renders the always-on bar as a Live Activity: a small font line on the Lock
// Screen and a compact/expanded presentation in the Dynamic Island.
@available(iOS 16.1, *)
struct SysBarLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: SysBarAttributes.self) { context in
            // Lock Screen / banner presentation — small font, the full stat line.
            HStack {
                Image(systemName: "gauge.with.dots.needle.bottom.50percent")
                Text(context.state.line)
                    .font(.system(size: 12, weight: .medium, design: .monospaced))
                    .lineLimit(1).minimumScaleFactor(0.6)
            }
            .padding(.horizontal, 12).padding(.vertical, 6)
        } dynamicIsland: { context in
            DynamicIsland {
                DynamicIslandExpandedRegion(.leading) {
                    Label(String(format: "RAM %.0f%%", context.state.ramPct), systemImage: "memorychip")
                        .font(.caption2)
                }
                DynamicIslandExpandedRegion(.trailing) {
                    Label(String(format: "CPU %.0f%%", context.state.cpuPct), systemImage: "cpu")
                        .font(.caption2)
                }
                DynamicIslandExpandedRegion(.bottom) {
                    Text(context.state.line)
                        .font(.system(size: 11, weight: .regular, design: .monospaced))
                        .lineLimit(1).minimumScaleFactor(0.6)
                }
            } compactLeading: {
                Text(String(format: "%.0f%%", context.state.ramPct)).font(.caption2)
            } compactTrailing: {
                Text(String(format: "%.0f%%", context.state.cpuPct)).font(.caption2)
            } minimal: {
                Text(String(format: "%.0f", context.state.ramPct)).font(.caption2)
            }
        }
    }
}

@main
struct SysBarWidgetBundle: WidgetBundle {
    var body: some Widget {
        if #available(iOS 16.1, *) {
            SysBarLiveActivity()
        }
    }
}
