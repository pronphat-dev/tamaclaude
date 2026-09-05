import Foundation

// MARK: - เหตุการณ์จาก Claude Code hook

/// รูปแบบ JSON ที่ Claude Code ป้อนเข้า stdin ของ hook
/// เก็บเฉพาะฟิลด์ที่ daemon ใช้จริง ฟิลด์อื่นถูกละทิ้งตอน decode
public struct HookEvent: Codable, Equatable, Sendable {
    public var hookEventName: String
    public var sessionId: String
    public var cwd: String?
    public var toolName: String?
    public var message: String?
    public var prompt: String?
    public var reason: String?
    public var source: String?
    /// ชนิดของ `Notification` — `permission_prompt`, `idle_prompt`, `auth_success` ฯลฯ
    ///
    /// optional เพราะรุ่นที่ยังไม่ส่งฟิลด์นี้มีอยู่จริง และเป็นรุ่นที่ต้องทำงานได้เหมือนเดิม
    /// ทุกประการ · ดู `SessionStore.quiet` ว่าชนิดไหนที่ *ไม่* ได้แปลว่ามีคนรออยู่
    public var notificationType: String?
    /// `tool_input` ทั้งก้อนในรูปข้อความ — เก็บไว้ให้ `Risk` *ค้นคำ* ไม่ใช่ให้ใครอ่านโครงสร้าง
    ///
    /// Claude Code ไม่ได้ส่งคีย์นี้มา `--hook` เป็นคนเติมเองจากไบต์ดิบก่อนส่งเข้า socket
    /// (กติกาเดียวกับ `owner`) — รูปร่างของ `tool_input` ต่างกันทุกเครื่องมือ การถอดมัน
    /// ผ่าน Codable ให้ครบต้องมีต้นไม้ชนิดใหม่ทั้งต้น เพื่อผลลัพธ์ที่เราจะแบนกลับเป็น
    /// ข้อความอยู่ดี · ชื่อคีย์บนสายจึงไม่ใช่ `tool_input` เพื่อไม่ให้ชนกับของจริง
    public var toolInput: String?
    /// process ของ Claude Code ที่เป็นเจ้าของ session นี้
    ///
    /// Claude Code ไม่ได้ส่งมาใน stdin — `--hook` เป็นคนเติมเองจากสายบรรพบุรุษของตัวเอง
    /// ก่อนส่งต่อเข้า socket จึงต้อง optional เสมอ (ทั้งเหตุการณ์ที่มาจาก `--send`
    /// และ hook เก่าที่ยังไม่รู้จักคีย์นี้)
    public var owner: ProcessHandle?

    enum CodingKeys: String, CodingKey {
        case hookEventName = "hook_event_name"
        case sessionId = "session_id"
        case cwd
        case toolName = "tool_name"
        case message
        case prompt
        case reason
        case source
        case notificationType = "notification_type"
        case toolInput = "tool_input_text"
        case owner
    }

    public init(
        hookEventName: String,
        sessionId: String,
        cwd: String? = nil,
        toolName: String? = nil,
        message: String? = nil,
        prompt: String? = nil,
        reason: String? = nil,
        source: String? = nil,
        notificationType: String? = nil,
        toolInput: String? = nil,
        owner: ProcessHandle? = nil
    ) {
        self.hookEventName = hookEventName
        self.sessionId = sessionId
        self.cwd = cwd
        self.toolName = toolName
        self.message = message
        self.prompt = prompt
        self.reason = reason
        self.source = source
        self.notificationType = notificationType
        self.toolInput = toolInput
        self.owner = owner
    }

    /// ชื่อโปรเจกต์ที่จะแสดงใต้มาสคอต — ชื่อโฟลเดอร์สุดท้ายของ cwd
    public var project: String {
        guard let cwd, !cwd.isEmpty else { return "claude" }
        let name = URL(fileURLWithPath: cwd).lastPathComponent
        return name.isEmpty ? "claude" : name
    }
}

// MARK: - สถานะภาพของมาสคอต

/// ต้องตรงกับ `STATES` ใน tools/gen/mascot.py และ enum ฝั่ง firmware ทุกตัว
public enum VisualState: String, Codable, Equatable, Sendable, CaseIterable {
    case idle
    case reading
    case writing
    case building
    case searching
    case thinking
    case waiting
    case sleeping
    case alert
    case celebrate
    case error
    case entering
    case leaving
    case conducting
    case beacon

