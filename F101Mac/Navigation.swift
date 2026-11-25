import AppKit
import SwiftUI

enum PanelMetrics {
    static let width: CGFloat = 720
    static let height: CGFloat = 400
    static let statusHeight: CGFloat = 30
    static let navigationWidth: CGFloat = 54
}

enum PanelTheme: String, CaseIterable {
    case system = "跟隨系統", dark = "深色", light = "淺色"

    var scheme: ColorScheme? {
        switch self {
        case .system: nil
        case .dark: .dark
        case .light: .light
        }
    }
}

struct PanelPalette {
    let bg: Color
    let bar: Color
    let card: Color
    let highlight: Color
    let primary = Color(Self.rgb(0x00AE42))
    let danger: Color
    let warning: Color
    let text: Color
    let secondary: Color
    let border: Color
    let track: Color

    init(_ scheme: ColorScheme) {
        let dark = scheme == .dark
        bg = Color(Self.rgb(dark ? 0x141414 : 0xF3F4F5))
        bar = Color(Self.rgb(dark ? 0x1C1C1C : 0xFFFFFF))
        card = Color(Self.rgb(dark ? 0x232323 : 0xFFFFFF))
        highlight = Color(Self.rgb(dark ? 0x303030 : 0xF0F1F2))
        danger = Color(Self.statusColor(paused: false, dark: dark))
        warning = Color(Self.statusColor(paused: true, dark: dark))
        text = Color(Self.rgb(dark ? 0xF2F2F2 : 0x1A1A1A))
        secondary = Color(Self.rgb(dark ? 0xA3A3A3 : 0x757575))
        border = Color(Self.rgb(dark ? 0x383838 : 0xE8E9EB))
        track = Color(Self.rgb(dark ? 0x3A3A3A : 0xE5E7E8))
    }

    static func statusColor(paused: Bool, dark: Bool) -> NSColor {
        rgb(paused ? (dark ? 0xF09A17 : 0xD98713) : (dark ? 0xE54B4B : 0xD83D3D))
    }

    private static func rgb(_ hex: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat(hex >> 16) / 255, green: CGFloat((hex >> 8) & 255) / 255,
                blue: CGFloat(hex & 255) / 255, alpha: 1)
    }
}

private struct PanelPaletteKey: EnvironmentKey {
    static let defaultValue = PanelPalette(.dark)
}

extension EnvironmentValues {
    var panelPalette: PanelPalette {
        get { self[PanelPaletteKey.self] }
        set { self[PanelPaletteKey.self] = newValue }
    }
}

private struct CardSurface: ViewModifier {
    @Environment(\.panelPalette) private var palette
    let horizontal: CGFloat

    func body(content: Content) -> some View {
        content.padding(.horizontal, horizontal).padding(.vertical, 6)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(palette.card, in: RoundedRectangle(cornerRadius: 8))
    }
}

extension View {
    func panelCard(horizontal: CGFloat = 8) -> some View {
        modifier(CardSurface(horizontal: horizontal))
    }

    func panelButtonStyle<Style: ButtonStyle>(_ style: Style) -> some View {
        buttonStyle(KeyboardButtonStyle(decoration: style))
    }
}

private struct KeyboardButtonStyle<Style: ButtonStyle>: PrimitiveButtonStyle {
    @Environment(\.isEnabled) private var enabled
    let decoration: Style

    func makeBody(configuration: Configuration) -> some View {
        Button(configuration)
            .buttonStyle(decoration)
            .focusable(enabled)
            .onKeyPress(.space) {
                guard enabled else { return .ignored }
                configuration.trigger()
                return .handled
            }
    }
}

struct PanelButtonStyle: ButtonStyle {
    enum Tone { case neutral, primary, danger, destructiveOutline, selected, segment }
    @Environment(\.panelPalette) private var palette
    @Environment(\.isEnabled) private var enabled
    var height: CGFloat? = 22
    var expands = false
    var tone: Tone = .neutral

