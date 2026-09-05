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
        let command = "\(URL(fileURLWithPath: binary).standardizedFileURL.path) --hook"
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
            var entries = hooks[event] as? [[String: Any]] ?? []
            let already = entries.contains { entry in
                let inner = entry["hooks"] as? [[String: Any]] ?? []
                return inner.contains { ($0["command"] as? String)?.contains("--hook") == true
                    && ($0["command"] as? String)?.contains("tamaclaude") == true }
            }
            if already {
                // อัปเดตพาธให้ตรงกับ binary ปัจจุบัน แทนที่จะเพิ่มซ้ำ
                entries = entries.map { entry in
                    var entry = entry
                    let inner = (entry["hooks"] as? [[String: Any]] ?? []).map { h -> [String: Any] in
                        var h = h
                        if let c = h["command"] as? String,
                            c.contains("tamaclaude"), c.contains("--hook") {
                            h["command"] = command
                        }
                        return h
                    }
                    entry["hooks"] = inner
                    return entry
                }
            } else {
                entries.append(["hooks": [["type": "command", "command": command]]])
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
