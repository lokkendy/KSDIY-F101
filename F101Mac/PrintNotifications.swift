import AppKit
import Combine
import UserNotifications

final class PrintFailureTracker {
    final class Failure {
        fileprivate var taskID: String?
        fileprivate init(taskID: String?) { self.taskID = taskID }
    }

    private static let lastTaskKey = "lastNotifiedPrintTaskID"
    private static let handledTasksKey = "handledPrintTaskIDs"
    private static let handledTasksLimit = 50
    private let defaults: UserDefaults?
    private var storedTasks = [String]()
    private var previousState: String?
    private var handledTasks = Set<String>()
    private var handledFailure = false
    private var activeSinceFailure = false
    private var awaitingFailedTaskID: Failure?
    private var lastDeliveredFailure: Failure?

    init(defaults: UserDefaults? = nil) {
        self.defaults = defaults
        var saved = (defaults?.stringArray(forKey: Self.handledTasksKey) ?? []).compactMap(Self.normalizedTaskID)
        // Earlier builds kept only the last notified task under a single key.
        if let legacy = Self.normalizedTaskID(defaults?.string(forKey: Self.lastTaskKey)), !saved.contains(legacy) {
            saved.append(legacy)
        }
        storedTasks = Array(saved.suffix(Self.handledTasksLimit))
        handledTasks.formUnion(storedTasks)
    }

    func beginSession() {
        previousState = nil
    }

    static func normalizedTaskID(_ value: String?) -> String? {
        guard let id = value?.trimmingCharacters(in: .whitespacesAndNewlines),
              !id.isEmpty, id != "0" else { return nil }
        return id
    }

    func record(state: String?, taskID: String?) -> Failure? {
        let task = Self.normalizedTaskID(taskID)
        let knownState = state.flatMap { $0.isEmpty || $0 == Model.missing ? nil : $0 }
        if previousState == "FAILED", knownState == nil || knownState == "FAILED",
           let failure = awaitingFailedTaskID, let task {
            handledTasks.insert(task)
            awaitingFailedTaskID = nil
            failure.taskID = task
            store(task)
        }
        guard let state = knownState else { return nil }
        let previous = previousState
        previousState = state
        if state != "FAILED" || previous == nil { awaitingFailedTaskID = nil }
        if state == "PREPARE" || state == "RUNNING" { activeSinceFailure = true }
        guard state == "FAILED", previous != "FAILED" else { return nil }

        if let task {
            guard handledTasks.insert(task).inserted else { return nil }
            store(task)
        } else if handledFailure && !activeSinceFailure {
            return nil
        }

        handledFailure = true
        activeSinceFailure = false
        let failure = Failure(taskID: task)
        // An ID that arrives later during this failure still belongs to it.
        if task == nil { awaitingFailedTaskID = failure }
        // A failure already present on the first report is the current baseline: handled, not announced.
        return previous == nil ? nil : failure
    }

    func didDeliver(_ failure: Failure) {
        lastDeliveredFailure = failure
    }

    private func store(_ task: String) {
        storedTasks.removeAll { $0 == task }
        storedTasks.append(task)
        storedTasks = Array(storedTasks.suffix(Self.handledTasksLimit))
        defaults?.set(storedTasks, forKey: Self.handledTasksKey)
        defaults?.removeObject(forKey: Self.lastTaskKey)
    }
}

struct PrintFailureNotice {
    let body: String
    let at: Date
}

protocol PrintNotificationCenter {
    func readAuthorization(_ completion: @escaping (UNAuthorizationStatus) -> Void)
    func requestAuthorization(_ completion: @escaping () -> Void)
    func deliver(_ notice: PrintFailureNotice, onDelivered: @escaping () -> Void)
}

final class PrintFailureNotifications: ObservableObject {
    @Published private(set) var authorization: UNAuthorizationStatus?
    private let center: PrintNotificationCenter
    private let now: () -> Date
    private var requesting = false

    init(center: PrintNotificationCenter, now: @escaping () -> Date = Date.init) {
        self.center = center
        self.now = now
    }

