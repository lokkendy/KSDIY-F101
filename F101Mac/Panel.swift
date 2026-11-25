import AppKit
import SwiftUI

let green = Color(red: 0, green: 174/255, blue: 66/255)
private func panelColor(dark: CGFloat, light: CGFloat) -> Color {
    Color(nsColor: NSColor(name: nil) { appearance in
        NSColor(white: appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? dark : light, alpha: 1)
    })
}
let card = panelColor(dark: 35/255, light: 1)
private let panelBackground = panelColor(dark: 20/255, light: 0.95)

final class Link {
    let process = Process()
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

final class Model: ObservableObject {
    @Published var lightTheme = false
    @Published var controls = DemoControls()
    @Published var mock = true
    @Published var values = [String:String]()
    @Published var notice = ""
    @Published var frame: NSImage?
    @Published var cameraNote = ""
    private var link: Link?, camera: Link?
    private var generation = UUID(), videoGeneration = UUID()
    var config = ["host":"", "serial":"", "code":"", "pin":""]
    init() { demo() }
    func clear() {
        generation = UUID()
        link = nil
        closeCamera()
        controls = DemoControls()
        values = [:]
        notice = ""
    }
    func demo() {
        clear()
        mock = true
        values = ["gcode_state":"RUNNING", "subtask_name":"校準方塊.3mf", "mc_percent":"42",
                  "mc_remaining_time":"18", "nozzle_temper":"218", "nozzle_target_temper":"220",
                  "bed_temper":"60", "bed_target_temper":"60", "chamber_temper":"32"]
    }
    func connect() {
        clear()
        mock = false
        notice = "正在連線，等待設備回報"
        let token = generation
        do {
            link = try Link("monitor", config: config) { [weak self] event in
                guard let self, token == self.generation else { return }
                if let status = event["status"] as? [String:Any] {
                    let firstReport = self.values.isEmpty
                    self.values.merge(status.mapValues { String(describing: $0) }) { _, new in new }
                    if firstReport { self.notice = "" }
                } else if let error = event["error"] as? String {
                    self.clear()
                    self.notice = error
                } else if event["closed"] != nil {
                    self.clear()
                    self.notice = "已斷線"
                } else if let message = event["message"] as? String { self.notice = message }
            }
        } catch { notice = "無法啟動區網連線" }
    }
    func closeCamera() {
        videoGeneration = UUID()
        camera = nil
        frame = nil
        cameraNote = ""
    }
    func openCamera() {
        closeCamera()
        if mock { cameraNote = "模擬鏡頭 · 測試圖"; return }
        let token = videoGeneration
        cameraNote = "正在連接 P1／A1 鏡頭"
        do {
            camera = try Link("camera", config: config) { [weak self] event in
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
    func command(_ action: String) {
        let state = values["gcode_state"]
        guard (action == "pause" && state == "RUNNING") || (action == "stop" && (state == "RUNNING" || state == "PAUSE")) else {
            notice = "設備狀態已改變或過期，請重新核對"
            return
        }
        if mock {
            values["gcode_state"] = action == "pause" ? "PAUSE" : "IDLE"
            return
        }
        do { try link?.send(action) } catch { clear(); notice = "指令傳送失敗" }
    }
}

struct Panel: View {
    @ObservedObject var model: Model
    @State private var path: [Screen] = [.home]
    private var page: Screen { path.last ?? .home }
    @State private var action = ""
    @State private var confirming = false
    @State private var expandedCamera = false
    private var state: String { model.values["gcode_state"] ?? "—" }
    private var stateText: String {
        ["RUNNING":"打印中","PAUSE":"已暫停","IDLE":"待機","FINISH":"已完成","FAILED":"失敗","PREPARE":"準備中"][state] ?? state
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(page.rawValue).font(.title2.bold())
                Spacer()
                Text(model.mock ? "模擬模式" : "區網模式").foregroundStyle(.secondary)
                if page == .home {
                    Button("多台總覽") { go(.fleet) }; Button("拓竹雲監控") { go(.cloud) }
                    Button("新增打印機") { go(.connection) }
                } else if path.count > 1 {
                    Button("返回") { path.removeLast(); configureCamera() }
                }
            }
            if page == .connection { SettingsForm(model: model) { go(.home) } }
            else if page == .control { ControlPage(model: model, stateText: stateText, confirm: ask) }
            else if page == .camera {
                HStack(alignment: .top, spacing: 10) {
                    ZStack {
                        Color.black
                        if let img = model.frame { Image(nsImage: img).resizable().scaledToFit() }
                        else {
                            VStack {
                                if model.mock { Image(systemName: "cube.transparent").font(.system(size: 74)).foregroundColor(green) }
                                Text(model.cameraNote)
                            }.foregroundColor(.white)
                        }
                    }.frame(height: expandedCamera ? 340 : 224).cornerRadius(10)
                    if !expandedCamera {
                        VStack(alignment: .leading, spacing: 10) {
                            PageCard(title: model.values["subtask_name"] ?? "—") {
                                Text(stateText).foregroundColor(green)
                                Text((model.values["mc_percent"] ?? "—") + "%").font(.title.bold())
                                ProgressView(value: Double(model.values["mc_percent"] ?? "") ?? 0, total: 100).tint(green)
                                Text("剩餘 " + (model.values["mc_remaining_time"] ?? "—") + " 分鐘")
                            }
                            HStack { tile("噴嘴","nozzle"); tile("熱床","bed") }
                            HStack {
                                Button(state == "PAUSE" ? "繼續" : "暫停") {
                                    if state == "PAUSE", model.mock { model.values["gcode_state"] = "RUNNING" } else { ask("pause") }
                                }.disabled(state != "RUNNING" && !(model.mock && state == "PAUSE"))
                                Button("停止") { ask("stop") }.disabled(!["RUNNING","PAUSE"].contains(state))
                            }
                        }.frame(width: 250)
                    }
                }
                if !model.notice.isEmpty { Text(model.notice).foregroundStyle(.secondary) }
                HStack {
                    ForEach(["拍照","錄製","燈光"], id: \.self) { item in Button(item) {}.disabled(true) }
                    Button(expandedCamera ? "退出全屏" : "全屏") { expandedCamera.toggle() }
                    Spacer()
                    Button("重新開啟鏡頭") { model.openCamera() }
                }
            } else if page == .home {
                HStack(alignment: .top, spacing: 12) {
                    VStack(alignment: .leading, spacing: 12) {
                        Text(model.mock ? "模擬打印機" : "區網打印機").font(.headline)
                        Label(stateText, systemImage: "circle.fill").foregroundColor(green)
                        Spacer()
                        Image(systemName: "printer").font(.system(size: 46)).foregroundStyle(.secondary)
                            .frame(maxWidth: .infinity)
                        Spacer()
                        Button("設備與 AMS") { go(.device) }
                        Button("區網連線設定") { go(.connection) }
                    }.padding(12).frame(width: 168).background(card).cornerRadius(9)
                    VStack(alignment: .leading, spacing: 10) {
                VStack(alignment: .leading, spacing: 10) {
                    HStack { Text("目前打印").font(.headline); Spacer(); Button("即時監控") { go(.camera) } }
                    Text(model.values["subtask_name"] ?? model.values["gcode_file"] ?? "—").font(.headline).lineLimit(1)
                    HStack {
                        Text((model.values["mc_percent"] ?? "—") + "%").font(.title.bold()).foregroundColor(green)
                        Spacer()
                        if let time = model.values["mc_remaining_time"] { Text("剩餘 " + time + " 分鐘") }
                    }
                    ProgressView(value: min(100, max(0, Double(model.values["mc_percent"] ?? "") ?? 0)), total: 100).tint(green)
                }.padding().background(card).cornerRadius(10)
                HStack {
                    tile("噴嘴", "nozzle"); tile("熱床", "bed"); tile("腔體", "chamber")
                }
                HStack {
                    ForEach([Screen.camera,.files,.control,.filament,.settings], id: \.self) { target in
                        Button { go(target) } label: {
                            VStack { Image(systemName: target.icon); Text(target.rawValue) }.frame(maxWidth: .infinity)
                        }.accessibilityLabel(target.rawValue)
                    }
                }
                }
                }.frame(height: 292)
                if !model.notice.isEmpty { Text(model.notice).foregroundStyle(.secondary) }
            } else { LibraryPage(model: model, page: page, go: go).id(page).frame(height: 320) }
            Divider()
            HStack {
                ForEach(Screen.tabs, id: \.self) { target in
                    Button {
                        path = [target]; configureCamera()
                    } label: {
                        Label(target.rawValue, systemImage: target.icon).frame(maxWidth: .infinity)
                    }.buttonStyle(.plain).foregroundColor(path.first == target ? green : .secondary)
                    .accessibilityLabel("分頁：" + target.rawValue)
                }
            }
        }.padding(page == .control ? 12 : 18).frame(width: page == .control ? 590 : 662).background(panelBackground).preferredColorScheme(model.lightTheme ? .light : .dark)
        .onDisappear { model.closeCamera(); path = [.home]; confirming = false }
        .alert(action == "pause" ? "暫停目前任務？" : "停止目前任務？", isPresented: $confirming) {
            Button("取消", role: .cancel) {}
            Button(action == "pause" ? "確認暫停" : "確認停止", role: .destructive) { model.command(action) }
        } message: { Text(model.mock ? "此操作只會變更模擬資料。" : "確認後會向已連線的打印機發送指令。") }
    }
    func go(_ target: Screen) {
        if target == .home { path = [.home] } else if target != page { path.append(target) }
        configureCamera()
    }
    func configureCamera() {
        expandedCamera = false
        if page == .camera { model.openCamera() } else { model.closeCamera() }
    }
    func ask(_ choice: String) { action = choice; confirming = true }
    func tile(_ title: String, _ key: String) -> some View {
        Button { go(.control) } label: { VStack(alignment: .leading, spacing: 5) {
            Text(title).foregroundStyle(.secondary)
            Text((model.values[key + "_temper"] ?? "—") + " °C").font(.headline)
            if let target = model.values[key + "_target_temper"] { Text("目標 " + target + "°").font(.caption) }
        }.frame(maxWidth: .infinity, minHeight: 62, alignment: .topLeading).padding(10).background(card).cornerRadius(8) }.buttonStyle(.plain).help("開啟控制頁")
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
        VStack(alignment: .leading, spacing: 10) {
            HStack { Text("LAN MQTT").font(.headline); Spacer(); Button("掃描區網打印機") {}.disabled(true) }
            TextField("打印機區網 IPv4", text: $host)
            TextField("設備序號", text: $serial)
            SecureField("LAN 存取碼", text: $code)
            TextField("已核對的 TLS 憑證 SHA-256 指紋", text: $pin)
            Text("從可信管道核對憑證指紋。連線資料僅保留至 App 結束。").font(.caption)
            HStack {
                Button("使用模擬資料") { model.demo(); done() }
                Spacer()
                Button("連線") {
                    model.config = ["host":host, "serial":serial, "code":code, "pin":pin]
                    model.connect(); done()
                }.disabled(host.isEmpty || serial.isEmpty || code.isEmpty || pin.isEmpty)
            }
        }.onAppear { host = model.config["host"]!; serial = model.config["serial"]!; code = model.config["code"]!; pin = model.config["pin"]! }
    }
}

@main struct F101Panel: App {
    @StateObject var model = Model()
    init() { signal(SIGPIPE, SIG_IGN) }
    var body: some Scene {
        MenuBarExtra("拓竹屏", systemImage: "printer") { Panel(model: model) }.menuBarExtraStyle(.window)
    }
}
