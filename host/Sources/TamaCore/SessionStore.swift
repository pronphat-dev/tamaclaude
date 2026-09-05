import Foundation

/// ค่าเวลาทั้งหมดของตรรกะสถานะ รวมไว้ที่เดียวเพื่อให้เทสต์ตั้งค่าได้
public struct Timings: Sendable {
    /// Stop แล้วเงียบเกินเท่านี้ = ถือว่าต้องเตือนผู้ใช้ด้วยการ์ด
    public var stopAlert: TimeInterval = 45
    /// Stop แล้วผ่านไปเท่านี้ = มาสคอตขึ้นท่ารอคำตอบ และจอถูกดึงกลับมาหามาสคอต
    ///
    /// แยกจาก `stopAlert` เพราะสองเรื่องนี้ราคาไม่เท่ากัน: ท่ารอคือการเปลี่ยนรูปของตัวที่
    /// ยืนอยู่บนจออยู่แล้ว ส่วนการ์ดคือแถวใหม่ที่มาแย่งที่ยืนของคนอื่น
    ///
    /// ตั้งเท่ากับ `celebrate` โดยตั้งใจ — ท่าดีใจจบแล้วต่อด้วยท่ารอทันที ไม่มี idle คั่น:
    /// Claude Code ไม่ยิง hook ใดเลยตอนถามคำถามกลางเทิร์น (`AskUserQuestion` กับ
    /// `ExitPlanMode` ไม่มี PreToolUse/PostToolUse เป็นของตัวเอง) เทิร์นที่จบด้วยคำถาม
    /// จึงแยกจากเทิร์นที่จบเฉยๆ ไม่ได้เลยจากสายเหตุการณ์ ทางเดียวที่ไม่ทำให้ผู้ใช้พลาด
    /// คำถามคือถือว่าทุกเทิร์นที่จบแปลว่าถึงตาเขา
    public var stopWaiting: TimeInterval = 5
    /// ท่าดีใจหลังงานจบ ก่อนกลับไป idle — อย่าตั้งต่ำกว่า `minPose` เพราะ minPose จะยืดให้เองอยู่ดี
    public var celebrate: TimeInterval = 5
    /// ท่าหนึ่งต้องอยู่บนจออย่างน้อยเท่านี้ ก่อนยอมให้ท่าถัดไปแทน
    ///
    /// Read/Edit ส่วนใหญ่จบใน ~100 มิลลิวินาที ถ้าเปลี่ยนท่าตามเหตุการณ์ทันที
    /// ท่า reading/writing จะโผล่สั้นกว่าหนึ่งเฟรมของบอร์ด (tick 1 วิ + ดีเลย์ BLE)
    /// ผู้ใช้จึงเห็นแต่ thinking ตลอด ทั้งที่ตรรกะข้างในถูกแล้ว
    public var minPose: TimeInterval = 5
    /// ท่าเดินเข้ามาของ session ใหม่
    public var entering: TimeInterval = 1.2
    /// ท่ามุดหายก่อนหายจากจอ
    public var leaving: TimeInterval = 1.2
    /// ไม่มีความเคลื่อนไหวเกินเท่านี้ = มาสคอตนอน
    public var sleep: TimeInterval = 300
    /// ไม่มีความเคลื่อนไหวเกินเท่านี้ = ทิ้ง session ไปเลย (กัน session ค้างจาก crash)
    public var evict: TimeInterval = 3600
    /// การ์ดหายเองเมื่อไม่มีใครสนใจ
    public var cardTTL: TimeInterval = 600

    public init() {}
}

/// สิ่งที่ session กำลังทำ — เก็บแค่นี้ ส่วนสถานะภาพคำนวณจากมันบวกเวลา
enum Activity: Equatable {
    case idle
    case thinking
    case tool(VisualState)
    case waiting  // Notification: Claude รอผู้ใช้ตอบ
    case failed   // StopFailure
}

