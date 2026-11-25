import AppKit
import ApplicationServices

/// Requires a fresh simulated app launched with `-panelAppearance 深色`.
@main struct PanelFocusAcceptance {
    private enum Key: Int {
        case tab = 48
        case space = 49
        case escape = 53
        case enter = 36
    }

    private let pid: pid_t
    private let directory: URL
    private let app: AXUIElement
    private let panel: AXUIElement

    static func main() throws {
        let args = CommandLine.arguments
        guard args.count == 3, let pid = pid_t(args[1]), AXIsProcessTrusted() else {
            fail("Usage: panel-focus <app-pid> <capture-directory> (requires Accessibility permission)")
        }
        let directory = URL(fileURLWithPath: args[2], isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let app = AXUIElementCreateApplication(pid)
        guard element(app, "AXFocusedWindow") == nil else { fail("Expected a fresh app with the panel closed") }
        let status = wait("menu-bar item") {
            element(app, "AXExtrasMenuBar").flatMap { children($0).first }
        }
        guard AXUIElementPerformAction(status, kAXPressAction as CFString) == .success else {
            fail("Could not open the menu-bar panel")
        }
        let panel = wait("the app's focused panel window") {
            element(app, "AXFocusedWindow").flatMap { label($0) == "拓竹屏" ? $0 : nil }
        }
        let test = Self(pid: pid, directory: directory, app: app, panel: panel)
        try test.run()
    }

    private func run() throws {
        require(find("在線 · 模擬") != nil, "Expected simulated mode before sending any keys")
        require(try captureFocus("panel-open") == 0, "A visible focus ring appeared before Tab")
        print("Open: key window ready, zero blue focus-ring pixels")

        let expected = ["設備", "存儲", "記錄", "相機", "設置", "耗材"]
        var visited: [String] = []
        var contentSeen = false
        for _ in 0..<30 {
            let button = tab()
            let name = Self.label(button)
            if Self.isNavigation(button) {
                if visited.isEmpty {
                    require(try captureFocus("tab-navigation") > 0, "Navigation focus ring was not visible")
                }
                visited.append(name)
            } else if name == "實時監控" {
                contentSeen = true
            }
            if visited.count == expected.count && contentSeen { break }
        }
        require(visited == expected && contentSeen, "Unexpected Tab sequence. Navigation \(visited), content \(contentSeen)")
        print("Tab: \(visited.joined(separator: " → "))，亦經過實時監控")

        focus("相機", navigation: true)
        activate(.space, focused: "相機")
        awaitView("模擬鏡頭測試圖")
        focus("全屏")
        require(try captureFocus("tab-camera-fullscreen") > 0, "Camera content focus ring was not visible")
        activate(.space, focused: "全屏")
        awaitView("退出全屏", removing: "進度 42%")
        _ = try captureFocus("camera-fullscreen")
        print("Space: camera content expanded and task details were hidden")
        focus("退出全屏")
        activate(.space, focused: "退出全屏")
        awaitView("進度 42%", removing: "退出全屏")

        openPauseConfirmation()
        focus("確認暫停")
        send(.escape)
        awaitCancelled()
        print("Esc: cancelled even while 確認暫停 had focus")

        for key in [Key.space, .enter] {
            openPauseConfirmation()
            focus("取消")
            let name = key == .space ? "Space" : "Return"
            require(try captureFocus("confirmation-cancel-\(name.lowercased())") > 0,
                    "Confirmation cancellation focus ring was not visible")
            activate(key, focused: "取消")
            awaitCancelled()
            print("\(name): activated the focused 取消 button; printing stayed at 42%")
        }
        print("panel-focus: PASS")
    }

    private func openPauseConfirmation() {
        focus("暫停")
        activate(.space, focused: "暫停")
        awaitView("確認暫停")
        require(find("取消") != nil, "Could not find the confirmation cancellation button")
    }

    private func awaitCancelled() {
        awaitView("打印中", removing: "確認暫停")
        require(find("進度 42%") != nil, "Simulated printing progress changed after cancelling")
    }

    private func awaitView(_ appearing: String, removing: String? = nil) {
        let _: AXUIElement = Self.wait("view \(appearing)") {
            guard let visible = find(appearing), removing.map({ find($0) == nil }) ?? true else { return nil }
            return visible
        }
    }

    private func find(_ label: String) -> AXUIElement? {
        Self.find(panel) { Self.label($0) == label }
    }

    private func focus(_ title: String, navigation: Bool = false) {
        if let focused = Self.element(app, "AXFocusedUIElement"),
           Self.label(focused) == title && Self.isNavigation(focused) == navigation { return }
        var trace: [String] = []
        for _ in 0..<30 {
            let focused = tab()
            trace.append(Self.label(focused))
            if Self.label(focused) == title && Self.isNavigation(focused) == navigation { return }
        }
        Self.fail("Could not Tab to \(title); observed \(trace)")
    }

    private func tab() -> AXUIElement {
        let previous = Self.element(app, "AXFocusedUIElement")
        send(.tab)
        return Self.wait("Tab to change the focused button") {
            guard let focused = Self.element(app, "AXFocusedUIElement"),
                  Self.attribute(focused, "AXRole") as? String == "AXButton",
                  previous.map({ !CFEqual($0, focused) }) ?? true else { return nil }
            return focused
        }
    }

    private func activate(_ key: Key, focused title: String) {
        require(!title.hasPrefix("確認"), "This test must never activate a destructive confirmation button")
        require(Self.element(app, "AXFocusedUIElement").map(Self.label) == title,
                "Refusing to send activation to a button other than \(title)")
        send(key)
    }

    private func send(_ key: Key) {
        ensureKeyWindow()
        let code = CGKeyCode(key.rawValue)
        CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true)?.postToPid(pid)
        CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false)?.postToPid(pid)
    }

    private func ensureKeyWindow() {
        _ = Self.wait("拓竹屏 to be the app's key window") {
            Self.element(app, "AXFocusedWindow").flatMap { CFEqual($0, panel) ? $0 : nil }
        }
    }

    private static func attribute(_ item: AXUIElement, _ key: String) -> CFTypeRef? {
        var result: CFTypeRef?
        return AXUIElementCopyAttributeValue(item, key as CFString, &result) == .success ? result : nil
    }

    private static func element(_ item: AXUIElement, _ key: String) -> AXUIElement? {
        guard let result = attribute(item, key), CFGetTypeID(result) == AXUIElementGetTypeID() else { return nil }
        return (result as! AXUIElement)
    }

    private static func children(_ item: AXUIElement) -> [AXUIElement] {
        attribute(item, "AXChildren") as? [AXUIElement] ?? []
    }

    private static func label(_ item: AXUIElement) -> String {
        for key in ["AXTitle", "AXDescription", "AXValue"] {
            if let value = attribute(item, key) as? String, !value.isEmpty { return value }
        }
        return ""
    }

    private static func isNavigation(_ item: AXUIElement) -> Bool {
        (attribute(item, "AXIdentifier") as? String)?.hasPrefix("navigation.") == true
    }

    private static func find(_ item: AXUIElement, where matches: (AXUIElement) -> Bool) -> AXUIElement? {
        var pending = [item]
        var visited: Set<AXUIElement> = []
        while let current = pending.popLast(), visited.count < 1200 {
            guard visited.insert(current).inserted else { continue }
            if matches(current) { return current }
            pending.append(contentsOf: children(current).reversed())
        }
        return nil
    }

    private static func wait<Value>(_ description: String, _ probe: () -> Value?) -> Value {
        let deadline = ProcessInfo.processInfo.systemUptime + 4
        repeat {
            if let value = probe() { return value }
            Thread.sleep(forTimeInterval: 0.04)
        } while ProcessInfo.processInfo.systemUptime < deadline
        fail("Timed out waiting for \(description)")
    }

    private func captureFocus(_ filename: String) throws -> Int {
        ensureKeyWindow()
        let capture = Process()
        let stdout = Pipe()
        capture.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        capture.arguments = ["orca", "computer", "get-app-state", "--app", "pid:\(pid)", "--screenshot-scale", "1", "--json"]
        capture.standardOutput = stdout
        try capture.run()
        let data = stdout.fileHandleForReading.readDataToEndOfFile()
        capture.waitUntilExit()
        guard capture.terminationStatus == 0,
              let response = try JSONSerialization.jsonObject(with: data) as? [String:Any],
              let result = response["result"] as? [String:Any],
              let screenshot = result["screenshot"] as? [String:Any],
              let source = screenshot["path"] as? String,
              screenshot["width"] as? Int == 720, screenshot["height"] as? Int == 400
        else { Self.fail("Could not capture \(filename): \(String(decoding: data, as: UTF8.self).prefix(500))") }
        let png = try Data(contentsOf: URL(fileURLWithPath: source))
        guard let bitmap = NSBitmapImageRep(data: png) else { Self.fail("Invalid image for \(filename)") }
        let file = directory.appendingPathComponent(filename + ".png")
        try png.write(to: file)

        var count = 0
        for y in 0..<bitmap.pixelsHigh {
            for x in 0..<bitmap.pixelsWide {
                guard let color = bitmap.colorAt(x: x, y: y)?.usingColorSpace(.sRGB), color.alphaComponent > 0.9
                else { continue }
                let (red, green, blue) = (color.redComponent, color.greenComponent, color.blueComponent)
                if blue > 0.35 && green > 0.25 && blue > red + 0.15 && blue > green + 0.055 { count += 1 }
            }
        }
        return count
    }

    private func require(_ condition: Bool, _ message: String) {
        if !condition { Self.fail(message) }
    }

    private static func fail(_ message: String) -> Never {
        FileHandle.standardError.write(Data(("FAIL: " + message + "\n").utf8))
        exit(1)
    }
}
