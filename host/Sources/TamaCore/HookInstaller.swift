import Foundation

/// เขียน hook ของเราเข้า `~/.claude/settings.json` โดยไม่แตะของเดิม
///
/// ใช้ JSONSerialization ไม่ใช่ Codable เพราะไฟล์นี้เป็นของผู้ใช้:
/// คีย์ที่เราไม่รู้จักต้องรอดกลับออกไปครบ
public enum HookInstaller {
    public static var settingsPath: URL {
        Paths.home
            .appendingPathComponent(".claude", isDirectory: true)
            .appendingPathComponent("settings.json")
    }

    /// hook ที่ daemon ใช้จริง — ตรงกับ `switch` ใน SessionStore.apply
    ///
    /// ชื่อที่ Claude Code รุ่นนั้นไม่รู้จักจะไม่มีวันยิง คีย์ที่เกินมาใน settings.json
    /// ไม่ทำให้ hook ตัวอื่นเสีย จึงติดตั้งเผื่อทั้งชุดได้ แทนที่จะต้องเดารุ่นของผู้ใช้
    public static let events = [
        "SessionStart",
        "UserPromptSubmit",
        "PreToolUse",
        "PostToolUse",
        // เครื่องมือที่พังไม่ได้จบด้วย PostToolUse — ขาดตัวนี้ไปมาสคอตจะค้างท่าเครื่องมือ
        "PostToolUseFailure",
        "PostToolBatch",
        "PreCompact",
        "PostCompact",
        "Notification",
        // คำขออนุญาตแยกออกมาเป็นเหตุการณ์ของตัวเองแล้ว ไม่ได้มาทาง Notification ทางเดียว
        // และ Elicitation คือ MCP ที่ขอคำตอบจากคน — ทั้งคู่แปลว่า "รอมือคน" เหมือนกัน
        "PermissionRequest",
        "PermissionDenied",
        "Elicitation",
        "ElicitationResult",
        "TeammateIdle",
        "Stop",
        // เทิร์นที่ตายเพราะ API ไม่ยิง Stop · SessionStore รู้จักชื่อนี้มาตลอด
        // แต่ไม่เคยถูกติดตั้ง ท่า error ของเทิร์นที่ล้มจึงไม่เคยขึ้นจอจริง
        "StopFailure",
        // ต้องมีคู่กับ SubagentStop เสมอ — ถ้าติดตั้งแต่ Stop ตัวนับจะติดลบไม่ได้
        // (max(0,·)) แล้วค้างที่ศูนย์ตลอด ท่า conducting จะไม่มีวันโผล่
        "SubagentStart",
        "SubagentStop",
        "SessionEnd",
    ]

    /// เพดานเวลาต่อ hook หนึ่งตัว เขียนลงไฟล์เป็นวินาที
    ///
    /// มีอยู่ชื่อเดียว เพราะมีอยู่เหตุการณ์เดียวที่ตั้งใจให้ค้าง · ค่าปริยายของ Claude Code
    /// สำหรับ hook ชนิดคำสั่งคือ 600 วินาที ซึ่งยาวเกินกว่าจะเป็นตาข่ายรองรับอะไรได้เลย
    /// ตัวเลขนี้ยาวกว่า `HookClient.decisionWait` พอให้ฝั่งเราได้จบเรื่องเองเสมอ
    /// และสั้นพอที่ hook ซึ่งพังเกินคาดจะไม่ถ่วง session ไว้เป็นสิบนาที
    public static let timeouts = ["PermissionRequest": 45]

    /// สภาพของ hook ที่ติดตั้งไว้จริง ณ วินาทีนี้ — อ่านอย่างเดียว ไม่แตะไฟล์
    public struct Status: Equatable, Sendable {
        /// ชื่อเหตุการณ์ที่มีคำสั่งของเราอยู่แล้ว (รวมชื่อที่เราเลิกใช้ไปแล้วด้วย)
        public var installed: Set<String>
        /// พาธที่ hook ชี้ไปตอนนี้ — nil = ไฟล์นี้ไม่เคยรู้จักเรา
        public var command: String?
        /// พาธที่ *ควร* ชี้ คือแอปตัวที่กำลังถามอยู่นี้
        public var wanted: String
        /// เพดานเวลาที่ติดตั้งไว้จริง ต่อเหตุการณ์ — ว่างคือใช้ค่าปริยายของ Claude Code
        public var timeouts: [String: Int]