struct Session {
    var id: String
    /// ชื่อโฟลเดอร์ดิบ ยังไม่ผ่าน `Text.fit` — การตัดเกิดที่ `snapshot()` ที่เดียว
    ///
    /// เคยตัดตั้งแต่ตรงนี้ แต่ป้ายที่มีเลขนับต่อท้าย ("x2") ต้องรู้ว่าเหลือที่เท่าไรก่อนตัด
    /// และ fit ซ้ำบนข้อความที่ตัดแล้วเป็นไปไม่ได้: ร่างไทยที่ประกอบแล้วอยู่ใน PUA
    /// ซึ่ง `Text.sanitize` ทิ้งทั้งหมด ชื่อโปรเจกต์ภาษาไทยจะกลายเป็นบรรทัดว่าง
    var project: String
    var activity: Activity = .thinking
    var startedAt: Date
    var lastActivity: Date
    /// เวลาที่ Stop ยิงและยังไม่มี prompt ใหม่ — ใช้ตัดสินเกณฑ์เตือน 45 วิ
    var stoppedAt: Date?
    var celebrateUntil: Date?
    var endingAt: Date?
    var subagents: Int = 0
    /// process ของ Claude Code ที่เป็นเจ้าของ session นี้ — nil = ไม่รู้ (ดู `ProcessTree`)
    var owner: ProcessHandle?
    /// ท่าที่อยู่บนจอตอนนี้ และเวลาที่มันขึ้นจอ — ใช้บังคับเวลาขั้นต่ำต่อท่า
    var posed: VisualState?
    var posedAt: Date = .distantPast
    /// ตอนนี้ยกมือขออยู่ไหม — ตัวที่ทำให้ "ยังรออยู่" ต่างจาก "เพิ่งเริ่มรอ"
    var raisedHand = false

    /// ท่าที่ "ควรจะเป็น" ตามสถานะจริง ณ วินาทีนี้ — ยังไม่ผ่านการหน่วง
    func visualState(now: Date, t: Timings) -> VisualState {
        if endingAt != nil { return .leaving }  // prune() เป็นคนเอาออกเมื่อท่าจบ
        if now < startedAt + t.entering { return .entering }
        switch activity {
        case .failed: return .error
        case .waiting: return .waiting
        case .tool, .thinking:
            // การกระจายงานชนะเครื่องมือ: ตอน subagent วิ่ง tool ที่เห็นเป็นของลูกน้อง
            // ไม่ใช่ของ session หลัก — "กำลังคุมงานอยู่" จึงตรงความจริงกว่า
            // (แต่แพ้ error กับ waiting ข้างบน: พังกับต้องการมือคน สำคัญกว่า)
            if subagents > 0 { return .conducting }
            if case .tool(let s) = activity { return s }
            return .thinking
        case .idle:
            if let c = celebrateUntil, now < c { return .celebrate }
            // นอนชนะการทวงถาม: ถ้าเงียบมาเป็นนาทีแล้ว ท่ายืนทวงตลอดกาลไม่ได้สื่ออะไรเพิ่ม
            // การเตือนยังอยู่ในรูปการ์ด ซึ่งเป็นคนละช่องทางกับท่าของมาสคอต
            if now >= lastActivity + t.sleep { return .sleeping }
            if let s = stoppedAt, now >= s + t.stopWaiting { return .waiting }
            return .idle
        }
    }

