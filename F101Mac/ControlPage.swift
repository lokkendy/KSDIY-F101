import SwiftUI

struct DemoControls {
    var positions = [0.0, 0.0, 0.0]
    var speed = "標準"
    var fans = [0, 0, 0]
    var extruded = 0
    var feedback = ""
}

struct PrintButtons: View {
    @ObservedObject var model: Model
    let height: CGFloat
    let confirm: (PrintAction) -> Void

    var body: some View {
        HStack(spacing: 6) {
            Button(model.state == "PAUSE" && model.mock ? "繼續" : "暫停") {
                if model.canResumeDemo { model.resumeDemo() }
                else { confirm(.pause) }
            }
            .panelButtonStyle(PanelButtonStyle(height: height, expands: true, tone: .primary))
            .disabled(!model.canSend(.pause) && !model.canResumeDemo)
            Button("停止") { confirm(.stop) }
                .panelButtonStyle(PanelButtonStyle(height: height, expands: true, tone: .destructiveOutline))
                .disabled(!model.canSend(.stop))
        }
    }
}

struct ControlPage: View {
    @ObservedObject var model: Model
    @Environment(\.panelPalette) private var palette
    let confirm: (PrintAction) -> Void
    @State private var step = 0.1
    private let axes = ["X", "Y", "Z"]
    private var manual: Bool { model.mock && ["IDLE", "FINISH", "FAILED"].contains(model.state) }
    private var hot: Bool { (Double(model.values["nozzle_temper"] ?? "") ?? 0) >= 170 }

    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 6) {
                motion.frame(width: (geometry.size.width - 6) * 0.58)
                controls(rowHeight: (geometry.size.height - 8 * 6) / 9)
            }
        }
    }

    private var motion: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                Text("步距").fontWeight(.semibold)
                HStack(spacing: 3) {
                    ForEach([0.1, 1, 10], id: \.self) { distance in
                        Button(distance.formatted() + (distance == 10 ? " mm" : "")) { step = distance }
                            .panelButtonStyle(PanelButtonStyle(height: 20, expands: true, tone: step == distance ? .segment : .neutral))
                            .accessibilityLabel("步距 \(distance.formatted()) 毫米")
                            .accessibilityAddTraits(step == distance ? .isSelected : [])
                    }
                }
                .padding(2)
                .background(palette.highlight, in: RoundedRectangle(cornerRadius: 6))
                .disabled(!model.mock)
                .help(Model.lanControlHelp)
            }
            GeometryReader { geometry in
                let keyWidth = (geometry.size.width - 24) / 4
                let keyHeight = (geometry.size.height - 10) / 3
                HStack(spacing: 14) {
                    VStack(spacing: 5) {
                        jog(1, 1, icon: "arrow.up").frame(width: keyWidth, height: keyHeight)
                        HStack(spacing: 5) {
                            jog(0, -1, icon: "arrow.left").frame(width: keyWidth, height: keyHeight)
                            Button(action: home) { Image(systemName: "house") }
                                .panelButtonStyle(PanelButtonStyle(height: nil, expands: true, tone: .selected))
                                .frame(width: keyWidth, height: keyHeight)
                                .accessibilityLabel("中心回零")
                            jog(0, 1, icon: "arrow.right").frame(width: keyWidth, height: keyHeight)
                        }
                        jog(1, -1, icon: "arrow.down").frame(width: keyWidth, height: keyHeight)
                    }
                    VStack(spacing: 5) {
                        jog(2, 1).frame(width: keyWidth, height: keyHeight)
                        jog(2, -1).frame(width: keyWidth, height: keyHeight)
                        Spacer(minLength: keyHeight)
                    }
                }.disabled(!manual).help(model.mock ? "待機後可移軸" : Model.lanControlHelp)
            }
            HStack(spacing: 4) {
                ForEach(0..<3) { axis in
                    let value = model.mock ? model.controls.positions[axis].formatted(.number.precision(.fractionLength(1))) : Model.missing
                    Text(axes[axis] + " " + value).fontWeight(.semibold)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                Text(model.mock ? (manual ? (hot ? "可移軸及進退料" : "擠出需達 170 °C") : "打印時停用移軸") : "區網僅限暫停／停止")
                    .font(.system(size: 11)).foregroundStyle(palette.secondary)
            }
            if !feedback.isEmpty {
                Text(feedback)
                    .font(.system(size: 10)).foregroundStyle(palette.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack(spacing: 6) {
                HStack(spacing: 6) {
                    Button("回零", action: home)
                    Button("自動調平") { model.controls.feedback = "模擬調平完成" }
                }
                .panelButtonStyle(PanelButtonStyle(height: 28, expands: true))
                .disabled(!manual)
                .help(model.mock ? "待機後可回零及調平" : Model.lanControlHelp)
                PrintButtons(model: model, height: 28, confirm: confirm)
            }
        }.panelCard()
    }

    private var feedback: String {
        if !model.notice.isEmpty { return model.notice }
        return model.mock ? model.controls.feedback : ""
    }

    private func controls(rowHeight: CGFloat) -> some View {
        VStack(spacing: 6) {
            row("擠出", rowHeight) {
                HStack(spacing: 3) {
                    Button("退料 10") { extrude(-10) }.accessibilityLabel("退料 10 毫米")
                    Button("進料 10") { extrude(10) }.accessibilityLabel("進料 10 毫米")
                }
                .panelButtonStyle(PanelButtonStyle(height: 20, expands: true))
                .disabled(!manual || !hot)
                .help(model.mock ? "待機且噴頭達 170 °C 可進退料" : Model.lanControlHelp)
            }
            temperature("噴頭", "nozzle", [180, 200, 220, 240], 300, rowHeight)
            temperature("熱床", "bed", [40, 60, 80, 100], 100, rowHeight)
            row("速度", rowHeight) {
                HStack(spacing: 3) {
                    ForEach(["靜音", "標準", "運動", "狂暴"], id: \.self) { speed in
                        choice(speed, label: "打印速度 \(speed)", selected: model.mock && speed == model.controls.speed) {
                            model.controls.speed = speed
                        }
                    }
                }.disabled(!model.mock).help(Model.lanControlHelp)
            }
            ForEach(Array(["模型", "輔助", "機箱"].enumerated()), id: \.offset) { index, fan in
                row(fan, rowHeight) {
                    HStack(spacing: 3) {
                        ForEach([0, 25, 50, 75, 100], id: \.self) { percent in
                            choice("\(percent)", label: "\(fan)風扇 \(percent)%", selected: model.mock && model.controls.fans[index] == percent) {
                                model.controls.fans[index] = percent
                            }
                        }
                    }.disabled(!model.mock).help(Model.lanControlHelp)
                }
            }
        }
    }

    private func row<Content: View>(_ name: String, _ height: CGFloat, @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 6) {
            Text(name).fontWeight(.semibold).frame(width: 28, alignment: .leading)
                .accessibilityHidden(name.isEmpty)
            content()
        }
        .padding(.horizontal, 8)
        .frame(height: height)
        .background(palette.card, in: RoundedRectangle(cornerRadius: 8))
    }

    private func choice(_ title: String, label: String, selected: Bool, action: @escaping () -> Void) -> some View {
        Button(title, action: action)
            .panelButtonStyle(PanelButtonStyle(height: 20, expands: true, tone: selected ? .selected : .neutral))
            .accessibilityLabel(label)
            .accessibilityAddTraits(selected ? .isSelected : [])
    }

    private func jog(_ axis: Int, _ direction: Double, icon: String? = nil) -> some View {
        Button { model.controls.positions[axis] += direction * step } label: {
            if let icon { Image(systemName: icon).fontWeight(.semibold) }
            else { Text(direction > 0 ? "Z+" : "Z−").fontWeight(.semibold) }
        }
        .panelButtonStyle(PanelButtonStyle(height: nil, expands: true))
        .accessibilityLabel(axes[axis] + (direction > 0 ? " 正向" : " 負向"))
    }

    private func home() {
        model.controls.positions = [0, 0, 0]
        model.controls.feedback = "模擬 XYZ 已回零"
    }

    private func extrude(_ amount: Int) {
        model.controls.extruded += amount
        model.controls.feedback = "模擬送料淨位移 \(model.controls.extruded) mm"
    }

    private func temperature(_ name: String, _ key: String, _ presets: [Int], _ maximum: Int, _ height: CGFloat) -> some View {
        let targetKey = key + "_target_temper"
        let target = model.values[targetKey].flatMap(Double.init)
        let current = model.values[key + "_temper"].flatMap(Double.init)
        return Group {
            row(name, height) {
                Button("−") { adjust(-5, target, key, maximum) }.accessibilityLabel(name + "降溫")
                    .disabled(!model.mock).help(Model.lanControlHelp)
                Text(degrees(target)).fontWeight(.bold).frame(minWidth: 35)
                    .accessibilityLabel(name + "目標 " + degrees(target))
                Button("+") { adjust(5, target, key, maximum) }.accessibilityLabel(name + "升溫")
                    .disabled(!model.mock).help(Model.lanControlHelp)
                Spacer(minLength: 0)
                Text(degrees(current)).foregroundStyle(palette.secondary)
                    .accessibilityLabel(name + "目前 " + degrees(current))
            }
            row("", height) {
                HStack(spacing: 3) {
                    ForEach(presets, id: \.self) { temp in
                        choice("\(temp)", label: "\(name)設定 \(temp) 度", selected: target == Double(temp)) {
                            setTemperature(temp, key)
                        }
                    }
                }.disabled(!model.mock).help(Model.lanControlHelp)
            }
        }
    }

    private func degrees(_ temperature: Double?) -> String {
        guard let temperature, temperature.isFinite else { return Model.missing }
        return temperature.formatted(.number.precision(.fractionLength(0))) + "°"
    }

    private func adjust(_ delta: Int, _ value: Double?, _ key: String, _ maximum: Int) {
        guard let value, value.isFinite else { return }
        setTemperature(Int(min(Double(maximum), max(0, value + Double(delta)))), key)
    }

    private func setTemperature(_ temperature: Int, _ key: String) {
        guard model.mock else { return }
        model.values[key + "_target_temper"] = String(temperature)
        model.values[key + "_temper"] = String(temperature)
    }
}