    private var fill: Color {
        switch tone {
        case .primary, .segment: palette.primary
        case .danger: palette.danger
        case .destructiveOutline: .clear
        default: palette.highlight
        }
    }
    private var text: Color {
        switch tone {
        case .primary, .danger, .segment: .white
        case .destructiveOutline: palette.danger
        case .selected: palette.primary
        default: palette.text
        }
    }
    private var outline: Color {
        switch tone {
        case .destructiveOutline: palette.danger
        case .selected: palette.primary
        default: .clear
        }
    }

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: height == 19 ? 11 : 12, weight: tone == .neutral ? .regular : .semibold))
            .lineLimit(1)
            .padding(.horizontal, height == 19 ? 5 : 6)
            .frame(maxWidth: expands ? .infinity : nil, maxHeight: height == nil ? .infinity : nil)
            .frame(height: height)
            .foregroundStyle(enabled ? text : palette.secondary.opacity(0.65))
            .background(enabled ? fill : palette.highlight, in: RoundedRectangle(cornerRadius: 6))
            .overlay { RoundedRectangle(cornerRadius: 6).strokeBorder(enabled ? outline : .clear, lineWidth: 1.5) }
            .contentShape(Rectangle())
            .opacity(configuration.isPressed ? 0.72 : 1)
    }
}

struct NavigationButtonStyle: ButtonStyle {
    @Environment(\.panelPalette) private var palette
    let selected: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(selected ? palette.primary : palette.secondary)
            .background(selected ? palette.primary.opacity(0.09) : .clear, in: RoundedRectangle(cornerRadius: 8))
            .overlay(alignment: .leading) {
                if selected {
                    RoundedRectangle(cornerRadius: 2).fill(palette.primary)
                        .frame(width: 3, height: 16).padding(.leading, 1)
                }
            }
            .contentShape(Rectangle())
            .opacity(configuration.isPressed ? 0.65 : 1)
    }
}

struct CardButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.contentShape(Rectangle()).opacity(configuration.isPressed ? 0.8 : 1)
    }
}

enum Screen: String, CaseIterable {
    case home = "設備", files = "設備儲存", history = "打印記錄", camera = "相機", settings = "設定", filament = "耗材"
    case control = "控制", connection = "新增打印機", device = "設備與 AMS", cloud = "拓竹雲監控", fleet = "多台總覽", wifi = "Wi-Fi 網絡", about = "關於"
    static let tabs: [Screen] = [.home, .files, .history, .camera, .settings, .filament]
    var isTab: Bool { Self.tabs.contains(self) }
    var tabTitle: String {
        switch self {
        case .files: "存儲"
        case .history: "記錄"
        case .settings: "設置"
        default: rawValue
        }
    }
    var icon: String {
        switch self {
        case .home: "house"
        case .files: "doc"
        case .history: "line.3.horizontal"
        case .camera: "video"
        case .settings: "gearshape"
        case .filament: "circle.grid.2x2"
        case .control: "slider.horizontal.3"
        default: "chevron.right"
        }
    }
}

struct PanelValue: View {
    @Environment(\.panelPalette) private var palette
    let title: String
    let value: String
    let color: Color?

    init(_ title: String, _ value: String, color: Color? = nil) {
        self.title = title
        self.value = value
        self.color = color
    }

    var body: some View {
        HStack(spacing: 5) {
            Text(title).foregroundStyle(palette.secondary).fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 0)
            Text(value).foregroundStyle(color ?? palette.text).lineLimit(1)
        }
        .accessibilityElement(children: .combine)
    }
}

struct Meter: View {
    @Environment(\.panelPalette) private var palette
    let fraction: Double
    let tint: Color

    var body: some View {
        GeometryReader { geometry in
            palette.track.overlay(alignment: .leading) {
                tint.frame(width: geometry.size.width * (fraction.isFinite ? min(1, max(0, fraction)) : 0))
            }.clipShape(Capsule())
        }.frame(height: 5).accessibilityHidden(true)
    }
}

enum DemoAMS {
    struct Slot { let material: String; let remaining: String?; let color: Color? }
    static let slots = [
        Slot(material: "PLA", remaining: "78%", color: Color(white: 0.94)),
        Slot(material: "PLA", remaining: "45%", color: Color(white: 0.07)),
        Slot(material: "PETG", remaining: "90%", color: Color(red: 0.86, green: 0.28, blue: 0.28)),
        Slot(material: "空", remaining: nil, color: nil)
    ]
}

struct PageCard<Content: View>: View {
    let title: String
    var fillsHeight = false
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.system(size: 13, weight: .bold)).accessibilityAddTraits(.isHeader)
            content
        }
        .frame(maxHeight: fillsHeight ? .infinity : nil, alignment: .topLeading)
        .panelCard()
    }
}

