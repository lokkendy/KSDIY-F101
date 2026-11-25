import AppKit
import Combine
import SwiftUI

protocol DeviceLink: AnyObject {
    func send(_ command: String) throws
}

final class Link: DeviceLink {
    private let process = Process()
    private let input = Pipe()
    init(_ role: String, config: [String:String], event: @escaping ([String:Any]) -> Void) throws {
        let output = Pipe()
        let script = Bundle.main.url(forResource: "link", withExtension: "py")!
        process.executableURL = URL(fileURLWithPath: "/usr/bin/python3")
        process.arguments = ["-I", "-B", "-u", script.path, role]
        process.standardInput = input
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        try process.run()
        DispatchQueue.global(qos: .utility).async {
            var buffer = Data()
            while true {
                let chunk = output.fileHandleForReading.availableData
                if chunk.isEmpty { break }
                buffer.append(chunk)
                while let end = buffer.firstIndex(of: 10) {
                    if let value = try? JSONSerialization.jsonObject(with: buffer[..<end]) as? [String:Any] {
                        DispatchQueue.main.async { event(value) }
                    }
                    buffer.removeSubrange(...end)
                }
            }
            DispatchQueue.main.async { event(["closed": true]) }
        }
        try input.fileHandleForWriting.write(contentsOf: JSONSerialization.data(withJSONObject: config) + Data([10]))
    }
    func send(_ command: String) throws {
        try input.fileHandleForWriting.write(contentsOf: Data((command + "\n").utf8))
    }
    deinit {
        try? input.fileHandleForWriting.close()
        if process.isRunning { process.terminate() }
    }
}

enum PrintAction: String {
    case pause, stop
    var title: String { self == .pause ? "暫停" : "停止" }

    func allows(_ state: String) -> Bool {
        state == "RUNNING" || (self == .stop && state == "PAUSE")
    }
}

final class Model: ObservableObject {
    typealias OpenLink = (String, [String:String], @escaping ([String:Any]) -> Void) throws -> DeviceLink
    static let missing = "—"
    static let lanControlHelp = "區網模式僅開放暫停與停止。"
    private static let taskFields = ["subtask_name", "gcode_file", "mc_percent", "mc_remaining_time",
                                     "layers", "elapsed", "eta", "material"]
    private static let demoTask = ["subtask_name":"校準方塊.3mf", "mc_percent":"42", "mc_remaining_time":"18",
                                   "layers":"82 / 196", "elapsed":"13 分", "eta":"14:41", "material":"PLA · 白"]
    @Published var theme: PanelTheme {
        didSet { defaults.set(theme.rawValue, forKey: "panelAppearance") }
    }
    @Published var controls = DemoControls()
    @Published private(set) var mock = true
    @Published var values = [String:String]()
    @Published private(set) var pending: PrintAction?
    @Published private var freshPrintState = false
    @Published private(set) var notice = ""
    @Published private(set) var frame: NSImage?
    @Published private(set) var cameraNote = ""
    @Published private(set) var failureSimulationScheduled = false
    let notifications: PrintFailureNotifications?
    private let defaults: UserDefaults
    private let openLink: OpenLink
    private var link: DeviceLink?, camera: DeviceLink?
    private var generation = UUID(), videoGeneration = UUID()
    private var stateExpiry: DispatchWorkItem?, commandExpiry: DispatchWorkItem?
    private let demoFailures = PrintFailureTracker()
    private let lanFailures: PrintFailureTracker
    var config = ["host":"", "serial":"", "code":"", "pin":""]

    init(defaults: UserDefaults = .standard, notifications: PrintFailureNotifications? = nil,
         openLink: @escaping OpenLink = { try Link($0, config: $1, event: $2) }) {
        self.defaults = defaults
        self.notifications = notifications
        self.openLink = openLink
        lanFailures = PrintFailureTracker(defaults: defaults)
        theme = defaults.string(forKey: "panelAppearance").flatMap(PanelTheme.init(rawValue:)) ?? .system
        demo()
    }