    /// ลำดับความสำคัญตอนเลือกว่า session ไหนได้ slot เมื่อมีเกิน 4 ตัว
    /// ตัวเลขสูง = ได้ก่อน
    public var priority: Int {
        switch self {
        case .alert, .error: return 40
        case .waiting: return 30
        case .entering, .leaving: return 25
        // สูงกว่า tool: session ที่คุม subagent อยู่ *เงียบสนิท* ไม่มี hook ยิงเป็นนาทีๆ
        // ตัวตัดสินอันดับสองคือ lastActivity ล่าสุดชนะ ถ้าให้เท่ากับ tool มันจะแพ้
        // session ที่ไถ Read ไปเรื่อยๆ แล้วหลุดจอ ทั้งที่เป็นตัวที่น่าสนใจที่สุด
        case .conducting: return 22
        case .reading, .writing, .building, .searching, .thinking, .beacon: return 20
        case .celebrate: return 15
        case .idle: return 10
        case .sleeping: return 0
        }
    }

    /// เดินต่อเองไม่ได้ถ้าไม่มีมือคน — เกณฑ์เดียวที่ตัดสินว่าจอควรถูกดึงกลับมาหามาสคอต
    ///
    /// `celebrate`/`idle` ไม่อยู่ในนี้ทั้งที่เทิร์นจบแล้ว: งานที่จบเรียบร้อยไม่ได้ขออะไร
    /// ส่วน `waiting` ครอบทั้งการขออนุญาตและเทิร์นที่เงียบเกินเกณฑ์ ซึ่งรอคนอยู่จริงทั้งคู่
    public var needsHuman: Bool {
        switch self {
        case .waiting, .alert, .error: return true
        default: return false
        }
    }
}

// MARK: - snapshot ที่ส่งข้ามสาย

public enum CardKind: String, Codable, Equatable, Sendable {
    case info
    case alert
    case done
}

public struct CardSnap: Codable, Equatable, Sendable {
    public var title: String
    public var body: String
    public var kind: CardKind

    enum CodingKeys: String, CodingKey {
        case title = "t"
        case body = "b"
        case kind = "k"
    }

    public init(title: String, body: String, kind: CardKind) {
        self.title = title
        self.body = body
        self.kind = kind
    }
}

public struct SessionSnap: Codable, Equatable, Sendable {
    public var project: String
    public var state: VisualState

    enum CodingKeys: String, CodingKey {
        case project = "p"
        case state = "s"
    }

    public init(project: String, state: VisualState) {
        self.project = project
        self.state = state
    }
}

/// โควตาหนึ่งหน้าต่าง — มาจาก `rate_limits.five_hour` / `.seven_day` ที่ Claude Code
/// ป้อนเข้า statusline
///
/// เข้ารหัสเป็น array 2 ช่อง `[percent, secondsRemaining]` ไม่ใช่ object
/// เพราะคีย์กินไบต์ในงบ 500 ที่แชร์กับ session และ card
///
/// **ส่งวินาทีที่เหลือ ไม่ใช่เวลารีเซ็ตสัมบูรณ์** — บอร์ดนับถอยลงเอง ทำให้ countdown
/// ยังเดินถูกตอน BLE หลุด และ daemon ไม่ต้องยิงใหม่ทุกนาทีเพียงเพื่ออัปเดตตัวเลข
public struct UsageSnap: Codable, Equatable, Sendable {
    /// ค่าที่แปลว่า "ไม่รู้" — ศูนย์เป็นค่าจริง จึงใช้เป็น sentinel ไม่ได้
    /// (ADR-0001 ของ esp32-claude-quota: utilization is reported, never derived)
    public static let unknown = -1

    public var percent: Int
    public var remaining: Int

    public init(percent: Int = UsageSnap.unknown, remaining: Int = UsageSnap.unknown) {
        self.percent = percent
        self.remaining = remaining
    }

    public var isKnown: Bool { percent != Self.unknown || remaining != Self.unknown }

    public init(from decoder: Decoder) throws {
        var c = try decoder.unkeyedContainer()
        percent = try c.decode(Int.self)
        remaining = try c.decode(Int.self)
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.unkeyedContainer()
        try c.encode(percent)
        try c.encode(remaining)
    }
}