    var authorizationLabel: String {
        switch authorization {
        case .authorized, .provisional, .ephemeral: "已允許"
        case .denied: "已拒絕"
        case .notDetermined: "尚未決定"
        default: "讀取中"
        }
    }

    func refreshAuthorization() {
        center.readAuthorization { [weak self] in self?.authorization = $0 }
    }

    func failure(printer: String, filename: String, percent: Int?, redacting identifiers: [String] = [],
                 onDelivered: @escaping () -> Void = {}) {
        var parts = [printer]
        if !filename.isEmpty, filename != Model.missing {
            let basename = URL(fileURLWithPath: filename.replacingOccurrences(of: "\\", with: "/")).lastPathComponent
            let ipv4 = #"(?<![0-9])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?![0-9])"#
            // IPv6 needs at least two colons; long hex runs cover fingerprints and similar keys.
            let ipv6 = #"(?<![0-9A-Fa-f:])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![0-9A-Fa-f:])"#
            let hexRun = #"(?<![0-9A-Fa-f])[0-9A-Fa-f]{16,}(?![0-9A-Fa-f])"#
            let literals = identifiers
                .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                .filter { !$0.isEmpty }
                .sorted { $0.count > $1.count }
                .map { NSRegularExpression.escapedPattern(for: $0) }
            let pattern = (literals + [ipv6, ipv4, hexRun]).joined(separator: "|")
            parts.append(basename.replacingOccurrences(of: pattern, with: "•••",
                                                       options: [.regularExpression, .caseInsensitive]))
        }
        if let percent { parts.append("\(percent)%") }
        let notice = PrintFailureNotice(body: parts.joined(separator: " · "), at: now())

        center.readAuthorization { [weak self] status in
            guard let self else { return }
            self.authorization = status
            switch status {
            case .authorized, .provisional, .ephemeral:
                self.center.deliver(notice, onDelivered: onDelivered)
            case .notDetermined where !self.requesting:
                self.requesting = true
                self.center.requestAuthorization { [weak self] in
                    self?.requesting = false
                    self?.refreshAuthorization()
                }
            default:
                break
            }
        }
    }
}

final class MacPrintNotificationCenter: NSObject, PrintNotificationCenter, UNUserNotificationCenterDelegate {
    private static let prefix = "print-failure."
    private let center = UNUserNotificationCenter.current()
    var openDevice: (() -> Void)?

    override init() {
        super.init()
        center.delegate = self
    }

    func readAuthorization(_ completion: @escaping (UNAuthorizationStatus) -> Void) {
        center.getNotificationSettings { settings in
            DispatchQueue.main.async { completion(settings.authorizationStatus) }
        }
    }

    func requestAuthorization(_ completion: @escaping () -> Void) {
        center.requestAuthorization(options: [.alert]) { _, error in
            if let error {
                let issue = error as NSError
                NSLog("Print notification authorization failed (%@ %ld)", issue.domain, issue.code)
            }
            DispatchQueue.main.async { completion() }
        }
    }

    func deliver(_ notice: PrintFailureNotice, onDelivered: @escaping () -> Void) {
        let content = UNMutableNotificationContent()
        content.title = "打印失敗"
        content.body = notice.body
        let id = Self.prefix + String(notice.at.timeIntervalSince1970) + "-" + UUID().uuidString
        center.add(UNNotificationRequest(identifier: id, content: content, trigger: nil)) { error in
            if let error {
                let issue = error as NSError
                NSLog("Print notification delivery failed (%@ %ld)", issue.domain, issue.code)
            } else {
                DispatchQueue.main.async { onDelivered() }
            }
        }
    }

    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler completion: @escaping (UNNotificationPresentationOptions) -> Void) {
        completion([.banner, .list])
    }

    func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse,
                                withCompletionHandler completion: @escaping () -> Void) {
        guard response.actionIdentifier == UNNotificationDefaultActionIdentifier,
              response.notification.request.identifier.hasPrefix(Self.prefix) else { completion(); return }
        DispatchQueue.main.async { [weak self] in
            self?.openDevice?()
            completion()
        }
    }
}