    var online: Bool { mock || !values.isEmpty }
    var printerName: String { mock ? "A1 · 模擬打印機" : "區網打印機" }
    var mode: String { mock ? "模擬" : "區網" }
    var connectionText: String { (online ? "在線" : "離線") + " · " + mode }
    var state: String { values["gcode_state"] ?? Self.missing }
    var stateText: String {
        ["RUNNING":"打印中", "PAUSE":"已暫停", "IDLE":"待機", "FINISH":"已完成",
         "FAILED":"錯誤", "PREPARE":"準備中", "SLICING":"切片中"][state] ?? Self.missing
    }
    var filename: String { values["subtask_name"] ?? values["gcode_file"] ?? Self.missing }
    var percent: Int? { Self.percent(in: values) }
    var percentText: String { percent.map { "\($0)%" } ?? Self.missing }
    static func percent(in values: [String:String]) -> Int? {
        guard let raw = values["mc_percent"].flatMap(Double.init), raw.isFinite else { return nil }
        return Int(min(100, max(0, raw)))
    }
    var remaining: String { values["mc_remaining_time"].map { "\($0) 分" } ?? Self.missing }
    var speed: String { mock ? controls.speed : Self.missing }
    var maskedSerial: String {
        let serial = mock ? "03W0000012" : config["serial"] ?? ""
        guard !serial.isEmpty else { return Self.missing }
        guard serial.count >= 8 else { return "••••" }
        return serial.prefix(4) + "••••" + serial.suffix(2)
    }

    func clear() {
        generation = UUID()
        link = nil
        closeCamera()
        stateExpiry?.cancel()
        freshPrintState = false
        finishCommand()
        demoFailures.beginSession()
        lanFailures.beginSession()
        failureSimulationScheduled = false
        controls = DemoControls()
        values = [:]
        notice = ""
    }
    func demo() {
        clear()
        mock = true
        controls.positions = [128, 128, 16.4]
        controls.fans = [75, 0, 0]
        report(Self.demoTask.merging(["gcode_state":"RUNNING", "nozzle_temper":"218", "nozzle_target_temper":"220",
                  "bed_temper":"60", "bed_target_temper":"60", "chamber_temper":"32",
                  "firmware":"01.04.00", "wifi":"−52 dBm", "lamp":"開", "hms":"正常",
                  "amsConditions":"濕度 2 級 · 26 °C"]) { _, new in new })
    }
    func simulatePrintFailure() {
        guard mock, pending == nil, !failureSimulationScheduled else { return }
        failureSimulationScheduled = true
        report(Self.demoTask.merging(["gcode_state": "RUNNING", "subtask_id": UUID().uuidString]) { _, new in new })
        let token = generation
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self, self.generation == token else { return }
            self.failureSimulationScheduled = false
            self.report(["gcode_state": "FAILED"])
        }
    }
    func connect() {
        clear()
        mock = false
        notice = "正在連線，等待設備回報"
        let token = generation
        do {
            link = try openLink("monitor", config) { [weak self] event in
                guard let self, token == self.generation else { return }
                if let status = event["status"] as? [String:Any] {
                    self.report(status.mapValues { String(describing: $0) })
                } else if let error = event["error"] as? String {
                    self.clear()
                    self.notice = error
                } else if event["closed"] != nil {
                    self.clear()
                    self.notice = "已斷線"
                } else if let message = event["message"] as? String, self.pending != nil {
                    self.notice = message
                    if message == "設備狀態已改變或過期，請重新核對" {
                        self.freshPrintState = false
                        self.finishCommand()
                    }
                }
            }
        } catch { notice = "無法啟動區網連線" }
    }
    private func report(_ status: [String:String]) {
        if values.isEmpty { notice = "" }
        var updated = values
        // Only a switch between two known tasks makes the stored task details stale.
        if let newID = PrintFailureTracker.normalizedTaskID(status["subtask_id"]),
           let oldID = PrintFailureTracker.normalizedTaskID(values["subtask_id"]), newID != oldID {
            for field in Self.taskFields { updated.removeValue(forKey: field) }
        }
        updated.merge(status) { _, new in new }
        values = updated
        let reportedState = status["gcode_state"]
        let tracker = mock ? demoFailures : lanFailures
        if let failure = tracker.record(state: reportedState, taskID: values["subtask_id"]) {
            let identifiers = [config["host"], config["serial"], config["code"], config["pin"]].compactMap { $0 }
            notifications?.failure(printer: printerName, filename: filename, percent: percent, redacting: identifiers) {
                tracker.didDeliver(failure)
            }
        }
        guard let reportedState else { return }
        if let pending, !pending.allows(reportedState) {
            finishCommand()
            notice = ""
        }
        guard !mock else { return }
        stateExpiry?.cancel()
        freshPrintState = true
        let expiry = DispatchWorkItem { [weak self] in self?.freshPrintState = false }
        stateExpiry = expiry
        DispatchQueue.main.asyncAfter(deadline: .now() + 30, execute: expiry)
    }
    func closeCamera() {
        videoGeneration = UUID()
        camera = nil
        frame = nil
        cameraNote = ""
    }
    func openCamera() {
        closeCamera()
        if mock { cameraNote = "模擬鏡頭測試圖"; return }
        guard online, link != nil else { cameraNote = "請先連線至打印機"; return }
        let token = videoGeneration
        cameraNote = "正在連接 P1／A1 鏡頭"
        do {
            camera = try openLink("camera", config) { [weak self] event in
                guard let self, token == self.videoGeneration else { return }
                if let encoded = event["frame"] as? String, let data = Data(base64Encoded: encoded),
                   let image = NSImage(data: data) {
                    self.frame = image
                    self.cameraNote = ""
                } else {
                    self.closeCamera()
                    self.cameraNote = event["error"] as? String ?? "鏡頭已斷線"
                }
            }
        } catch { cameraNote = "無法開啟鏡頭" }
    }
    func canSend(_ action: PrintAction) -> Bool {
        pending == nil && action.allows(state) && (mock || (link != nil && freshPrintState))
    }
    var canResumeDemo: Bool { mock && pending == nil && state == "PAUSE" }
    func resumeDemo() {
        guard canResumeDemo else { return }
        report(["gcode_state": "RUNNING"])
    }
    @discardableResult
    func command(_ action: PrintAction) -> Bool {
        guard canSend(action) else { return false }
        pending = action
        notice = mock ? "等待模擬設備回報" : "等待設備回報"
        if mock {
            let token = generation
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { [weak self] in
                guard let self, self.generation == token, self.pending == action else { return }
                self.report(["gcode_state": action == .pause ? "PAUSE" : "IDLE"])
            }
        } else {
            do { try link?.send(action.rawValue) }
            catch { clear(); notice = "指令傳送失敗"; return false }
            let expiry = DispatchWorkItem { [weak self] in
                guard let self else { return }
                self.finishCommand()
                self.notice = "設備未回報操作結果，請核對打印狀態"
            }
            commandExpiry = expiry
            DispatchQueue.main.asyncAfter(deadline: .now() + 30, execute: expiry)
        }
        return true
    }
    private func finishCommand() {
        commandExpiry?.cancel()
        commandExpiry = nil
        pending = nil
    }
}

