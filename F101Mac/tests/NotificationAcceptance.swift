import Foundation
import UserNotifications

private final class FakeNotificationCenter: PrintNotificationCenter {
    var authorization: UNAuthorizationStatus
    var requests = 0
    var sent = [PrintFailureNotice]()
    private var finishRequest: (() -> Void)?

    init(_ authorization: UNAuthorizationStatus) { self.authorization = authorization }

    func readAuthorization(_ completion: @escaping (UNAuthorizationStatus) -> Void) {
        completion(authorization)
    }
    func requestAuthorization(_ completion: @escaping () -> Void) {
        requests += 1
        finishRequest = completion
    }
    func resolve(_ authorization: UNAuthorizationStatus) {
        self.authorization = authorization
        finishRequest?()
        finishRequest = nil
    }
    func deliver(_ notice: PrintFailureNotice, onDelivered: @escaping () -> Void) {
        sent.append(notice)
        onDelivered()
    }
}

private final class FakeMonitor: DeviceLink {
    func send(_ command: String) throws { fatalError("Notification test attempted printer control") }
}

@main enum NotificationAcceptance {
    @MainActor static func main() {
        let suite = "local.f101.macpanel.v2.notifications.tests." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        switch CommandLine.arguments.dropFirst().first {
        case "late-task-id": testLateTaskID(defaults)
        case "reconnect-activity": testReconnectActivity(defaults)
        case "restart-task-id": testRestartTaskID(defaults)
        case "new-task-details": testNewTaskDetails(defaults)
        case "redacted-filename": testRedactedFilename(defaults)
        case "backfill-details": testBackfillDetails(defaults)
        case "baseline-late-id": testBaselineLateTaskID(defaults)
        case "restart-many-ids": testRestartManyTaskIDs(defaults)
        case "redacted-secrets": testRedactedSecrets(defaults)
        default:
            testBackfillDetails(defaults)
            testBaselineLateTaskID(defaults)
            testRestartManyTaskIDs(defaults)
            testRedactedSecrets(defaults)
            testLateTaskID(defaults)
            testReconnectActivity(defaults)
            testRestartTaskID(defaults)
            testNewTaskDetails(defaults)
            testRedactedFilename(defaults)
            runOriginalCases(defaults)
        }
    }

