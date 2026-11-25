import SwiftUI

enum Screen: String, CaseIterable {
    case home = "設備", files = "設備儲存", history = "打印記錄", camera = "相機", settings = "設定", filament = "耗材"
    case control = "控制", connection = "新增打印機", device = "設備與 AMS", cloud = "拓竹雲監控", fleet = "多台總覽", wifi = "Wi-Fi 網絡", about = "關於"
    static let tabs: [Screen] = [.home, .files, .history, .camera, .settings, .filament]
    var icon: String {
        switch self {
        case .home: "house"
        case .files: "folder"
        case .history: "clock.arrow.circlepath"
        case .camera: "video"
        case .settings: "gearshape"
        case .filament: "circle.grid.2x2"
        case .control: "slider.horizontal.3"
        default: "chevron.right"
        }
    }
}

struct PageCard<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            content
        }.padding(12).frame(maxWidth: .infinity, alignment: .leading).background(card).cornerRadius(9)
    }
}

struct LibraryPage: View {
    @ObservedObject var model: Model
    let page: Screen
    let go: (Screen) -> Void
    @State private var slot = 0
    @State private var source = 1
    @State private var selected = false
    @State private var version = false
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                switch page {
                case .files:
                    Picker("來源", selection: $source) {
                        Text("快取").tag(0); Text("打印機 TF 卡").tag(1); Text("本機 TF 卡").tag(2)
                    }.pickerStyle(.segmented).onChange(of: source) { selected = false }
                    HStack(alignment: .top) {
                        PageCard(title: ["快取 › 打印文件", "打印機 TF 卡", "本機 TF 卡"][source]) {
                            if model.mock {
                                Button { selected = true } label: { Label("校準方塊.3mf", systemImage: "doc") }
                                Button("刷新") { selected = false }
                            } else { Text("此版本未接入 FTPS 或本機 TF 卡。") }
                        }
                        PageCard(title: selected && model.mock ? "校準方塊.3mf" : "檔案詳情") {
                            Image(systemName: "photo").font(.largeTitle).foregroundStyle(.secondary)
                                .frame(maxWidth: .infinity, minHeight: 80)
                            Text(selected && model.mock ? "3MF · 模擬檔案" : "請選擇檔案")
                            unavailable(source == 2 ? ["上傳到打印機"] : ["開始打印","刪除"])
                        }.frame(width: 224)
                    }
                case .history:
                    PageCard(title: "打印記錄") {
                        if model.mock {
                            Button { selected.toggle() } label: { Label("校準方塊.3mf", systemImage: "clock") }
                            if selected { Text("模擬記錄 · 已完成") }
                        } else { Text("未接入打印記錄來源") }
                    }
                case .filament:
                    HStack(alignment: .top) {
                        PageCard(title: "AMS 槽位") {
                            ForEach(0..<4) { i in
                                Button { slot = i } label: {
                                    HStack { Circle().fill(model.mock ? [Color.green,.orange,.blue,.gray][i] : Color.gray).frame(width: 10); Text("A\(i+1)"); Spacer(); Text(model.mock ? ["PLA","PETG","PLA","空槽位"][i] : "—") }
                                }.disabled(!model.mock)
                            }
                        }
                        PageCard(title: "耗材詳情") {
                            Text(model.mock ? "A\(slot+1) · " + ["PLA","PETG","PLA","空槽位"][slot] : "未接入 AMS 資料")
                            Text(model.mock && slot < 3 ? "剩餘 " + ["76%","48%","95%"][slot] : "剩餘 —")
                            unavailable(["退料","進料"])
                        }
                    }
                case .device:
                    PageCard(title: "系統") { Text(model.mock ? "模擬打印機" : "區網打印機"); unavailable(["同步狀態","關蜂鳴器","全校準"]) }
                    PageCard(title: "燈光") { unavailable(["機箱燈 開","機箱燈 關","工作燈 開","工作燈 關"]) }
                    PageCard(title: "目標溫度") { Button("噴頭與熱床") { go(.control) } }
                    PageCard(title: "風扇與 HMS") { Text(model.mock ? "模擬設備 · 無 HMS 告警" : "未接入診斷資料"); Button("查看風扇") { go(.control) } }
                    PageCard(title: "AMS 槽位") { Button("耗材管理") { go(.filament) }; unavailable(["AMS 復位","延時攝影"]) }
                case .settings:
                    HStack(alignment: .top, spacing: 10) {
                        VStack(spacing: 8) {
                            PageCard(title: "Wi-Fi 網絡") {
                                Button("掃描附近 Wi-Fi") { go(.wifi) }
                                Text("由 macOS 管理").foregroundStyle(.secondary)
                                Button("區網連線設定") { go(.connection) }
                            }
                            PageCard(title: "設備資訊") {
                                Text("F101 Mac · macOS")
                                Button("設備與 AMS") { go(.device) }
                            }
                        }
                        VStack(spacing: 8) {
                            PageCard(title: "顯示") {
                                Picker("主題", selection: $model.lightTheme) {
                                    Text("深色").tag(false); Text("淺色").tag(true)
                                }.pickerStyle(.segmented)
                                Text("繁體中文 · 橫屏")
                                Text("螢幕亮度、方向及音量由 macOS 管理。").font(.caption)
                                unavailable(["語音提示","觸控回饋音"])
                            }
                            PageCard(title: "關於") {
                                Button("關於本機與應用") { go(.about) }
                                Button("結束 App") { model.clear(); NSApp.terminate(nil) }
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
            }
        }.frame(maxWidth: .infinity)
    }
    private func unavailable(_ titles: [String]) -> some View {
        HStack {
            ForEach(titles, id: \.self) { name in Button(name) {}.disabled(true) }
        }.help("本版未開放此操作")
    }
}