struct Panel: View {
    @ObservedObject var model: Model
    @Environment(\.colorScheme) private var systemScheme
    @State private var path: [Screen] = [.home]
    @State private var confirmation: PrintAction?
    @State private var expandedCamera = false
    @FocusState private var initialFocus: Bool
    @State private var initialFocusAvailable = true
    private var page: Screen { path.last ?? .home }
    private var scheme: ColorScheme { model.theme.scheme ?? systemScheme }
    private var palette: PanelPalette { PanelPalette(scheme) }

    var body: some View {
        VStack(spacing: 0) {
            statusBar
            HStack(spacing: 0) {
                navigation
                pageContent
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .padding(6)
            }
        }
        .disabled(confirmation != nil)
        .accessibilityHidden(confirmation != nil)
        .overlay(alignment: .bottomTrailing) {
            if let confirmation {
                confirmationOverlay(confirmation)
                    .frame(width: PanelMetrics.width - PanelMetrics.navigationWidth,
                           height: PanelMetrics.height - PanelMetrics.statusHeight)
            }
        }
        .frame(width: PanelMetrics.width, height: PanelMetrics.height)
        .overlay(alignment: .topLeading) {
            // Keep opening the panel passive until the user starts keyboard navigation.
            Color.clear.frame(width: 1, height: 1)
                .focusable(initialFocusAvailable)
                .focused($initialFocus)
                .focusEffectDisabled()
                .accessibilityHidden(true)
                .allowsHitTesting(false)
        }
        .defaultFocus($initialFocus, true)
        .onChange(of: initialFocus) { wasFocused, isFocused in
            if wasFocused && !isFocused { initialFocusAvailable = false }
        }
        .background(palette.bg)
        .font(.system(size: 12))
        .monospacedDigit()
        .foregroundStyle(palette.text)
        .tint(palette.primary)
        .panelButtonStyle(PanelButtonStyle())
        .environment(\.panelPalette, palette)
        .environment(\.colorScheme, scheme)
        .onExitCommand {
            if confirmation != nil { confirmation = nil }
            else if expandedCamera { expandedCamera = false }
        }
    }