    /// ท่าที่แสดงจริง: ท่าดิบที่ถูกหน่วงไว้ให้อยู่ครบ `minPose` ก่อนเปลี่ยนตัว
    ///
    /// ท่าที่ถูกข้ามระหว่างหน่วงจะหายไปเลย ไม่เข้าคิว — คิวจะทำให้มาสคอตเล่าอดีต
    /// ช้ากว่าความจริงเรื่อยๆ เมื่อเครื่องมือยิงรัว ซึ่งแย่กว่าการตกท่าไปบางท่า
    mutating func displayState(now: Date, t: Timings) -> VisualState {
        let raw = visualState(now: now, t: t)
        func latch() -> VisualState {
            posed = raw
            posedAt = now
            return raw
        }
        // ท่าเข้า/ออกมีนาฬิกาของตัวเองอยู่แล้ว จึงไม่หน่วง ทั้งขาเข้าและขาออกจากมัน
        // (และ prune นับเวลาท่ามุดหายจาก endingAt ไม่ใช่จากจอ ถ้าหน่วงจะโดนลบก่อนได้แสดง)
        if raw == .entering || raw == .leaving || posed == .entering { return latch() }
        guard let cur = posed else { return latch() }
        if cur == raw { return cur }
        // ท่าคิดคือการไม่มีข่าว ไม่ใช่ข่าว — มันจึงกันที่ให้ตัวเองไม่ได้
        //
        // ทุกเทิร์นเริ่มที่ `thinking` (UserPromptSubmit) และกลับมาที่นี่ทุกครั้งที่เครื่องมือจบ
        // ถ้ามันหน่วงได้เท่าท่าอื่น มันจะเป็นตัวที่ล็อกจอไว้เกือบตลอดเวลา: PreToolUse ตัวถัดมา
        // มักมาถึงภายในไม่ถึงวินาที ซึ่งเร็วกว่า `minPose` เสมอ และ priority ก็เท่ากัน
        // แทรกไม่ได้ · ผลคือท่า reading/writing/building/searching ไม่เคยได้ขึ้นจอเลยสักครั้ง
        // ทั้งที่ตรรกะข้างในถูกมาตลอด — การหน่วงที่มีไว้กันท่ากระพริบ กลับกลายเป็นตัวกลบ
        // ทุกท่าที่มันควรจะปกป้อง
        //
        // ขากลับยังหน่วงตามปกติ (cur เป็นท่าเครื่องมือ, raw เป็น thinking → รอครบเวลา)
        // ท่าเครื่องมือจึงยังอยู่ครบห้าวินาทีเหมือนเดิม ซึ่งคือสิ่งที่ `minPose` ตั้งใจจะทำ
        if cur == .thinking { return latch() }
        // เรื่องด่วนกว่าแทรกได้ทันที (พัง/ต้องการมือคน) ที่เหลือรอให้ท่าปัจจุบันอยู่ครบเวลา
        if raw.priority > cur.priority || now >= posedAt + t.minPose { return latch() }
        return cur
    }
}

/// หนึ่ง slot บนจอ = ทุก session ของโปรเจกต์เดียวกันรวมกัน
struct SlotGroup {
    /// ชื่อดิบ ใช้เทียบกับหัวการ์ด — ไม่ใช่สิ่งที่ขึ้นจอ ดู `label`
    var project: String
    var state: VisualState
    var lastActivity: Date
    var count: Int

    /// ป้ายใต้มาสคอต — ชื่อโปรเจกต์ ต่อท้ายด้วยเลขนับเมื่อกลุ่มมีมากกว่าหนึ่งตัว
    ///
    /// เลขนับกันที่ของตัวเองไว้ก่อนแล้วชื่อจึงถูกตัดให้พอดีที่เหลือ ("x2" จึงไม่มีวันหลุด
    /// ขอบป้าย) · ใช้ "x" ไม่ใช่ U+00D7 เพราะฟอนต์บนบอร์ดไม่มีตัวนั้น และ `Text.sanitize`
    /// จะทิ้งมันเงียบๆ เหลือ "tamaclaude 2" ที่อ่านได้ว่าเป็นชื่อโปรเจกต์
    var label: String {
        guard count > 1 else { return Text.fit(project, to: Text.Limit.project) }
        let tail = " x\(count)"
        return Text.fit(project, to: Text.Limit.project, reserving: tail) + tail
    }
}

struct StoredCard {
    var sessionId: String?
    var title: String
    var body: String
    var kind: CardKind
    var createdAt: Date
}

/// ตัวจริงของ daemon: กินเหตุการณ์จาก hook แล้วคายภาพหน้าจอออกมา
///
/// ไม่มี timer อยู่ข้างใน — เวลาถูกป้อนเข้ามาทุกครั้ง จึงเทสต์ได้ทั้งหมดโดยไม่ต้องรอจริง
public final class SessionStore {
    public var toolMap: ToolMap
    public var timings: Timings
    /// จำนวน slot บนจอ ที่เหลือถูกนับเป็น "+N"
    ///
    /// ต้องตรงกับ `[slots] count` ใน tools/layout.toml เสมอ — ส่งเกินไปแล้วบอร์ดตัดทิ้งเงียบๆ
    /// (ct_model.c) และ "+N" จะนับต่ำกว่าจริง คือโกหกว่ามองเห็นครบทุก session
    public let slotCount: Int
    /// ถาม kernel ว่าเจ้าของ session ยังอยู่ไหม — แทนที่ได้เพื่อให้เทสต์ไม่ต้องฆ่า process จริง
    public var isProcessAlive: (ProcessHandle) -> Bool = { ProcessTree.isAlive($0) }