struct LibraryPage: View {
    @Environment(\.panelPalette) private var palette
    @ObservedObject var model: Model
    let page: Screen
    let go: (Screen) -> Void
    @State private var slot = 0
    @State private var source = 1
    @State private var selected = false
    @State private var version = false
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
                switch page {
                case .files:
                    Picker("來源", selection: $source) {
                        Text("快取").tag(0); Text("打印機 TF 卡").tag(1); Text("本機 TF 卡").tag(2)
                    }.pickerStyle(.segmented).onChange(of: source) { _, _ in selected = false }
                    HStack(alignment: .top, spacing: 6) {
                        PageCard(title: ["快取 › 打印文件", "打印機 TF 卡", "本機 TF 卡"][source], fillsHeight: true) {
                            if model.mock {
                                Button { selected = true } label: { Label("校準方塊.3mf", systemImage: "doc") }
                                Button("刷新") { selected = false }
                            } else { Text("此版本未接入 FTPS 或本機 TF 卡。") }
                        }
                        PageCard(title: selected && model.mock ? "校準方塊.3mf" : "檔案詳情", fillsHeight: true) {
                            Image(systemName: "photo").font(.largeTitle).foregroundStyle(palette.secondary)
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                                .background(palette.highlight, in: RoundedRectangle(cornerRadius: 6))
                                .accessibilityHidden(true)
                            Text(selected && model.mock ? "3MF · 模擬檔案" : "請選擇檔案")
                            unavailable(source == 2 ? ["上傳到打印機"] : ["開始打印","刪除"])
                        }.frame(width: 224)
                    }
                case .history:
                    HStack(alignment: .top, spacing: 6) {
                        PageCard(title: "打印記錄", fillsHeight: true) {
                            if model.mock {
                                Button { selected.toggle() } label: { Label("校準方塊.3mf", systemImage: "clock") }
                            } else { Text("未接入打印記錄來源") }
                        }
                        PageCard(title: "記錄詳情", fillsHeight: true) {
                            Text(selected ? "模擬記錄 · 已完成" : "請選擇記錄")
                                .foregroundStyle(palette.secondary)
                        }.frame(width: 224)
                    }
                case .filament:
                    HStack(alignment: .top, spacing: 6) {
                        PageCard(title: "AMS 槽位", fillsHeight: true) {
                            ForEach(0..<4) { i in
                                Button { slot = i } label: {
                                    HStack(spacing: 6) {
                                        Circle().fill(model.mock ? DemoAMS.slots[i].color ?? palette.track : palette.track)
                                            .frame(width: 10, height: 10).accessibilityHidden(true)
                                        Text("A\(i+1)")
                                        Spacer()
                                        Text(model.mock ? DemoAMS.slots[i].material : Model.missing)
                                    }
                                }
                                .panelButtonStyle(PanelButtonStyle(height: nil, expands: true, tone: model.mock && slot == i ? .selected : .neutral))
                                .disabled(!model.mock).help("模擬槽位詳情")
                            }
                        }
                        PageCard(title: "耗材詳情", fillsHeight: true) {
                            Text(model.mock ? "A\(slot+1) · " + DemoAMS.slots[slot].material : "未接入 AMS 資料")
                            Text("剩餘 " + (model.mock ? DemoAMS.slots[slot].remaining ?? Model.missing : Model.missing))
                            unavailable(["退料","進料"])
                        }.frame(width: 224)
                    }
                case .device:
                    HStack(alignment: .top, spacing: 6) {
                        VStack(spacing: 6) {
                            PageCard(title: "系統") {
                                Text(model.printerName)
                                unavailable(["同步狀態","關蜂鳴器","全校準"])
                            }
                            PageCard(title: "燈光") { unavailable(["機箱燈 開","機箱燈 關","工作燈 開","工作燈 關"]) }
                            PageCard(title: "目標溫度") { Button("噴頭與熱床") { go(.control) } }
                        }
                        VStack(spacing: 6) {
                            PageCard(title: "風扇與 HMS") {
                                Text(model.mock ? "模擬設備 · 無 HMS 告警" : "未接入診斷資料")
                                Button("查看風扇") { go(.control) }
                            }
                            PageCard(title: "AMS 槽位") {
                                Button("耗材管理") { go(.filament) }
                                unavailable(["AMS 復位","延時攝影"])
                            }
                        }
                    }
                case .settings:
                    HStack(alignment: .top, spacing: 6) {
                        VStack(spacing: 6) {
                            PageCard(title: "Wi-Fi 網絡", fillsHeight: true) {
                                Button("掃描附近 Wi-Fi") { go(.wifi) }
                                Text("由 macOS 管理").foregroundStyle(palette.secondary)
                                Button("區網連線設定") { go(.connection) }
                            }
                            PageCard(title: "設備資訊", fillsHeight: true) {
                                Text("F101 Mac · macOS")
                                Button("設備與 AMS") { go(.device) }
                            }
                        }
                        VStack(spacing: 6) {
                            PageCard(title: "顯示", fillsHeight: true) {
                                Picker("主題", selection: $model.theme) {
                                    ForEach(PanelTheme.allCases, id: \.self) { theme in
                                        Text(theme.rawValue).tag(theme)
                                    }
                                }.pickerStyle(.segmented)
                                Text("繁體中文 · 橫屏")
                                Text("螢幕亮度、方向及音量由 macOS 管理。").font(.caption)
                                unavailable(["語音提示","觸控回饋音"])
                            }
                            PageCard(title: "關於", fillsHeight: true) {
                                Button("關於本機與應用") { go(.about) }
                                Button("結束 App") { model.clear(); NSApp.terminate(nil) }
                            }
                            if let notifications = model.notifications {
                                NotificationSettings(model: model, notifications: notifications)
                            }
                        }
                    }
                case .wifi:
                    PageCard(title: "可用網絡") { Text("Wi-Fi 由 macOS 管理。面板使用目前的網絡連接打印機。"); unavailable(["掃描網絡","連線","斷開並清除"]) }
                case .about:
                    PageCard(title: "拓竹 LAN 面板") {
                        Text("F101 選單列版 · KSDIY 酷世 DIY")
                        Button("版本資訊") { version.toggle() }
                        if version { Text("上游 Kevincoooool/KSDIY-F101 · 623fb75") }
                    }
                case .cloud:
                    PageCard(title: "中國區 · 公網唯讀") {
                        Text("本版使用區網，雲端登入及設備同步未開放。")
                        TextField("電郵或中國內地手機號碼", text: .constant("")).disabled(true)
                        SecureField("驗證碼", text: .constant("")).disabled(true)
                        unavailable(["取得驗證碼","登入並儲存"])
                    }
                    PageCard(title: "設備監控") { Button("多台總覽") { go(.fleet) }; HStack { Button("區網相機") { go(.camera) }; Button("設備儲存") { go(.files) } } }
                case .fleet:
                    PageCard(title: "設備清單") { Text(model.mock ? "模擬打印機 · 單台" : "目前區網連線 · 單台"); Button("查看設備") { go(.home) }; Button("新增打印機") { go(.connection) } }
                default: EmptyView()
                }
        }.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
    private func unavailable(_ titles: [String]) -> some View {
        HStack {
            ForEach(titles, id: \.self) { name in Button(name) {}.disabled(true) }
        }.help("本版未開放此操作").accessibilityHint("本版未開放此操作")
    }
}

private struct NotificationSettings: View {
    @ObservedObject var model: Model
    @ObservedObject var notifications: PrintFailureNotifications

    var body: some View {
        PageCard(title: "打印失敗通知") {
            PanelValue("通知權限", notifications.authorizationLabel)
            HStack(spacing: 6) {
                Button("打開系統設定", action: openSystemSettings)
                if model.mock {
                    Button(model.failureSimulationScheduled ? "模擬失敗排程中" : "模擬打印失敗") {
                        model.simulatePrintFailure()
                    }
                    .disabled(model.failureSimulationScheduled || model.pending != nil)
                    .help("三秒後回報打印失敗。可關閉面板測試通知。")
                }
            }
        }
        .onAppear { notifications.refreshAuthorization() }
    }

    private func openSystemSettings() {
        let workspace = NSWorkspace.shared
        let notificationsPane = URL(string: "x-apple.systempreferences:com.apple.Notifications-Settings.extension")!
        if !workspace.open(notificationsPane),
           let settings = workspace.urlForApplication(withBundleIdentifier: "com.apple.systempreferences") {
            workspace.open(settings)
        }
    }
}