        /// เคยกดติดตั้งไว้ไหม — เกณฑ์ว่า "ของนี้เป็นของเขาแล้ว" ซึ่งต่างจาก
        /// "เราควรติดตั้งให้เขา" อย่างสิ้นเชิง ดู `repair`
        public var isInstalled: Bool { command != nil }
        /// ชี้มาที่แอปตัวนี้ไหม — แอปที่ถูกย้าย/อัปเกรดทำให้ข้อนี้เป็นเท็จเงียบๆ
        public var matchesBinary: Bool { command == wanted }
        public var covered: [String] { HookInstaller.events.filter { installed.contains($0) } }
        public var missing: [String] { HookInstaller.events.filter { !installed.contains($0) } }
        /// เพดานเวลาที่เราต้องการถูกเขียนไว้ครบไหม
        ///
        /// อยู่ในเกณฑ์สุขภาพด้วย ไม่ใช่แค่พาธ: การอัปเกรดที่เพิ่มเพดานเวลาเข้ามาใหม่
        /// จะไม่มีวันไปถึงไฟล์ของคนที่ติดตั้งไว้ตั้งแต่รุ่นก่อน ถ้าไม่มีใครถือว่ามันผิด
        public var timeoutsMatch: Bool {
            HookInstaller.timeouts.allSatisfy { timeouts[$0.key] == $0.value }
        }
        public var isHealthy: Bool {
            isInstalled && matchesBinary && missing.isEmpty && timeoutsMatch
        }

        public init(
            installed: Set<String>, command: String?, wanted: String,
            timeouts: [String: Int] = [:]
        ) {
            self.installed = installed
            self.command = command
            self.wanted = wanted
            self.timeouts = timeouts
        }
    }

    /// คำสั่งที่เขียนลงไฟล์ — ที่เดียวที่รู้รูปแบบนี้ ทั้งขาเขียนและขาอ่าน
    public static func command(for binary: String) -> String {
        "\(URL(fileURLWithPath: binary).standardizedFileURL.path) --hook"
    }

    /// อ่านว่าตอนนี้ไฟล์ของผู้ใช้พูดถึงเราว่าอย่างไร
    ///
    /// ไฟล์อ่านไม่ออก/ไม่มี = "ไม่เคยติดตั้ง" ไม่ใช่ error: ฟังก์ชันนี้ถูกเรียกทุกวินาที
    /// ตอนหน้าตั้งค่าเปิดอยู่ และไม่มีอะไรให้ผู้ใช้ทำต่างกันระหว่างสองกรณีนั้น
    /// `at:` มีไว้ให้เทสต์ชี้ไปที่ไฟล์ชั่วคราว — `Paths.home` อ่านจาก getpwuid
    /// จึงหลอกด้วย env ไม่ได้ (เหตุผลเดียวกับ `cacheTarget` ใน main.swift)
    public static func status(
        binary: String = CommandLine.arguments[0], at settings: URL = HookInstaller.settingsPath
    ) -> Status {
        let wanted = command(for: binary)
        guard let data = try? Data(contentsOf: settings),
            let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
            let hooks = root["hooks"] as? [String: Any]
        else { return Status(installed: [], command: nil, wanted: wanted) }

        var installed: Set<String> = []
        var found: String?
        var timeouts: [String: Int] = [:]
        for (event, value) in hooks {
            for entry in value as? [[String: Any]] ?? [] {
                for hook in entry["hooks"] as? [[String: Any]] ?? [] {
                    guard let c = hook["command"] as? String,
                        c.contains("tamaclaude"), c.contains("--hook")
                    else { continue }
                    installed.insert(event)
                    if let seconds = hook["timeout"] as? Int { timeouts[event] = seconds }
                    // ตัวแรกที่เจอเป็นตัวแทนทั้งไฟล์ — `install` เขียนพาธเดียวกันทุกที่เสมอ
                    if found == nil { found = c }
                }
            }
        }
        return Status(
            installed: installed, command: found, wanted: wanted, timeouts: timeouts)
    }