    private var order: [String] = []  // ลำดับซ้าย->ขวา ต้องนิ่ง = ลำดับที่ session เกิด
    private var sessions: [String: Session] = [:]
    private var cards: [StoredCard] = []
    /// เลขเหตุการณ์ที่ต้องการคน — ขึ้นทีละหนึ่งตอนมีคน *เริ่ม* รอ ไม่ใช่ตลอดเวลาที่ยังรออยู่
    /// (ดู `Snapshot.attention` ว่าทำไมต้องเป็นเลขนับ ไม่ใช่ธงบอกสถานะ)
    private var attention = 0

    public init(toolMap: ToolMap = .default, timings: Timings = Timings(), slotCount: Int = 3) {
        self.toolMap = toolMap
        self.timings = timings
        self.slotCount = slotCount
    }

    // MARK: - รับเหตุการณ์

    public func apply(_ e: HookEvent, now: Date = Date()) {
        let id = e.sessionId
        if sessions[id] == nil {
            // เหตุการณ์แรกที่เห็นเป็นตัวสร้าง session แม้จะไม่ใช่ SessionStart
            // (daemon อาจเพิ่งสตาร์ททีหลัง session)
            sessions[id] = Session(
                id: id,
                project: e.project,
                startedAt: now,
                lastActivity: now
            )
            order.append(id)
        }
        guard var s = sessions[id] else { return }
        s.lastActivity = now
        s.endingAt = nil
        // เขียนทับได้เสมอ ไม่ใช่เขียนครั้งเดียว: ผู้ใช้ `--resume` session เดิมได้
        // ซึ่งได้ process ตัวใหม่แต่ id เดิม ถ้ายึดตัวแรกไว้ session ที่ฟื้นมาจะถูกฆ่าทิ้งทันที
        if let o = e.owner { s.owner = o }
        if let cwd = e.cwd, !cwd.isEmpty {
            s.project = e.project
        }

        switch e.hookEventName {
        case "SessionStart":
            s.startedAt = now
            s.activity = .idle
            s.stoppedAt = nil
            s.subagents = 0

        case "UserPromptSubmit":
            s.activity = .thinking
            s.stoppedAt = nil
            s.celebrateUntil = nil
            // เทิร์นใหม่ = ล้างตัวนับที่ค้างจากเทิร์นก่อน (SubagentStop ที่หายไปตอน
            // daemon ไม่ได้รัน จะทำให้ session ติดท่า conducting ตลอดกาลถ้าไม่ล้าง)
            s.subagents = 0
            dismissCards(for: id)

        case "PreToolUse":
            s.activity = .tool(toolMap.state(for: e.toolName ?? ""))
            s.stoppedAt = nil
            // ขออนุญาตแล้วได้ไปต่อ = คำขอนั้นตายแล้ว การ์ดต้องไม่ค้างจนหมดอายุเอง
            // (PermissionRequest ยิง Notification ทีหลัง PreToolUse ของตัวมันเอง
            //  การ์ดของรอบนี้จึงไม่โดนล้างทิ้งก่อนผู้ใช้เห็น)
            dismissCards(for: id)

        // เครื่องมือที่พังจบด้วยเหตุการณ์ของตัวเอง ไม่ใช่ PostToolUse — ถ้าไม่ฟังไว้
        // มาสคอตจะค้างท่าของเครื่องมือตัวนั้นจนกว่าจะมีอย่างอื่นบังเอิญมาแทน
        // (PostToolBatch ปิดท้ายชุดที่ยิงขนานกัน ตัวสุดท้ายที่จบเป็นตัวบอกว่าหมดชุดจริง)
        case "PostToolUse", "PostToolUseFailure", "PostToolBatch":
            s.activity = .thinking
            dismissCards(for: id)

        case "PreCompact", "PostCompact":
            s.activity = .thinking

        case "SubagentStart":
            s.subagents += 1
            s.activity = .thinking

        case "SubagentStop":
            s.subagents = max(0, s.subagents - 1)

        // สี่ชื่อ เรื่องเดียวกัน: เดินต่อเองไม่ได้ถ้าไม่มีมือคน
        //
        // Claude Code รุ่นใหม่ไม่ได้ส่งคำขออนุญาตมาทาง `Notification` ทางเดียวอีกแล้ว —
        // `PermissionRequest` เป็นเหตุการณ์ของตัวเอง, `Elicitation` คือ MCP ที่ขอคำตอบ
        // กลางการเรียกเครื่องมือ, `TeammateIdle` คือเพื่อนร่วมทีมที่ค้างรออยู่
        // ชื่อที่รุ่นเก่าไม่รู้จักก็แค่ไม่เคยยิง ไม่มีใครเสียหาย
        case "Notification", "PermissionRequest", "Elicitation", "TeammateIdle":
            s.activity = .waiting
            s.stoppedAt = nil
            push(
                StoredCard(
                    sessionId: id,
                    title: s.project,
                    body: e.message ?? "needs your input",
                    kind: .alert,
                    createdAt: now
                ))

        // ตัดสินใจไปแล้ว = คำขอนั้นตายแล้ว · ทางปฏิเสธไม่มี PostToolUse ตามมาล้างให้
        // การ์ดจึงต้องถูกเก็บตรงนี้ ไม่ใช่ปล่อยให้ค้างจนหมดอายุเอง
        case "PermissionDenied", "ElicitationResult":
            s.activity = .thinking
            s.stoppedAt = nil
            dismissCards(for: id)

        case "Stop":
            s.activity = .idle
            s.stoppedAt = now
            s.celebrateUntil = now + timings.celebrate
            // main loop จบแล้ว จะมี subagent ค้างจริงไม่ได้
            s.subagents = 0
            // เทิร์นจบแล้ว คำขออนุญาตของเทิร์นนั้นหมดความหมาย ต่อให้ผู้ใช้กดปฏิเสธ
            // (ทางนั้นไม่มี PostToolUse มาล้างให้) — การเตือนที่เหลือมาทาง stopAlerts
            dismissCards(for: id)

        case "StopFailure", "SubagentStopFailure":
            s.activity = .failed
            s.stoppedAt = nil
            s.celebrateUntil = nil
            push(
                StoredCard(
                    sessionId: id,
                    title: s.project,
                    body: e.reason ?? e.message ?? "stopped with an error",
                    kind: .alert,
                    createdAt: now
                ))

        case "SessionEnd":
            s.endingAt = now
            s.activity = .idle
            s.subagents = 0
            dismissCards(for: id)

        default:
            break
        }
        sessions[id] = s
    }