/// คำขออนุญาตที่ค้างอยู่ ตามที่จอต้องรู้เพื่อวาดการ์ดสองปุ่ม
///
/// เล็กที่สุดเท่าที่ยังตอบได้ครบ เพราะมันเบียดที่ของ snapshot ทั้งก้อนใน MTU เดียว
public struct AskSnap: Codable, Equatable, Sendable {
    /// id ที่ต้องส่งกลับมาพร้อมคำตอบ
    public var id: String
    /// สิ่งที่กำลังจะเกิดขึ้น — "Bash · npm test"
    public var title: String
    /// จอเสนอปุ่ม Allow ได้ไหม (`Risk`)
    ///
    /// `false` ไม่ได้แปลว่าซ่อนปุ่มขวา แต่แปลว่าปุ่มขวาเปลี่ยนความหมายเป็น "ไปตอบที่
    /// คีย์บอร์ด" · การ์ดที่มีปุ่มเดียวบอกแค่ว่าทำอะไรไม่ได้ การ์ดที่ปุ่มขวาจางอยู่บอกว่า
    /// เรื่องนี้ต้องใช้คุณจริงๆ ซึ่งเป็นคนละข้อความกัน
    public var mayAllow: Bool

    enum CodingKeys: String, CodingKey {
        case id = "i"
        case title = "t"
        case mayAllow = "a"
    }

    public init(id: String, title: String, mayAllow: Bool) {
        self.id = id
        self.title = title
        self.mayAllow = mayAllow
    }
}

/// ก้อนเดียวที่อธิบายทั้งหน้าจอ — firmware วาดจากสิ่งนี้อย่างเดียว ไม่เก็บสถานะเอง
/// ต้องพอดี 1 MTU (517) เสมอ ดู `encoded(maxBytes:)`
public struct Snapshot: Codable, Equatable, Sendable {
    public var clock: String
    public var date: String
    public var overflow: Int
    public var sessions: [SessionSnap]
    public var cards: [CardSnap]
    /// จำนวน card ที่มีอยู่จริงแต่ไม่ได้ส่ง — จอวาดได้แค่ 2 ใบ
    /// การ์ดที่หายไปเงียบๆ คือการเตือนที่หายไป ต้องเหลือร่องรอยว่ายังมีอีก
    public var cardOverflow: Int
    /// `[session, weekly]` เสมอเมื่อมี — `nil` แปลว่าไม่เคยได้ข้อมูลเลย
    /// ซึ่งบอร์ดตีความว่า "ถอยไปเป็นนาฬิกาตั้งโต๊ะ" ไม่ใช่ "วาดโครงเปล่า"
    public var usage: [UsageSnap]?
    /// จำนวนครั้งที่มี session *เข้าสู่* สถานะที่ต้องการคน นับตั้งแต่ daemon เริ่มทำงาน
    ///
    /// เป็น id ของเหตุการณ์ ไม่ใช่สถานะ: บอร์ดเด้งกลับหน้ามาสคอตเมื่อเลขนี้โตขึ้นเท่านั้น
    /// ถ้าดูจากสถานะแทน จอจะถูกกระชากกลับทุก snapshot ตลอดสิบนาทีที่คำขออนุญาตค้างอยู่
    /// และเรื่องใหม่ของ session ที่สอง (ซึ่งไม่เปลี่ยนสถานะรวมเลย) จะไม่ได้เด้งสักครั้ง
    public var attention: Int
    /// คำขออนุญาตที่กำลังรอมือคน — `nil` คือไม่มีใครถามอะไรอยู่ ซึ่งเป็นสภาพปกติ
    ///
    /// ใบเดียวเสมอ ไม่ใช่รายการ: จอถามได้ทีละคำถาม และคำถามที่ซ้อนกันสองใบบนจอที่
    /// ถูกเหลือบมองคือทางที่ทำให้กดผิดใบ · ใบที่เหลือรอคิว หรือหมดเวลาไปเองตามปกติ
    public var ask: AskSnap?

    enum CodingKeys: String, CodingKey {
        case clock = "c"
        case date = "d"
        case overflow = "o"
        case sessions = "s"
        case cards = "n"
        case cardOverflow = "m"
        case usage = "u"
        case attention = "a"
        case ask = "q"
    }

    public init(
        clock: String,
        date: String,
        overflow: Int = 0,
        sessions: [SessionSnap] = [],
        cards: [CardSnap] = [],
        cardOverflow: Int = 0,
        usage: [UsageSnap]? = nil,
        attention: Int = 0,
        ask: AskSnap? = nil
    ) {
        self.clock = clock
        self.date = date
        self.overflow = overflow
        self.sessions = sessions
        self.cards = cards
        self.cardOverflow = cardOverflow
        self.usage = usage
        self.attention = attention
        self.ask = ask
    }
}

public enum Wire {
    /// ขนาดสูงสุดที่เขียนลง GATT characteristic ได้ในครั้งเดียว
    /// MTU 517 หัก ATT header 3 ไบต์ แล้วเผื่อไว้อีกหน่อย
    public static let maxPayload = 500