    private var statusBar: some View {
        HStack(spacing: 7) {
            if page.isTab {
                statusDot
                Text(model.printerName).font(.system(size: 13, weight: .bold))
                Text(model.connectionText).foregroundStyle(palette.secondary)
            } else {
                Button { goBack() } label: { Label("返回", systemImage: "chevron.left") }
                    .panelButtonStyle(PanelButtonStyle(height: 19))
                Text(page.rawValue).font(.system(size: 13, weight: .bold)).accessibilityAddTraits(.isHeader)
            }
            Spacer(minLength: 6)
            if page == .home {
                Button("多台總覽") { go(.fleet) }
                Button("拓竹雲監控") { go(.cloud) }
                Button { go(.connection) } label: { Label("新增打印機", systemImage: "plus") }
            } else if !page.isTab {
                statusDot
                Text([model.printerName, model.connectionText, model.stateText].joined(separator: " · "))
                    .foregroundStyle(palette.secondary)
            } else if page == .camera && (model.mock || model.frame != nil) {
                Text(model.mock ? "模擬畫面" : "實時畫面").foregroundStyle(palette.secondary)
            }
        }
        .lineLimit(1)
        .panelButtonStyle(PanelButtonStyle(height: 19))
        .padding(.leading, 10).padding(.trailing, 8)
        .frame(height: PanelMetrics.statusHeight)
        .background(palette.bar)
        .overlay(alignment: .bottom) { Rectangle().fill(palette.border).frame(height: 1) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("狀態列")
    }

    private var statusDot: some View {
        Circle().fill(model.online ? palette.primary : palette.danger).frame(width: 7, height: 7)
            .accessibilityHidden(true)
    }

    private var navigation: some View {
        VStack(spacing: 2) {
            ForEach(Screen.tabs, id: \.self) { target in
                let selected = path.first == target
                Button { go(target) } label: {
                    VStack(spacing: 3) {
                        Image(systemName: target.icon).font(.system(size: 16))
                        Text(target.tabTitle).font(.system(size: 10))
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .panelButtonStyle(NavigationButtonStyle(selected: selected))
                .accessibilityLabel(target.tabTitle)
                .accessibilityAddTraits(selected ? .isSelected : [])
                .accessibilityIdentifier("navigation.\(target.icon)")
            }
        }
        .padding(.horizontal, 3).padding(.vertical, 4)
        .frame(width: PanelMetrics.navigationWidth)
        .background(palette.bar)
        .overlay(alignment: .trailing) { Rectangle().fill(palette.border).frame(width: 1) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("主導航")
    }

    @ViewBuilder private var pageContent: some View {
        switch page {
        case .home: homePage
        case .camera: cameraPage
        case .control: ControlPage(model: model, confirm: ask)
        case .connection: SettingsForm(model: model) { go(.home) }
        default: LibraryPage(model: model, page: page, go: go).id(page)
        }
    }

    private var homePage: some View {
        GeometryReader { geometry in
            HStack(spacing: 6) {
                VStack(alignment: .leading, spacing: 4) {
                    Text(model.printerName).font(.system(size: 13, weight: .bold))
                    HStack(spacing: 4) {
                        Circle().fill(stateColor).frame(width: 7, height: 7).accessibilityHidden(true)
                        Text(model.stateText).foregroundStyle(stateColor)
                        Text("· " + (model.mock ? model.speed + "速度" : Model.missing)).foregroundStyle(palette.secondary)
                    }
                    Image(systemName: "printer")
                        .font(.system(size: 43, weight: .light))
                        .foregroundStyle(palette.secondary)
                        .frame(maxWidth: .infinity, minHeight: 96, maxHeight: .infinity)
                        .background(palette.highlight, in: RoundedRectangle(cornerRadius: 7))
                        .accessibilityHidden(true)
                    PanelValue("序號", model.maskedSerial)
                    PanelValue("IP", model.mock ? "192.0.2.40" : knownConfig("host"))
                    PanelValue("韌體", model.values["firmware"] ?? Model.missing)
                    PanelValue("Wi-Fi", model.values["wifi"] ?? Model.missing)
                    PanelValue("腔燈", model.values["lamp"] ?? Model.missing)
                    PanelValue("HMS", model.values["hms"] ?? Model.missing, color: model.mock ? palette.primary : nil)
                    if !model.notice.isEmpty {
                        Text(model.notice).font(.system(size: 10)).foregroundStyle(palette.secondary)
                            .lineLimit(2).fixedSize(horizontal: false, vertical: true)
                    }
                    Button("設備與 AMS") { go(.device) }
                    Button("區網連線設定") { go(.connection) }
                }
                .panelButtonStyle(PanelButtonStyle(height: 22, expands: true))
                .panelCard()
                .frame(width: (geometry.size.width - 6) * 0.29)

                VStack(spacing: 6) {
                    HStack(spacing: 8) {
                        cover(size: 84)
                        VStack(alignment: .leading, spacing: 4) {
                            HStack(spacing: 6) {
                                taskName
                                Button { go(.camera) } label: { Label("實時監控", systemImage: "video") }
                                    .panelButtonStyle(PanelButtonStyle(height: 21))
                                    .fixedSize()
                            }
                            HStack(alignment: .firstTextBaseline, spacing: 8) {
                                progressNumber
                                Text("剩餘 " + model.remaining)
                                Text(model.values["eta"].map { "預計 \($0) 完成" } ?? "預計 —")
                                    .foregroundStyle(palette.secondary)
                            }.lineLimit(1)
                            Meter(fraction: (Double(model.percent ?? 0)) / 100, tint: palette.primary)
                                .accessibilityHidden(true)
                            HStack(spacing: 10) {
                                Text(model.values["layers"].map { "第 \($0) 層" } ?? "層數 —")
                                Text("已用 " + (model.values["elapsed"] ?? Model.missing))
                                Text(model.values["material"] ?? Model.missing)
                            }.foregroundStyle(palette.secondary)
                        }
                    }.panelCard()
                    temperatureCards(compact: false)
                    HStack(spacing: 6) {
                        ForEach([Screen.camera, .files, .control, .filament, .settings], id: \.self) { target in
                            Button(target == .settings ? target.tabTitle : target.rawValue) { go(target) }
                        }
                    }.panelButtonStyle(PanelButtonStyle(height: 28, expands: true))
                    amsSummary
                }
            }
        }
    }

    private var amsSummary: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("AMS").fontWeight(.bold)
                Spacer()
                Text(model.values["amsConditions"] ?? Model.missing).foregroundStyle(palette.secondary)
            }
            HStack(spacing: 6) {
                ForEach(Array(DemoAMS.slots.enumerated()), id: \.offset) { index, slot in
                    VStack(alignment: .leading, spacing: 3) {
                        let swatch = model.mock ? slot.color : nil
                        RoundedRectangle(cornerRadius: 4)
                            .fill(swatch ?? Color.clear)
                            .overlay {
                                RoundedRectangle(cornerRadius: 4).strokeBorder(palette.secondary.opacity(0.45),
                                    style: StrokeStyle(lineWidth: 1, dash: swatch == nil ? [3, 2] : []))
                            }
                            .frame(maxHeight: .infinity)
                            .accessibilityHidden(true)
                        Text("A\(index + 1) " + (model.mock ? slot.material : Model.missing))
                        Text(model.mock ? slot.remaining ?? " " : Model.missing).foregroundStyle(palette.secondary)
                    }
                    .font(.system(size: 11))
                    .padding(6)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
                    .background(palette.highlight, in: RoundedRectangle(cornerRadius: 6))
                    .overlay {
                        if index == 0 && model.mock {
                            RoundedRectangle(cornerRadius: 6).strokeBorder(palette.primary, lineWidth: 1.5)
                        }
                    }
                    .accessibilityElement(children: .combine)
                    .accessibilityAddTraits(index == 0 && model.mock ? .isSelected : [])
                }
            }
        }.panelCard()
    }

    private var cameraPage: some View {
        HStack(spacing: 6) {
            VStack(spacing: 6) {
                ZStack {
                    if let frame = model.frame {
                        Color.black
                        Image(nsImage: frame).resizable().scaledToFit().accessibilityLabel("打印機鏡頭")
                    } else {
                        (model.mock ? palette.bg : .black)
                        if model.mock { DemoCameraPattern().accessibilityHidden(true) }
                        VStack(spacing: 7) {
                            Text(model.cameraNote).foregroundStyle(model.mock ? palette.secondary : .white)
                            if !model.mock {
                                Button("重新開啟鏡頭") { model.openCamera() }
                            }
                        }
                    }
                }
                .overlay(alignment: .top) {
                    HStack {
                        Text(model.printerName)
                        Spacer()
                        if model.mock || model.frame != nil { Text("實時畫面").foregroundStyle(model.mock ? palette.secondary : Color.white) }
                    }
                    .foregroundStyle(model.mock ? palette.text : Color.white)
                    .padding(8)
                    .allowsHitTesting(false)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .clipShape(RoundedRectangle(cornerRadius: 8))
                HStack(spacing: 6) {
                    ForEach(["拍照", "錄製", "燈光"], id: \.self) { title in
                        Button(title) {}.disabled(true)
                            .help("本版未開放相機" + title)
                            .accessibilityHint("本版未開放此操作")
                    }
                    Button(expandedCamera ? "退出全屏" : "全屏") { expandedCamera.toggle() }
                }.panelButtonStyle(PanelButtonStyle(height: 26, expands: true))
            }
            if !expandedCamera {
                VStack(spacing: 6) {
                    HStack(spacing: 7) {
                        cover(size: 46)
                        VStack(alignment: .leading, spacing: 2) {
                            taskName
                            Text(model.stateText).foregroundStyle(stateColor)
                            Text(model.values["material"] ?? Model.missing).foregroundStyle(palette.secondary)
                        }
                    }.panelCard()
                    VStack(spacing: 4) {
                        HStack(alignment: .firstTextBaseline) {
                            progressNumber
                            Spacer()
                            Text("剩餘 " + model.remaining)
                        }
                        Meter(fraction: Double(model.percent ?? 0) / 100, tint: palette.primary)
                            .accessibilityHidden(true)
                        PanelValue("層數", model.values["layers"] ?? Model.missing)
                        PanelValue("已用", model.values["elapsed"] ?? Model.missing)
                        PanelValue("預計完成", model.values["eta"] ?? Model.missing)
                        PanelValue("速度", model.speed)
                    }.panelCard()
                    temperatureCards(compact: true)
                    if !model.notice.isEmpty {
                        Text(model.notice).font(.system(size: 10)).foregroundStyle(palette.secondary)
                            .frame(maxWidth: .infinity).fixedSize(horizontal: false, vertical: true)
                    }
                    PrintButtons(model: model, height: 32, confirm: ask)
                }.frame(width: 196)
            }
        }
    }

    private var stateColor: Color {
        switch model.state {
        case "RUNNING": palette.primary
        case "FAILED": palette.danger
        case "PAUSE": palette.warning
        default: palette.secondary
        }
    }
    private var taskName: some View {
        Text(model.filename)
            .font(.system(size: 13, weight: .bold))
            .lineLimit(1).truncationMode(.tail)
            .frame(maxWidth: .infinity, alignment: .leading)
            .help(model.filename)
            .accessibilityIdentifier("task.filename")
    }
    private var progressNumber: some View {
        Text(model.percentText).font(.system(size: 25, weight: .bold)).foregroundStyle(palette.primary)
            .accessibilityLabel("進度 " + model.percentText)
    }
    private func cover(size: CGFloat) -> some View {
        RoundedRectangle(cornerRadius: 6).fill(palette.highlight)
            .frame(width: size, height: size).accessibilityHidden(true)
    }
    private func temperatureCards(compact: Bool) -> some View {
        HStack(spacing: compact ? 4 : 6) {
            temperature("噴頭", "nozzle", compact: compact)
            temperature("熱床", "bed", compact: compact)
            temperature("腔體", "chamber", compact: compact)
        }
    }
    private func temperature(_ title: String, _ key: String, compact: Bool) -> some View {
        let reading = model.values[key + "_temper"]
        let target = model.values[key + "_target_temper"]
        let current = reading.flatMap(Double.init)?.formatted(.number.precision(.fractionLength(0))) ?? Model.missing
        let temperature = Text(current).font(.system(size: 18, weight: .bold))
        let detail = target.map { (compact ? "/" : "/ ") + $0 + (compact ? "" : " °C") }
            ?? (reading == nil ? Model.missing : (compact ? "°C" : " °C"))
        let fraction = reading.flatMap(Double.init).map { $0 / max(1, target.flatMap(Double.init) ?? 100) } ?? 0
        return Button { go(.control) } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(title).foregroundStyle(palette.secondary)
                if compact {
                    temperature
                    Text(detail).foregroundStyle(palette.secondary).lineLimit(1)
                } else {
                    HStack(alignment: .firstTextBaseline, spacing: 3) {
                        temperature
                        if reading != nil || target != nil { Text(detail).foregroundStyle(palette.secondary) }
                    }
                    Meter(fraction: fraction, tint: target == nil ? palette.secondary : palette.primary)
                }
            }
            .frame(maxHeight: compact ? .infinity : nil)
            .panelCard(horizontal: compact ? 5 : 8)
        }
        .panelButtonStyle(CardButtonStyle())
        .accessibilityLabel(title + " " + (reading.map { $0 + " 度" } ?? "未知") + (target.map { "，目標 " + $0 + " 度" } ?? ""))
        .accessibilityHint("開啟控制頁")
        .help("開啟控制頁")
    }

