import Foundation

/// โหมด `--hook` — ต่อท้าย Claude Code hook ทุกตัว
///
/// กติกาข้อเดียวที่ห้ามพลาด: ต้องคืน 0 และจบเร็วเสมอ แม้ daemon ไม่ทำงาน
/// hook ที่พังหรือค้างจะไปทำให้ session ของผู้ใช้พังตาม ซึ่งแย่กว่าจอไม่ขยับมาก
public enum HookClient {
    /// นานที่สุดที่ยอมค้างรอคำตอบของคำขออนุญาต ก่อนปล่อยให้ terminal ถามตามปกติ
    ///
    /// ยาวกว่าฝั่ง daemon เล็กน้อยโดยตั้งใจ (`Daemon.decisionWait`) เพื่อให้คำว่า `ask`
    /// ที่ daemon ส่งมาเองเป็นตัวจบเรื่อง ไม่ใช่การหมดเวลาเงียบๆ ของฝั่งนี้
    ///
    /// ตัวเลขนี้คือเวลาที่ผู้ใช้จะเห็น terminal นิ่งอยู่ถ้าเขาไม่ได้มองจอ — สั้นพอที่จะ
    /// ไม่ทำให้คนที่ลืมบอร์ดไปแล้วนั่งงง ยาวพอที่จะหันไปแตะจอทัน
    public static let decisionWait: TimeInterval = 30

    public static func run(input: FileHandle = .standardInput) -> Int32 {
        guard let raw = try? input.readToEnd(), !raw.isEmpty else { return 0 }
        guard var event = try? JSONDecoder().decode(HookEvent.self, from: raw) else {
            Log.debug("hook input was not a recognised event")
            return 0
        }
        // ต้องหาที่นี่เท่านั้น: hook process ยังเป็นลูกของ Claude Code อยู่ตอนนี้
        // พอส่งเข้า socket แล้ว daemon อยู่คนละสายบรรพบุรุษ ไต่กลับไปไม่ได้อีก
        event.owner = ProcessTree.claudeAncestor()
        // เหตุผลเดียวกันในทางกลับกัน: ไบต์ดิบอยู่ในมือ *ที่นี่* ที่เดียว
        event.toolInput = toolInputText(raw)
        guard let line = try? Wire.encoder().encode(event) else { return 0 }

        // คำขออนุญาตเป็นเหตุการณ์เดียวที่รอคำตอบ · ที่เหลือส่งแล้วจบทันทีเหมือนเดิม
        // — hook ที่ค้างคือ hook ที่ทำให้ session ของผู้ใช้ค้างตาม
        if event.hookEventName == "PermissionRequest" {
            let answer = SocketClient(path: Paths.socket)
                .sendAndWait(line, timeout: decisionWait)
            if let answer,
                let decision = try? JSONDecoder().decode(Decision.self, from: answer),
                let verdict = output(for: decision) {
                print(verdict)
            }
            return 0
        }

        let ok = SocketClient(path: Paths.socket).send(line)
        if !ok { Log.debug("daemon not running, event dropped") }
        return 0
    }

    /// JSON ที่ Claude Code อ่านจาก stdout — มีก็ต่อเมื่อมีคำตอบจริงเท่านั้น
    ///
    /// `ask` ไม่ได้ถูกเขียนออกไปเป็นคำสั่ง "ให้ถาม" แต่คือการ *ไม่พูดอะไรเลย* ซึ่งเป็น
    /// สิ่งที่ hook ตัวนี้ทำมาตลอดก่อนมีฟีเจอร์นี้ · เงียบคือค่าเริ่มต้นที่ปลอดภัยที่สุด
    /// และเป็นค่าเดียวกับที่เราตกไปอยู่เมื่อทุกอย่างผิดพลาด
    public static func output(for decision: Decision) -> String? {
        let verdict: String
        switch decision.answer {
        case .allow: verdict = "allow"
        case .deny: verdict = "deny"
        case .ask: return nil
        }
        return #"""
            {"hookSpecificOutput":{"hookEventName":"PermissionRequest",\#
            "permissionDecision":"\#(verdict)",\#
            "permissionDecisionReason":"answered on the TamaClaude board"}}
            """#
    }

    /// `tool_input` ทั้งก้อนแบนเป็นข้อความบรรทัดเดียว — nil เมื่อไม่มีคีย์นี้
    ///
    /// ไม่ได้พยายามเข้าใจโครงสร้างของมันเลย เพราะไม่มีใครในระบบนี้ต้องการโครงสร้าง
    /// สิ่งที่ต้องการคือ "มีคำว่า `rm -rf` อยู่ในนั้นไหม" (`Risk`) ซึ่งข้อความตอบได้
    public static func toolInputText(_ raw: Data) -> String? {
        guard let root = try? JSONSerialization.jsonObject(with: raw) as? [String: Any],
            let input = root["tool_input"]
        else { return nil }
        // เครื่องมือที่รับสตริงเดี่ยวมีอยู่จริง และ `isValidJSONObject` ปฏิเสธมัน
        if let text = input as? String { return text }
        guard JSONSerialization.isValidJSONObject(input),
            let data = try? JSONSerialization.data(withJSONObject: input, options: [.sortedKeys])
        else { return nil }
        return String(data: data, encoding: .utf8)
    }
}