    private func push(_ card: StoredCard) {
        // ใบใหม่สุดอยู่บน และ session หนึ่งมีการ์ดค้างได้ใบเดียว
        cards.removeAll { $0.sessionId != nil && $0.sessionId == card.sessionId }
        cards.insert(card, at: 0)
    }

    private func dismissCards(for sessionId: String) {
        cards.removeAll { $0.sessionId == sessionId }
    }

    // MARK: - เวลาเดิน

    /// ทิ้ง session ที่จบท่ามุดหายแล้ว/ค้างนานเกิน และการ์ดที่หมดอายุ
    public func prune(now: Date = Date()) {
        for (id, s) in sessions {
            var s = s
            // ปิดหน้าต่าง terminal = process โดน SIGHUP ทันที `SessionEnd` ไม่มีวันยิง
            // จึงเช็คตัว process เองแทนที่จะรอ hook ที่ไม่มา · ให้มุดหายเหมือนจบปกติ
            // ไม่ใช่หายวับ เพราะจากตาผู้ใช้มันคือการปิด session เหมือนกัน
            if s.endingAt == nil, let o = s.owner, !isProcessAlive(o) {
                s.endingAt = now
                s.activity = .idle
                s.subagents = 0
                dismissCards(for: id)
                sessions[id] = s
            }
            let ended = s.endingAt.map { now >= $0 + timings.leaving } ?? false
            let stale = now >= s.lastActivity + timings.evict
            if ended || stale {
                sessions[id] = nil
                order.removeAll { $0 == id }
                dismissCards(for: id)
            }
        }
        cards.removeAll { now >= $0.createdAt + timings.cardTTL }
    }