    private func confirmationOverlay(_ action: PrintAction) -> some View {
        ZStack {
            Color.black.opacity(0.55).accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 5) {
                Text(action.title + "打印？").font(.system(size: 15, weight: .bold))
                    .accessibilityAddTraits(.isHeader)
                Text([model.printerName, model.filename, model.percentText].joined(separator: " · "))
                    .lineLimit(1).truncationMode(.middle).help(model.filename)
                    .foregroundStyle(palette.secondary)
                Text(action == .stop ? "停止後無法繼續這次打印。" : "暫停後會保留目前打印進度。")
                    .foregroundStyle(palette.secondary)
                HStack(spacing: 6) {
                    Spacer()
                    Button("取消") { confirmation = nil }.keyboardShortcut(.cancelAction)
                    Button("確認" + action.title) {
                        guard confirmation == action else { return }
                        model.command(action)
                        confirmation = nil
                    }
                    .panelButtonStyle(PanelButtonStyle(height: 22, tone: action == .stop ? .danger : .primary))
                    .disabled(!model.canSend(action))
                }.padding(.top, 5)
            }
            .padding(12)
            .frame(width: 280)
            .background(palette.card, in: RoundedRectangle(cornerRadius: 10))
            .overlay { RoundedRectangle(cornerRadius: 10).strokeBorder(palette.border) }
            .accessibilityElement(children: .contain)
            .accessibilityLabel(action.title + "確認")
        }
    }

    private func go(_ target: Screen) {
        if target.isTab { transition(to: [target]) }
        else if target != page { transition(to: path + [target]) }
    }
    private func goBack() { transition(to: Array(path.dropLast())) }
    private func transition(to destination: [Screen]) {
        let next = destination.last ?? .home
        if next != page {
            expandedCamera = false
            if page == .camera { model.closeCamera() }
            if next == .camera { model.openCamera() }
        }
        path = destination
    }
    private func ask(_ action: PrintAction) {
        if model.canSend(action) { confirmation = action }
    }
    private func knownConfig(_ key: String) -> String {
        guard let value = model.config[key], !value.isEmpty else { return Model.missing }
        return value
    }
}

