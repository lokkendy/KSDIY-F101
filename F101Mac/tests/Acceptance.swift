import AppKit

private final class StubLink: DeviceLink {
    var commands: [String] = []
    let event: ([String:Any]) -> Void
    init(event: @escaping ([String:Any]) -> Void) { self.event = event }
    func send(_ command: String) throws { commands.append(command) }
}

@main enum Acceptance {
    @MainActor static func main() {
        let defaults = UserDefaults(suiteName: "local.f101.macpanel.v2.tests")!
        let args = CommandLine.arguments
        if args.contains("--long-filename") || args.contains("--local-link-fixture") || args.contains("--demo-error") {
            let app = NSApplication.shared
            app.setActivationPolicy(.accessory)
            let model = Model(defaults: defaults)
            if args.contains("--local-link-fixture") {
                precondition(Bundle.main.bundleIdentifier == "local.f101.macpanel.v2.acceptance")
                model.connect()
            } else if args.contains("--long-filename") {
                model.values["subtask_name"] = String(repeating: "長檔名測試", count: 8) + ".3mf"
            } else {
                model.values["gcode_state"] = "FAILED"
            }
            let controller = StatusPanel(model: model)
            withExtendedLifetime(controller) { app.run() }
            return
        }

        let demo = Model(defaults: defaults) { _, _, _ in
            fatalError("Demo tried to open a device link")
        }
        checkDemo(.pause, report: "PAUSE", model: demo)
        demo.resumeDemo()
        checkDemo(.stop, report: "IDLE", model: demo)
        demo.demo()
        precondition(demo.command(.pause))
        demo.demo()
        RunLoop.main.run(until: Date(timeIntervalSinceNow: 1))
        precondition(demo.state == "RUNNING" && demo.pending == nil)
        print("Simulated pause/stop: initially RUNNING, duplicate rejected, state changes on delayed report; reset drops stale report.")

        var monitor: StubLink?
        weak var camera: StubLink?
        let lan = Model(defaults: defaults) { role, _, event in
            let stub = StubLink(event: event)
            if role == "monitor" { monitor = stub } else { camera = stub }
            return stub
        }
        lan.connect()
        precondition(!lan.mock && !lan.canSend(.pause))
        monitor?.event(["status": ["gcode_state": "RUNNING"]])
        precondition(lan.canSend(.pause) && lan.canSend(.stop) && !lan.canResumeDemo)
        precondition(lan.command(.pause) && !lan.command(.pause) && !lan.command(.stop))
        precondition(monitor?.commands == ["pause"] && lan.state == "RUNNING")
        monitor?.event(["message": "指令已送出，等待設備回報"])
        monitor?.event(["status": ["mc_percent": 43]])
        monitor?.event(["status": ["gcode_state": "RUNNING"]])
        precondition(lan.state == "RUNNING" && lan.pending == .pause)
        monitor?.event(["status": ["gcode_state": "PAUSE"]])
        precondition(!lan.canSend(.pause) && lan.canSend(.stop) && !lan.canResumeDemo)
        precondition(lan.command(.stop) && !lan.command(.stop))
        precondition(monitor?.commands == ["pause", "stop"] && lan.state == "PAUSE")
        monitor?.event(["status": ["gcode_state": "IDLE"]])
        precondition(lan.state == "IDLE" && lan.pending == nil)
        print("Stub monitor: only one send per action; send ACK, telemetry, and repeated RUNNING do not advance state.")

        lan.openCamera()
        precondition(camera != nil)
        let stale = camera?.event
        lan.closeCamera()
        precondition(camera == nil)
        stale?(["error": "stale event"])
        precondition(lan.cameraNote.isEmpty)
        print("Stub camera: close releases link and drops stale events.")
    }

    private static func checkDemo(_ action: PrintAction, report: String, model: Model) {
        precondition(model.command(action))
        precondition(model.state == "RUNNING" && model.pending == action)
        precondition(!model.command(action))
        RunLoop.main.run(until: Date(timeIntervalSinceNow: 1))
        precondition(model.state == report && model.pending == nil)
    }
}