    /// เติมการ์ดของ session ที่ Stop แล้วเงียบเกินเกณฑ์
    ///
    /// เป็น `.done` ไม่ใช่ `.alert` — เทิร์นจบเฉยๆ ไม่มีอะไรค้างให้ตอบ ต่างจาก
    /// `Notification`/`StopFailure` ที่ไม่มีมือคนแล้วเดินต่อไม่ได้จริง ถ้าย้อมแดง
    /// เหมือนกันหมด ทุกเทิร์นที่ผู้ใช้ลุกจากโต๊ะจะได้การ์ดแดง แล้วสีแดงจะเลิกแปลว่า
    /// "ต้องมือคน" · ท่ามาสคอตยังเป็น `waiting` (นาฬิกาเหลือง = ถึงตาคุณ) เหมือนเดิม
    private func stopAlerts(now: Date) {
        for id in order {
            guard let s = sessions[id], let stopped = s.stoppedAt else { continue }
            guard now >= stopped + timings.stopAlert else { continue }
            guard !cards.contains(where: { $0.sessionId == id }) else { continue }
            push(
                StoredCard(
                    sessionId: id,
                    title: s.project,
                    body: "your turn",
                    kind: .done,
                    createdAt: now
                ))
        }
    }

    // MARK: - ภาพหน้าจอ

    public func snapshot(now: Date = Date(), calendar: Calendar = .current) -> Snapshot {
        prune(now: now)
        stopAlerts(now: now)

        // เดินท่าของทุกตัวก่อน (รวมตัวที่ตกจอ) เพื่อให้นาฬิกาหน่วงท่าเดินสม่ำเสมอ
        var live: [(project: String, state: VisualState, lastActivity: Date)] = []
        for id in order {
            guard var s = sessions[id] else { continue }
            let state = s.displayState(now: now, t: timings)
            // นับตอนขอบขาขึ้นของ *แต่ละ session* ไม่ใช่ของทั้งจอ: ตัวที่สองที่ขอความช่วยเหลือ
            // ระหว่างที่ตัวแรกยังรออยู่ ไม่ได้เปลี่ยนภาพรวมเลย แต่เป็นเรื่องใหม่ที่ต้องได้เด้ง
            let needs = state.needsHuman
            if needs && !s.raisedHand { attention += 1 }
            s.raisedHand = needs
            sessions[id] = s
            live.append((s.project, state, s.lastActivity))
        }

        // ควบ session ที่อยู่โปรเจกต์เดียวกันให้เหลือป้ายเดียว
        //
        // มาสคอตสองตัวที่มีชื่อเดียวกันยืนติดกันไม่ได้บอกอะไรที่ "ชื่อ + เลขนับ" ไม่ได้บอก
        // แต่มันกินสองในสาม slot และทำให้ชื่อเดียวถูกอ่านซ้ำในเหลือบเดียว · ท่าที่ขึ้นจอ
        // คือท่าที่สำคัญที่สุดในกลุ่ม (เสมอกันแล้วเอาตัวที่ขยับล่าสุด) เพราะกลุ่มที่มีตัวหนึ่ง
        // ยกมือขออนุญาตต้องอ่านออกทันทีว่ามีมือยกอยู่ ไม่ใช่ถูกกลบด้วยตัวที่กำลังไถ Read
        var groups: [SlotGroup] = []
        var groupOf: [String: Int] = [:]
        for s in live {
            guard let i = groupOf[s.project] else {
                groupOf[s.project] = groups.count
                groups.append(
                    SlotGroup(
                        project: s.project, state: s.state,
                        lastActivity: s.lastActivity, count: 1))
                continue
            }
            groups[i].count += 1
            let better =
                s.state.priority > groups[i].state.priority
                || (s.state.priority == groups[i].state.priority
                    && s.lastActivity > groups[i].lastActivity)
            if better { groups[i].state = s.state }
            groups[i].lastActivity = max(groups[i].lastActivity, s.lastActivity)
        }

        // เลือกตัวที่ได้ slot ตามความสำคัญ แต่ *วาด* ตามลำดับเกิดเสมอ
        // สิ่งที่ต้องนิ่งคือลำดับซ้าย->ขวา ไม่ใช่พิกัด
        let chosen: [SlotGroup]
        if groups.count <= slotCount {
            chosen = groups
        } else {
            // เรียงตาม: ความสำคัญ -> ขยับล่าสุด -> เกิดทีหลัง
            // ข้อสุดท้ายทำให้ผลนิ่ง (ไม่ขึ้นกับการเรียงที่ไม่เสถียร) และตัดตัวที่เก่าสุด
            // และเงียบสุดออกก่อน ซึ่งเป็นตัวที่ผู้ใช้น่าจะสนใจน้อยที่สุด
            let ranked = groups.enumerated().sorted { a, b in
                let pa = a.element.state.priority
                let pb = b.element.state.priority
                if pa != pb { return pa > pb }
                if a.element.lastActivity != b.element.lastActivity {
                    return a.element.lastActivity > b.element.lastActivity
                }
                return a.offset > b.offset
            }
            let keep = Set(ranked.prefix(slotCount).map { $0.offset })
            chosen = groups.enumerated().filter { keep.contains($0.offset) }.map { $0.element }
        }

        let snaps = chosen.map { SessionSnap(project: $0.label, state: $0.state) }
        // ชื่อที่มีมาสคอตยืนอยู่บนจอเดียวกันแล้ว — หัวการ์ดที่ซ้ำกับป้ายพวกนี้ถูกตัดทิ้ง
        // ก็ต่อเมื่อในเซ็ตนี้มีชื่อเดียว (ดู `card(from:onScreen:)`)
        let onScreen = Set(chosen.map(\.project))
        // "+N" นับเป็น *session* ไม่ใช่กลุ่ม: กลุ่มที่ขึ้นจอพาสมาชิกขึ้นไปครบทุกตัวแล้ว
        let seated = chosen.reduce(0) { $0 + $1.count }

        let f = DateFormatter()
        f.calendar = calendar
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "HH:mm"
        let clock = f.string(from: now)
        f.dateFormat = "EEE d MMM"
        let date = f.string(from: now)

        return Snapshot(
            clock: clock,
            date: date,
            overflow: max(0, live.count - seated),
            sessions: snaps,
            // จอวาดได้ 2 ใบ (CT_CARD_MAX ใน tools/layout.toml) — ส่งเกินมาก็ถูกทิ้ง
            // แล้วยังกิน MTU ของสิ่งที่จอใช้จริง ที่เหลือไปโผล่เป็น "+N" แทน
            cards: cards.prefix(2).map { card(from: $0, onScreen: onScreen) },
            cardOverflow: max(0, cards.count - 2),
            attention: attention
        )
    }