private struct DemoCameraPattern: View {
    @Environment(\.panelPalette) private var palette
    var body: some View {
        Canvas { context, size in
            var stripes = Path()
            for start in stride(from: -size.height, to: size.width, by: 24) {
                stripes.move(to: CGPoint(x: start, y: 0))
                stripes.addLine(to: CGPoint(x: start + size.height, y: size.height))
            }
            context.stroke(stripes, with: .color(palette.bar.opacity(0.8)), lineWidth: 9)
        }
    }
}

struct SettingsForm: View {
    @ObservedObject var model: Model
    var done: () -> Void
    @State private var host = ""
    @State private var serial = ""
    @State private var code = ""
    @State private var pin = ""
    var body: some View {
        PageCard(title: "LAN MQTT") {
            HStack {
                Text("從可信管道核對憑證指紋。連線資料僅保留至 App 結束。")
                Spacer()
                Button("掃描區網打印機") {}.disabled(true).help("本版未開放掃描")
            }
            TextField("打印機區網 IPv4", text: $host)
            TextField("設備序號", text: $serial)
            SecureField("LAN 存取碼", text: $code)
            TextField("已核對的 TLS 憑證 SHA-256 指紋", text: $pin)
            HStack {
                Button("使用模擬資料") { model.demo(); done() }
                Spacer()
                Button("連線") {
                    model.config = ["host":host, "serial":serial, "code":code, "pin":pin]
                    model.connect(); done()
                }.disabled(host.isEmpty || serial.isEmpty || code.isEmpty || pin.isEmpty)
            }
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
        .onAppear {
            host = model.config["host"] ?? ""
            serial = model.config["serial"] ?? ""
            code = model.config["code"] ?? ""
            pin = model.config["pin"] ?? ""
        }
    }
}

private final class PanelWindow: NSPanel {
    override var canBecomeKey: Bool { true }
}

@MainActor final class StatusPanel: NSObject {
    private let model: Model
    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let window = PanelWindow(
        contentRect: NSRect(x: 0, y: 0, width: PanelMetrics.width, height: PanelMetrics.height),
        styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false
    )
    private var observation: AnyCancellable?
    private var outsideClick: Any?
    private let printer = NSImage(systemSymbolName: "printer", accessibilityDescription: nil)