    @MainActor private static func testLateTaskID(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            report(["gcode_state": "RUNNING"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 1, "late-task-id: first failure should notify once")
            report(["subtask_id": "late-task"])
            report(["gcode_state": "IDLE"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 1, "late-task-id: expected 1 notification, got \(center.sent.count)")
        }
        print("late-task-id: PASS")
    }

    @MainActor private static func testReconnectActivity(_ defaults: UserDefaults) {
        for activeBeforeReconnect in [true, false] {
            let center = FakeNotificationCenter(.authorized)
            let (model, report) = lanFixture(defaults, center)
            report(["gcode_state": "RUNNING"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 1, "reconnect-activity: first failure should notify once")
            if activeBeforeReconnect { report(["gcode_state": "RUNNING"]) }
            model.connect()
            report(["gcode_state": "PAUSE"])
            expect(center.sent.count == 1, "reconnect-activity: baseline should be silent")
            if !activeBeforeReconnect { report(["gcode_state": "PREPARE"]) }
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 2, "reconnect-activity: expected 2 notices, got \(center.sent.count) (active before reconnect: \(activeBeforeReconnect))")
        }
        print("reconnect-activity: PASS")
    }

    @MainActor private static func testRestartTaskID(_ defaults: UserDefaults) {
        for late in [false, true] {
            testRestartTaskID(defaults, late: late)
        }
        print("restart-task-id: PASS")
    }

    @MainActor private static func testRestartTaskID(_ defaults: UserDefaults, late: Bool) {
        let center = FakeNotificationCenter(.authorized)
        let task = late ? "persisted-late-task" : "persisted-task"
        do {
            let (model, report) = lanFixture(defaults, center)
            withExtendedLifetime(model) {
                var status = ["gcode_state": "RUNNING"]
                if !late { status["subtask_id"] = task }
                report(status)
                report(["gcode_state": "FAILED"])
                expect(center.sent.count == 1, "restart-task-id: first run should notify once")
                if late { report(["subtask_id": task]) }
            }
        }
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            report(["gcode_state": "PAUSE", "subtask_id": task])
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 1, "restart-task-id: expected no duplicate after recreating Model, got \(center.sent.count) notices")
        }
    }

    @MainActor private static func testNewTaskDetails(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        report(["gcode_state": "RUNNING", "subtask_id": "old-job", "subtask_name": "old.3mf",
                "gcode_file": "/old.gcode", "mc_percent": "82", "mc_remaining_time": "9",
                "layers": "80 / 100", "elapsed": "11 分", "eta": "14:41", "material": "PLA",
                "nozzle_temper": "218", "bed_temper": "60"])
        report(["gcode_state": "RUNNING", "subtask_id": "new-job"])
        report(["gcode_state": "FAILED"])
        expect(center.sent.map(\.body) == ["區網打印機"], "new-task-details: unexpected notice \(center.sent.map(\.body))")
        expect(model.filename == Model.missing && model.percent == nil && model.remaining == Model.missing,
               "new-task-details: panel retained old filename, progress or remaining time")
        expect(["gcode_file", "layers", "elapsed", "eta", "material"].allSatisfy { model.values[$0] == nil },
               "new-task-details: task metadata was not cleared")
        expect(model.values["nozzle_temper"] == "218" && model.values["bed_temper"] == "60",
               "new-task-details: device telemetry should survive a task change")
        report(["gcode_state": "RUNNING", "subtask_id": "third-job", "subtask_name": "new.3mf", "mc_percent": "14"])
        report(["gcode_state": "FAILED"])
        expect(center.sent.last?.body == "區網打印機 · new.3mf · 14%", "new-task-details: same-report fields were lost")
        print("new-task-details: PASS")
    }

    @MainActor private static func testRedactedFilename(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        model.config["host"] = "192.0.2.40"
        model.config["serial"] = "SNFIX.77"
        let filename = "C:\\jobs\\snfix.77_snfixX77_192.0.2.40_198.51.100.7_999.888.777.666.3mf"
        report(["gcode_state": "RUNNING", "subtask_id": "privacy-task", "gcode_file": filename, "mc_percent": "42"])
        report(["gcode_state": "FAILED"])
        expect(center.sent.map(\.body) == ["區網打印機 · •••_snfixX77_•••_•••_•••.3mf · 42%"],
               "redacted-filename: notice exposed identifiers or redacted unrelated text: \(center.sent.map(\.body))")
        expect(model.filename == filename, "redacted-filename: panel filename should remain unchanged")
        print("redacted-filename: PASS")
    }

    // The first ID for a running task belongs to that task; it must not wipe its details.
    @MainActor private static func testBackfillDetails(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            report(["gcode_state": "RUNNING", "subtask_name": "cube.3mf", "mc_percent": "42"])
            report(["subtask_id": "backfill-task"])
            expect(model.filename == "cube.3mf" && model.percent == 42,
                   "backfill-details: first ID cleared details (\(model.filename), \(String(describing: model.percent)))")
            report(["gcode_state": "FAILED"])
            expect(center.sent.map(\.body) == ["區網打印機 · cube.3mf · 42%"],
                   "backfill-details: unexpected notice \(center.sent.map(\.body))")
        }
        print("backfill-details: PASS")
    }

    // A baseline failure without an ID is still the current failure once its ID arrives.
    @MainActor private static func testBaselineLateTaskID(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            report(["gcode_state": "FAILED"])
            report(["subtask_id": "baseline-task"])
            report(["gcode_state": "IDLE"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.isEmpty, "baseline-late-id: same baseline task notified \(center.sent.count) times")
        }
        print("baseline-late-id: PASS")
    }

    // Every notified task stays handled across a restart, not only the latest one.
    @MainActor private static func testRestartManyTaskIDs(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        do {
            let (model, report) = lanFixture(defaults, center)
            withExtendedLifetime(model) {
                for task in ["many-a", "many-b"] {
                    report(["gcode_state": "RUNNING", "subtask_id": task])
                    report(["gcode_state": "FAILED"])
                }
                expect(center.sent.count == 2, "restart-many-ids: first run expected 2 notices, got \(center.sent.count)")
            }
        }
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            report(["gcode_state": "PAUSE", "subtask_id": "many-a"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.count == 2, "restart-many-ids: older task notified again after restart (\(center.sent.count))")
        }
        print("restart-many-ids: PASS")
    }

    // Configured secrets and other address shapes are masked in the notice text.
    @MainActor private static func testRedactedSecrets(_ defaults: UserDefaults) {
        let center = FakeNotificationCenter(.authorized)
        let (model, report) = lanFixture(defaults, center)
        withExtendedLifetime(model) {
            let pin = String(repeating: "ab12", count: 16)
            model.config["code"] = "fake4321"
            model.config["pin"] = pin
            let filename = "part_2001:db8::1_fake4321_" + pin + "_v2.3mf"
            report(["gcode_state": "RUNNING", "subtask_id": "secret-task", "gcode_file": filename, "mc_percent": "5"])
            report(["gcode_state": "FAILED"])
            expect(center.sent.map(\.body) == ["區網打印機 · part_•••_•••_•••_v2.3mf · 5%"],
                   "redacted-secrets: notice exposed identifiers: \(center.sent.map(\.body))")
        }
        print("redacted-secrets: PASS")
    }

    @MainActor private static func lanFixture(_ defaults: UserDefaults, _ center: FakeNotificationCenter)
        -> (Model, ([String:String]) -> Void) {
        var event: ([String:Any]) -> Void = { _ in }
        let notifications = PrintFailureNotifications(center: center)
        let model = Model(defaults: defaults, notifications: notifications) { _, _, callback in
            event = callback
            return FakeMonitor()
        }
        model.connect()
        return (model, { status in event(["status": status]) })
    }

    private static func expect(_ condition: Bool, _ message: String) {
        guard !condition else { return }
        FileHandle.standardError.write(Data(("FAIL: " + message + "\n").utf8))
        exit(1)
    }

    @MainActor private static func runOriginalCases(_ defaults: UserDefaults) {
        var clock = Date(timeIntervalSince1970: 1_790_000_000)
        let center = FakeNotificationCenter(.authorized)
        var mockReport: ([String:Any]) -> Void = { _ in }
        let notifications = PrintFailureNotifications(center: center, now: { clock })
        let lan = Model(defaults: defaults, notifications: notifications) { role, _, event in
            precondition(role == "monitor")
            mockReport = event
            return FakeMonitor()
        }
        lan.connect()
        func report(_ state: String, _ task: String? = nil) {
            var status = ["gcode_state": state]
            status["subtask_id"] = task
            mockReport(["status": status])
        }

        report("FAILED", "startup")
        report("IDLE")
        report("FAILED")
        precondition(center.sent.isEmpty)
        report("RUNNING", "task-a")
        mockReport(["status": ["gcode_file": "/fixture/data/Cube.3mf", "mc_percent": "42", "pin": "ignored"]])
        clock += 2
        report("FAILED")
        precondition(center.requests == 0 && center.sent.count == 1)
        precondition(center.sent[0].at == clock && center.sent[0].body == "區網打印機 · Cube.3mf · 42%")
        report("FAILED", "task-a")
        report("IDLE")
        report("FAILED", "task-a")
        report("RUNNING", "task-a")
        report("FAILED", "task-a")
        precondition(center.sent.count == 1)
        lan.connect()
        report("FAILED", "task-a")
        report("RUNNING", "task-a")
        report("FAILED")
        precondition(center.sent.count == 1)
        report("PREPARE", "task-b")
        report("FAILED")
        precondition(center.sent.count == 2 && center.sent[1].body == "區網打印機")
        report("IDLE", "0")
        report("FAILED")
        precondition(center.sent.count == 2)
        report("RUNNING")
        report("FAILED")
        precondition(center.sent.count == 3)
        report("IDLE", "")
        report("FAILED")
        precondition(center.sent.count == 3)
        report("PREPARE", "")
        report("FAILED")
        precondition(center.sent.count == 4)
        print("AUTHORIZED: first transition once, repeated/flapping/reconnected task suppressed, new ID and ID-less active job emit.")

        center.authorization = .denied
        var deniedReport: ([String:Any]) -> Void = { _ in }
        let denied = Model(defaults: defaults, notifications: notifications) { _, _, event in
            deniedReport = event
            return FakeMonitor()
        }
        denied.connect()
        deniedReport(["status": ["gcode_state": "RUNNING"]])
        deniedReport(["status": ["gcode_state": "FAILED"]])
        precondition(center.sent.count == 4 && center.requests == 0)
        precondition(denied.state == "FAILED" && StatusPanel.iconState(values: denied.values) == .failed)
        precondition(notifications.authorization == .denied)
        print("DENIED: no notification or permission request; menu status stays failed.")

        let undecided = FakeNotificationCenter(.notDetermined)
        let firstUse = PrintFailureNotifications(center: undecided, now: { clock })
        firstUse.refreshAuthorization()
        precondition(undecided.requests == 0)
        firstUse.failure(printer: "A1", filename: "alpha.3mf", percent: nil)
        firstUse.failure(printer: "A1", filename: "beta.3mf", percent: nil)
        precondition(undecided.requests == 1 && undecided.sent.isEmpty)
        undecided.resolve(.authorized)
        precondition(undecided.sent.isEmpty)
        firstUse.failure(printer: "A1", filename: "gamma.3mf", percent: nil)
        precondition(undecided.sent.map(\.body) == ["A1 · gamma.3mf"])
        print("UNDETERMINED: prompt once on first need, no retroactive delivery; next independent failure delivered.")
    }
}