    /// การ์ดหนึ่งใบบนสาย — ชื่อโปรเจกต์ไม่เคยได้บรรทัดของตัวเองเมื่อมาสคอตของมันอยู่บนจอ
    ///
    /// การ์ดเดิมพูดชื่อโปรเจกต์ซ้ำกับป้ายที่อยู่เหนือมันขึ้นไปไม่ถึงร้อยพิกเซล การอ่านชื่อ
    /// เดียวกันสองครั้งไม่ได้เพิ่มอะไรในเหลือบเดียว แต่มันดันประโยคที่บอกว่า *เกิดอะไรขึ้น*
    /// ลงไปเป็นบรรทัดล่างตัวเล็กสีจาง ซึ่งเป็นบรรทัดเดียวที่ต้องอ่านออกจริงๆ
    /// · ตัดหัวออกแล้วประโยคเลื่อนขึ้นมาเป็นบรรทัดใหญ่แทน: `body` ว่าง = การ์ดบรรทัดเดียว
    /// (`ct_ui.c` ↔ `gen/screen.py` ต้องตรงกันตรงนี้) ราคาที่จ่ายคือประโยคถูกตัดที่เพดาน
    /// ของหัวการ์ด (32) แทนของเนื้อ (44) — สั้นลงแต่ตัวโตขึ้น ซึ่งคือสิ่งที่จอนี้ต้องการ
    ///
    /// **`onScreen.count == 1` ไม่ใช่แค่ `onScreen.contains`** — เหตุผลข้างบนเป็นจริง
    /// ก็ต่อเมื่อชื่อที่ตัดทิ้งชี้ไปได้ที่เดียว · มีสองโปรเจกต์ยืนอยู่พร้อมกันเมื่อไร ชื่อไม่ใช่
    /// ของซ้ำอีกต่อไป มันคือตัวชี้เป้าตัวเดียวที่มี: ลำดับการ์ดเรียงตามใหม่สุด ไม่ใช่ตาม
    /// ลำดับ slot ซ้าย→ขวา และท่ามาสคอตก็แยกให้ไม่ได้เพราะ `alert` กับ `done` เป็นท่า
    /// `waiting` ทั้งคู่ · สองใบที่เหลือแต่ประโยคจึงลอยอยู่โดยไม่มีอะไรผูกกับตัวไหนเลย
    ///
    /// **แต่ชื่อไปอยู่บรรทัดเดียวกับประโยค ไม่ใช่บรรทัดของตัวเอง** — การ์ดสองบรรทัดสูง 56px
    /// และที่มีจริงคือ 90px สองใบจึงไม่มีทางลงพร้อมกัน (56+4+56 = 116) กติกา "เก็บชื่อไว้"
    /// แบบสองบรรทัดจึงแลกความกำกวมมาด้วยการซ่อนใบที่สองไว้หลัง `+N more` ซึ่งเป็นการ
    /// แก้ปัญหาหนึ่งด้วยอีกปัญหาหนึ่ง: จอที่มีสองโปรเจกต์รออยู่ต้องบอกว่า *ทั้งสอง* รออยู่
    /// · หนึ่งบรรทัดคือ 32px สองใบลงพอดี (32+4+32 = 68)
    /// · ราคาที่จ่ายคือหางประโยคถูกตัด — ซึ่งเป็นส่วนที่มีค่าน้อยที่สุดบนจอที่ถูกเหลือบมอง
    ///
    /// · ชื่อยังอยู่เมื่อ session ของมันตกจอ ไม่งั้นการ์ดจะไม่มีทางบอกได้ว่าใครเป็นคนขอ —
    /// ตอนนั้นไม่มีมาสคอตให้แข่งด้วย การ์ดจึงกลับไปเป็นสองบรรทัดเต็มใบได้ตามเดิม
    private func card(from c: StoredCard, onScreen: Set<String>) -> CardSnap {
        guard onScreen.contains(c.title) else {
            // ไม่มีมาสคอตของโปรเจกต์นี้บนจอ — การ์ดเป็นที่เดียวที่บอกได้ว่าใครขอ
            return CardSnap(
                title: Text.fit(c.title, to: Text.Limit.cardTitle),
                body: Text.fit(c.body, to: Text.Limit.cardBody),
                kind: c.kind
            )
        }
        // ป้ายใต้มาสคอตพูดชื่อนี้อยู่แล้ว — พูดซ้ำก็ต่อเมื่อมันชี้เป้าได้จริง คือมีชื่อให้เลือก
        // มากกว่าหนึ่ง · ทั้งสองทางเป็นการ์ดบรรทัดเดียว สองใบจึงลงจอพร้อมกันเสมอ
        guard onScreen.count > 1 else {
            return CardSnap(
                title: Text.fit(c.body, to: Text.Limit.cardTitle), body: "", kind: c.kind)
        }
        // ชื่อถูกตัดก่อน ประโยคได้ที่ที่เหลือทั้งหมด — ไม่ใช่ตัดทั้งบรรทัดรวดเดียว ซึ่งทำให้
        // ชื่อโปรเจกต์ยาวๆ ตัวเดียวกินบรรทัดจนไม่เหลือคำบอกว่าเกิดอะไรขึ้นเลย
        // · `head` ไม่ใช่ `fit`: ชื่อตรงนี้ชี้ไปที่ป้ายใต้มาสคอต ไม่ได้เป็นเนื้อหาของตัวเอง
        let name = Text.head(c.title, to: Text.Limit.cardName)
        let said = Text.fit(c.body, to: Text.Limit.cardTitle, reserving: "\(name): ")
        return CardSnap(title: "\(name): \(said)", body: "", kind: c.kind)
    }

    public var sessionCount: Int { sessions.count }
}