    init(model: Model) {
        self.model = model
        super.init()
        window.title = "拓竹屏"
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = true
        window.hidesOnDeactivate = false
        window.isReleasedWhenClosed = false
        window.level = .popUpMenu
        window.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        window.tabbingMode = .disallowed
        item.button?.target = self
        item.button?.action = #selector(toggle)
        item.button?.imagePosition = .imageLeading
        printer?.isTemplate = true
        observation = model.$values.sink { [weak self] in self?.updateIcon($0) }
    }

    @objc private func toggle() {
        if window.isVisible { dismiss(); return }
        openDevice()
    }

    func openDevice() {
        guard let button = item.button, let menuBarWindow = button.window else { return }
        let anchor = menuBarWindow.convertToScreen(button.convert(button.bounds, to: nil))
        let anchorScreen = NSScreen.screens.first { $0.frame.contains(NSPoint(x: anchor.midX, y: anchor.midY)) }
        let pointerScreen = NSScreen.screens.first { $0.frame.contains(NSEvent.mouseLocation) }
        guard let visible = (anchorScreen ?? pointerScreen ?? NSScreen.main)?.visibleFrame else { return }
        let x = anchorScreen == nil ? visible.maxX - PanelMetrics.width - 4 : anchor.midX - PanelMetrics.width / 2
        let y = min(anchorScreen == nil ? visible.maxY : anchor.minY, visible.maxY) - PanelMetrics.height - 2
        model.closeCamera()
        window.contentView = NSHostingView(rootView: Panel(model: model).clipShape(RoundedRectangle(cornerRadius: 12)))
        window.setFrameOrigin(NSPoint(
            x: min(max(visible.minX + 4, x), visible.maxX - PanelMetrics.width - 4),
            y: max(visible.minY, y)
        ))
        window.makeKeyAndOrderFront(nil)
        if outsideClick == nil {
            outsideClick = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] _ in
                self?.dismiss()
            }
        }
    }

    private func dismiss() {
        if let outsideClick { NSEvent.removeMonitor(outsideClick) }
        outsideClick = nil
        model.closeCamera()
        window.orderOut(nil)
        window.contentView = nil
    }

    private func updateIcon(_ values: [String:String]) {
        guard let button = item.button else { return }
        var label = "拓竹屏"
        button.image = printer
        button.title = ""
        switch Self.iconState(values: values) {
        case .printing(let current):
            if let percent = current {
                button.title = "\(percent)%"
                label += "，打印中 \(percent)%"
            } else {
                label += "，打印中"
            }
        case .paused:
            button.image = printerWithIndicator(paused: true)
            label += "，已暫停"
        case .failed:
            button.image = printerWithIndicator(paused: false)
            label += "，錯誤"
        case .idle: label += "，待機"
        case .quiet: break
        }
        button.setAccessibilityLabel(label)
    }

    enum IconState: Equatable {
        case printing(Int?), paused, failed, idle, quiet
    }

    static func iconState(values: [String:String]) -> IconState {
        switch values["gcode_state"] {
        case "RUNNING": .printing(Model.percent(in: values))
        case "PAUSE": .paused
        case "FAILED": .failed
        case "IDLE", "FINISH": .idle
        default: .quiet
        }
    }

    private func printerWithIndicator(paused: Bool) -> NSImage {
        let symbol = printer?.withSymbolConfiguration(.init(pointSize: 15, weight: .regular))
        return NSImage(size: NSSize(width: 28, height: 19), flipped: false) { rect in
            let dark = NSAppearance.currentDrawing().bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            let color = NSColor.labelColor
            if let symbol {
                let size = symbol.size
                let symbolRect = NSRect(x: 1, y: (rect.height - size.height) / 2, width: size.width, height: size.height)
                symbol.draw(in: symbolRect)
                color.setFill()
                symbolRect.fill(using: .sourceAtop)
            }
            PanelPalette.statusColor(paused: paused, dark: dark).setFill()
            NSBezierPath(ovalIn: NSRect(x: 21, y: 6, width: 7, height: 7)).fill()
            return true
        }
    }
}

#if !F101_TEST
@main enum F101Panel {
    @MainActor static func main() {
        signal(SIGPIPE, SIG_IGN)
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let center = MacPrintNotificationCenter()
        let panel = StatusPanel(model: Model(notifications: PrintFailureNotifications(center: center)))
        center.openDevice = { [weak panel] in panel?.openDevice() }
        withExtendedLifetime(panel) { app.run() }
    }
}
#endif
