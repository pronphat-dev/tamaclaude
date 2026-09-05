import Foundation

/// โหมด `--hook` — ต่อท้าย Claude Code hook ทุกตัว
///
/// กติกาข้อเดียวที่ห้ามพลาด: ต้องคืน 0 และจบเร็วเสมอ แม้ daemon ไม่ทำงาน
/// hook ที่พังหรือค้างจะไปทำให้ session ของผู้ใช้พังตาม ซึ่งแย่กว่าจอไม่ขยับมาก
public enum HookClient {
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
        let ok = SocketClient(path: Paths.socket).send(line)
        if !ok { Log.debug("daemon not running, event dropped") }
        return 0
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