    public static func encoder() -> JSONEncoder {
        let e = JSONEncoder()
        // sortedKeys ไม่ใช่เรื่องความสวยงาม: ถ้าลำดับคีย์สุ่มไปเรื่อยๆ
        // การเทียบว่า "snapshot เปลี่ยนไหม" จะจริงทุกครั้ง แล้วบอร์ดโดนยิงทุกวินาที
        e.outputFormatting = [.withoutEscapingSlashes, .sortedKeys]
        return e
    }
}

extension Snapshot {
    /// encode แล้วบีบข้อความให้พอดี `maxBytes` — ไม่มี chunking บนสาย
    /// ลำดับการตัด: body ของ card → title ของ card → ตัด card ทิ้งจากใบล่างสุด
    /// sessions ไม่เคยถูกตัดทิ้ง เพราะมันคือสิ่งที่จอนี้มีไว้แสดง
    public func encoded(maxBytes: Int = Wire.maxPayload) throws -> Data {
        let encoder = Wire.encoder()
        var copy = self
        var data = try encoder.encode(copy)
        if data.count <= maxBytes { return data }

        for limit in [40, 28, 18, 10, 0] {
            copy.cards = copy.cards.map {
                CardSnap(title: $0.title, body: Text.clip($0.body, to: limit), kind: $0.kind)
            }
            data = try encoder.encode(copy)
            if data.count <= maxBytes { return data }
        }
        for limit in [24, 16, 10] {
            copy.cards = copy.cards.map {
                CardSnap(title: Text.clip($0.title, to: limit), body: $0.body, kind: $0.kind)
            }
            data = try encoder.encode(copy)
            if data.count <= maxBytes { return data }
        }
        while !copy.cards.isEmpty {
            copy.cards.removeLast()
            // ใบที่ถูกตัดเพราะ MTU ล้นก็ยังต้องนับ — จอต้องบอกได้ว่ามีอีกกี่ใบ
            // ไม่ว่ามันหายไปเพราะจอวาดไม่พอหรือเพราะสายส่งไม่พอ
            copy.cardOverflow += 1
            data = try encoder.encode(copy)
            if data.count <= maxBytes { return data }
        }
        // โควตาตกก่อน session — session คือเหตุผลที่จอนี้มีอยู่ ส่วนโควตายังดูได้จาก
        // statusline บนจอคอม การหายไปของมันจึงไม่ทำให้อุปกรณ์ไร้ประโยชน์
        if copy.usage != nil {
            copy.usage = nil
            data = try encoder.encode(copy)
            if data.count <= maxBytes { return data }
        }
        return data
    }
}

// MARK: - คำขออนุญาตที่รอคำตอบ

/// คำตอบที่เดินทางกลับไปหา hook ที่ค้างรออยู่บนสาย
public struct Decision: Codable, Equatable, Sendable {
    public enum Answer: String, Codable, Equatable, Sendable {
        case allow
        case deny
        /// ไม่มีคำตอบจากที่นี่ ให้ Claude Code ถามที่ terminal ตามปกติ
        ///
        /// เป็นคำตอบของ *ทุก* ทางที่ไม่ราบรื่น ไม่ใช่กรณีพิเศษที่ต้องจัดการแยก:
        /// daemon ไม่ได้รัน, บอร์ดไม่ได้ต่อ, ไม่มีใครแตะจอ, เฟิร์มแวร์ส่งของแปลกมา
        /// — ปลายทางเดียวกันหมดคือคำถามเดิมที่คีย์บอร์ด ซึ่งคือพฤติกรรมก่อนมีฟีเจอร์นี้
        case ask
    }

    public var answer: Answer

    public init(_ answer: Answer) { self.answer = answer }

    enum CodingKeys: String, CodingKey {
        case answer = "d"
    }
}

/// คำสั่งถึง daemon ที่ไม่ได้มาจาก hook — ตอนนี้มีเรื่องเดียวคือการตัดสินคำขอ
///
/// ฟิลด์ทั้งสองไม่ใช่ optional โดยตั้งใจ: `HookEvent` กับ `Control` วิ่งบนสายเส้นเดียวกัน
/// และตัวที่ถอดได้จากทุกอย่างจะกลืนของที่ไม่ใช่ของมัน
public struct Control: Codable, Equatable, Sendable {
    /// id ของคำขอ ตามที่ daemon แจกไว้
    public var decide: String
    public var allow: Bool

    public init(decide: String, allow: Bool) {
        self.decide = decide
        self.allow = allow
    }
}