    /// ทำให้ hook ที่ *เคยติดตั้งไว้* กลับมาตรงกับแอปตัวนี้ — คืน true เมื่อได้เขียนจริง
    ///
    /// **ซ่อม ไม่ใช่ติดตั้ง** — คนที่ไม่เคยกดติดตั้งต้องไม่ถูกเขียน `~/.claude/settings.json`
    /// ให้โดยไม่ได้ขอ ไฟล์นั้นเป็นของเขา · แต่คนที่เคยกดแล้วคือคนที่ *ขอไว้แล้ว* ว่าอยากให้
    /// จอขยับ การปล่อยให้พาธที่ค้างอยู่พาไปหาสำเนาที่ถูกลบไปแล้วจึงไม่ใช่การเคารพเจตนาเขา
    ///
    /// จำเป็นเพราะความเงียบของ hook ที่พังแยกไม่ออกจาก "วันนี้ยังไม่ได้เปิด session" เลย
    /// สักนิด — ทุกครั้งที่แอปถูกย้ายหรืออัปเกรดไปที่ใหม่ ระบบทั้งระบบจะตายเงียบ
    @discardableResult
    public static func repair(binary: String = CommandLine.arguments[0]) throws -> Bool {
        let now = status(binary: binary)
        guard now.isInstalled, !now.isHealthy else { return false }
        try install(binary: binary)
        return true
    }

    public enum InstallError: Error, CustomStringConvertible {
        case unreadableSettings
        case notJSONObject

        public var description: String {
            switch self {
            case .unreadableSettings: return "could not read ~/.claude/settings.json"
            case .notJSONObject: return "settings.json is not a JSON object"
            }
        }
    }

    public static func install(binary: String = CommandLine.arguments[0]) throws {
        let command = Self.command(for: binary)
        var root: [String: Any] = [:]

        if FileManager.default.fileExists(atPath: settingsPath.path) {
            guard let data = try? Data(contentsOf: settingsPath) else {
                throw InstallError.unreadableSettings
            }
            if !data.isEmpty {
                guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any]
                else { throw InstallError.notJSONObject }
                root = obj
            }
            // สำรองไว้ก่อนเสมอ ไฟล์นี้ผู้ใช้แก้เองมาแล้วแน่ๆ
            try? data.write(to: settingsPath.appendingPathExtension("tamaclaude.bak"))
        }

        var hooks = root["hooks"] as? [String: Any] ?? [:]
        for event in events {
            var mine: [String: Any] = ["type": "command", "command": command]
            if let seconds = timeouts[event] { mine["timeout"] = seconds }
            var entries = hooks[event] as? [[String: Any]] ?? []
            let already = entries.contains { entry in
                let inner = entry["hooks"] as? [[String: Any]] ?? []
                return inner.contains { ($0["command"] as? String)?.contains("--hook") == true
                    && ($0["command"] as? String)?.contains("tamaclaude") == true }
            }
            if already {
                // เขียนทับรายการของเราทั้งใบ แทนที่จะเพิ่มซ้ำ — พาธและเพดานเวลา
                // เปลี่ยนได้ทั้งคู่ระหว่างสองรุ่น และของที่ค้างจากรุ่นก่อนไม่ควรรอด
                entries = entries.map { entry in
                    var entry = entry
                    let inner = (entry["hooks"] as? [[String: Any]] ?? []).map { h -> [String: Any] in
                        guard let c = h["command"] as? String,
                            c.contains("tamaclaude"), c.contains("--hook")
                        else { return h }
                        return mine
                    }
                    entry["hooks"] = inner
                    return entry
                }
            } else {
                entries.append(["hooks": [mine]])
            }
            hooks[event] = entries
        }
        root["hooks"] = hooks

        try FileManager.default.createDirectory(
            at: settingsPath.deletingLastPathComponent(), withIntermediateDirectories: true)
        let out = try JSONSerialization.data(
            withJSONObject: root, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
        try out.write(to: settingsPath)
    }
}
