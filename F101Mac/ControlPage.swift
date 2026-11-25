import SwiftUI

struct DemoControls {
    var positions = [0.0, 0.0, 0.0]
    var speed = "標準"
    var fans = [0, 0, 0]
    var extruded = 0
    var feedback = ""
}

struct ControlPage: View {
    @ObservedObject var model: Model
    let stateText: String
    let confirm: (String) -> Void
    @State private var step = 0.1
    private let axes = ["X", "Y", "Z"]
    private var state: String { model.values["gcode_state"] ?? "" }
    private var manual: Bool { model.mock && ["IDLE", "FINISH", "FAILED"].contains(state) }
    private var hot: Bool { (Double(model.values["nozzle_temper"] ?? "") ?? 0) >= 170 }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(model.mock ? "XYZ · 溫度" : "XYZ · 溫度　區網僅開放暫停與停止").foregroundStyle(.secondary)
            HStack(alignment: .top, spacing: 10) {
                VStack(alignment: .leading, spacing: 10) {
                    section("XYZ 移動控制") {
                        Picker("步距", selection: $step) {
                            ForEach([0.1, 1, 10], id: \.self) { distance in
                                Text("\(distance.formatted()) mm").tag(distance)
                            }
                        }.pickerStyle(.segmented).disabled(!model.mock)
                        HStack(spacing: 20) {
                            VStack(spacing: 6) {
                                jog(1, 1, "↑")
                                HStack(spacing: 5) {
                                    jog(0, -1, "←")
                                    Button { home() } label: { Image(systemName: "house.fill").frame(width: 30, height: 25) }
                                        .accessibilityLabel("中心回零").tint(green)
                                    jog(0, 1, "→")
                                }
                                jog(1, -1, "↓")
                            }
                            VStack { jog(2, 1, "Z+"); jog(2, -1, "Z−") }
                        }.frame(maxWidth: .infinity).padding(.vertical, 4).disabled(!manual)
                        HStack {
                            ForEach(0..<3) { i in
                                Text(axes[i] + "  " + (model.mock ? model.controls.positions[i].formatted() : "—"))
                                    .frame(maxWidth: .infinity)
                            }
                        }.monospacedDigit()
                    }
                    HStack {
                        Button("回零", action: home).disabled(!manual)
                        Button("自動調平") { model.controls.feedback = "模擬調平完成" }.disabled(!manual)
                        Button(state == "PAUSE" ? "繼續" : "暫停") {
                            if state == "PAUSE", model.mock { model.values["gcode_state"] = "RUNNING" }
                            else { confirm("pause") }
                        }.disabled(state != "RUNNING" && !(model.mock && state == "PAUSE"))
                            .buttonStyle(.borderedProminent).tint(green)
                        Button("停止") { confirm("stop") }.disabled(!["RUNNING", "PAUSE"].contains(state))
                            .buttonStyle(.borderedProminent).tint(.red)
                    }
                    if model.mock && !manual { Text("待機後可移動、回零及調平。").foregroundStyle(.secondary) }
                    if !model.controls.feedback.isEmpty && model.mock { Text(model.controls.feedback) }
                    if !model.notice.isEmpty { Text(model.notice) }
                }.frame(width: 292)
                ScrollView {
                    VStack(spacing: 6) {
                        section("擠出控制") {
                            HStack {
                                Button("退料 10 mm") { extrude(-10) }
                                Button("進料 10 mm") { extrude(10) }
                            }.disabled(!manual || !hot)
                            Text("待機且噴嘴達 170 °C 可進退料").font(.caption).foregroundStyle(.secondary)
                        }
                        temperature("噴頭", "nozzle", [180, 200, 220, 240], 300)
                        temperature("熱床", "bed", [40, 60, 80, 100], 100)
                        HStack {
                            Text("目前狀態").font(.headline)
                            Spacer()
                            Text(stateText).foregroundColor(green)
                        }.padding(8).background(card).cornerRadius(9)
                        section("打印速度") {
                            HStack {
                                ForEach(["靜音","標準","運動","狂暴"], id: \.self) { name in
                                    Button(name) { model.controls.speed = name }
                                        .tint(model.mock && name == model.controls.speed ? green : nil)
                                }
                            }.disabled(!model.mock)
                            if model.mock { Text(model.controls.speed).font(.caption) }
                        }
                        section("風扇") {
                            ForEach(Array(["模型風扇", "輔助風扇", "機箱風扇"].enumerated()), id: \.offset) { i, name in
                                HStack { Text(name); Spacer(); Text(model.mock ? "\(model.controls.fans[i])%" : "—") }
                                HStack(spacing: 4) {
                                    ForEach([0,25,50,75,100], id: \.self) { pct in
                                        Button("\(pct)%") { model.controls.fans[i] = pct }
                                    }
                                }.controlSize(.small).disabled(!model.mock)
                            }
                        }
                    }
                }
            }.frame(height: 318)
        }
    }

    private func section<Content: View>(_ title: String, @ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.headline)
            content()
        }.padding(8).frame(maxWidth: .infinity, alignment: .leading).background(card).cornerRadius(9)
    }

    private func jog(_ axis: Int, _ sign: Double, _ title: String) -> some View {
        Button { model.controls.positions[axis] += sign * step } label: { Text(title).frame(width: 30, height: 25) }
            .accessibilityLabel(axes[axis] + (sign > 0 ? " 正向" : " 負向"))
    }
    private func home() {
        model.controls.positions = [0,0,0]
        model.controls.feedback = "模擬 XYZ 已回零"
    }
    private func extrude(_ amount: Int) {
        model.controls.extruded += amount
        model.controls.feedback = "模擬送料淨位移　\(model.controls.extruded) mm"
    }
    private func temperature(_ name: String, _ key: String, _ presets: [Int], _ maximum: Int) -> some View {
        let target = key + "_target_temper"
        let value = model.values[target].flatMap(Double.init)
        return VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Text(name + " °C").font(.headline)
                Spacer()
                Button("−") { adjust(-5, value, target, maximum) }.accessibilityLabel(name + "降溫")
                Text(value?.formatted() ?? "—").monospacedDigit().frame(minWidth: 28)
                Button("+") { adjust(5, value, target, maximum) }.accessibilityLabel(name + "升溫")
            }
            HStack(spacing: 5) {
                ForEach(presets, id: \.self) { temp in
                    Button { setTemperature(temp, key, target) } label: {
                        Text(String(temp)).frame(maxWidth: .infinity)
                    }
                }
            }
        }.padding(8).background(card).cornerRadius(9).disabled(!model.mock)
    }
    private func adjust(_ delta: Int, _ value: Double?, _ target: String, _ maximum: Int) {
        let temp = min(maximum, max(0, Int(value ?? 0) + delta))
        setTemperature(temp, target.hasPrefix("bed") ? "bed" : "nozzle", target)
    }
    private func setTemperature(_ temp: Int, _ key: String, _ target: String) {
        guard model.mock else { return }
        model.values[target] = String(temp)
        model.values[key + "_temper"] = String(temp)
    }
}
