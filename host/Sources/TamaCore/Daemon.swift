import Foundation

/// ตัว daemon: socket -> store -> snapshot -> transports
///
/// ทุกอย่างวิ่งบนคิวเดียว (`work`) สถานะจึงไม่ต้องล็อก
public final class Daemon {
    private let store: SessionStore
    private let work = DispatchQueue(label: "tamaclaude.daemon")
    private var server: SocketServer?
    private var transports: [Transport]
    private var timer: DispatchSourceTimer?
    private var lastSent: Data?
    /// หน้าอื่นที่ไม่ใช่มาสคอต — แต่ละหน้าเดินทางเป็นเฟรมของตัวเอง (ADR-0003)
    /// แตะได้จากคิวของ daemon เท่านั้น เหมือนสถานะอื่นทั้งหมดในนี้
    private let pages = PageHub()

    /// คำขออนุญาตที่ยังไม่มีคำตอบ — id → คำขอ และวิธีตอบกลับไปหา hook ที่ค้างอยู่
    ///
    /// แตะได้จากคิว `work` เท่านั้น เหมือนสถานะอื่นทั้งหมดในนี้
    private var pending: [String: (Pending, (Data) -> Void)] = [:]

    /// คำขออนุญาตหนึ่งใบที่กำลังรอมือคน
    public struct Pending: Equatable, Sendable {
        public var id: String
        public var sessionId: String
        public var project: String
        public var tool: String
        /// จอเสนอปุ่ม Allow ให้ใบนี้ได้ไหม (`Risk`) — ปุ่ม Deny เสนอได้เสมอทุกใบ
        public var boardMayAllow: Bool
        public var askedAt: Date
    }

    /// นานที่สุดที่ยอมให้ hook ค้างรอ ก่อนปล่อยกลับไปถามที่ terminal
    ///
    /// ต้องสั้นกว่าเวลาที่ฝั่ง hook ยอมรอ (`HookClient.decisionWait`) เพื่อให้คำว่า
    /// `ask` ที่เราส่งเองเป็นตัวจบเรื่องเสมอ — ปล่อยให้ hook หมดเวลาเองได้ผลเหมือนกัน
    /// แต่ทิ้งคำขอค้างไว้ในนี้โดยไม่มีใครมาเก็บ
    public var decisionWait: TimeInterval = 25

    /// ทุกกี่วินาทีที่คำนวณ snapshot ใหม่ — สถานะหลายอย่างเกิดจากเวลาผ่านไปเฉยๆ
    /// (นอน, เกณฑ์เตือน 45 วิ, นาฬิกาเปลี่ยนนาที) ไม่ใช่จากเหตุการณ์
    private let tick: TimeInterval = 1.0

    /// เรียกทุกครั้งที่ภาพเปลี่ยน — เมนูบาร์ใช้แสดงว่ากำลังเกิดอะไรอยู่
    /// ถูกเรียกจากคิวของ daemon ไม่ใช่เธรดหลัก
    public var onPublish: ((Snapshot) -> Void)?

    /// เรียกทุกครั้งที่ได้ยิน hook — แยกจาก `onPublish` เพราะสองคำถามนี้คนละคำถาม
    ///
    /// ภาพที่ไม่เปลี่ยนไม่ได้แปลว่าไม่มีอะไรเข้ามา (เหตุการณ์ส่วนใหญ่ตกท่าไปกับ `minPose`
    /// และ `lastSent`) — "ท่อยังมีชีวิตอยู่ไหม" จึงต้องถามที่ขาเข้า ไม่ใช่ที่ขาออก
    /// ถูกเรียกจากคิวของ daemon ไม่ใช่เธรดหลัก
    public var onEvent: ((HookEvent) -> Void)?

    public init(store: SessionStore, transports: [Transport]) {
        self.store = store
        self.transports = transports
    }

    public func start() throws {
        Paths.ensureStateDir()

        for t in transports {
            t.onConnect = { [weak self] in
                // บอร์ดเพิ่งกลับมา — มันไม่จำอะไรเลย ต้องบังคับส่งใหม่ทั้งก้อน
                self?.work.async {
                    self?.lastSent = nil
                    self?.pages.forgetSent()
                }
            }
            t.start()
        }

        let server = SocketServer(path: Paths.socket) { [weak self] line, reply in
            self?.handle(line, reply: reply)
        }
        try server.start()
        self.server = server
        Log.info("listening on \(Paths.socket.path)")

        let t = DispatchSource.makeTimerSource(queue: work)
        t.schedule(deadline: .now(), repeating: tick)
        t.setEventHandler { [weak self] in self?.pulse() }
        timer = t
        t.resume()
    }

    public func stop() {
        timer?.cancel()
        timer = nil
        server?.stop()
        server = nil
    }

    private func handle(_ line: Data, reply: @escaping (Data) -> Void) {
        work.async { [weak self] in
            guard let self else { return }
            if let event = try? JSONDecoder().decode(HookEvent.self, from: line) {
                Log.debug("event \(event.hookEventName) \(event.toolName ?? "")")
                self.store.apply(event, now: Date())
                self.onEvent?(event)
                self.publish()
                self.consider(event, reply: reply)
                return
            }
            // ไม่ใช่เหตุการณ์ ก็เป็นคำสั่งตัดสินคำขอ — สองชนิดนี้ถอดสลับกันไม่ได้
            // เพราะคีย์ที่จำเป็นของแต่ละฝั่งไม่มีในอีกฝั่ง
            if let control = try? JSONDecoder().decode(Control.self, from: line) {
                self.settle(control.decide, allow: control.allow)
                return
            }
            Log.debug("line was neither an event nor a decision")
        }
    }

    // MARK: - คำขออนุญาต

    /// คำขออนุญาตเป็นเหตุการณ์เดียวที่ hook ค้างรอคำตอบ — ที่เหลือส่งแล้วปิดสายไปแล้ว
    private func consider(_ e: HookEvent, reply: @escaping (Data) -> Void) {
        guard e.hookEventName == "PermissionRequest" else { return }
        // จอที่ไม่มีใครเห็นตอบแทนใครไม่ได้ · ปล่อยกลับเดี๋ยวนี้ ดีกว่าให้ผู้ใช้นั่งมอง
        // terminal ที่ยังไม่ขึ้นคำถาม เพราะ hook ของเราถ่วงมันอยู่ยี่สิบวินาที
        guard transports.contains(where: { $0.isConnected }) else {
            reply(Self.answer(.ask))
            return
        }
        let tool = e.toolName ?? ""
        // id สั้นพอให้คนพิมพ์ตามได้ (`--decide`) และพอให้จอส่งกลับมาได้ในเฟรมเดียว
        let id = String(UUID().uuidString.prefix(8))
        pending[id] = (
            Pending(
                id: id, sessionId: e.sessionId, project: e.project, tool: tool,
                boardMayAllow: !Risk.needsKeyboard(tool: tool, input: e.toolInput),
                askedAt: Date()),
            reply
        )
        Log.info("waiting on a decision for \(tool) — id \(id)")
    }

    /// ตอบคำขอหนึ่งใบแล้วเอาออกจากรายการ · เรียกซ้ำด้วย id เดิมไม่เกิดอะไรขึ้น
    ///
    /// `allow: nil` = หมดเวลา
    private func settle(_ id: String, allow: Bool?) {
        guard let (request, reply) = pending.removeValue(forKey: id) else { return }
        guard let allow else {
            reply(Self.answer(.ask))
            return
        }
        // ด่านสุดท้ายอยู่ตรงนี้ ไม่ใช่ที่ปุ่มบนจอ — เฟิร์มแวร์รุ่นเก่า รุ่นที่ถูกแก้ หรือ
        // เฟรมที่เพี้ยนบนสายวิทยุ ต้องอนุญาตสิ่งที่ `Risk` ห้ามไว้ไม่ได้ ต่อให้ส่งคำว่า
        // allow มาก็ตาม · กติกาความปลอดภัยที่บังคับใช้ตรงปลายทางเท่านั้นคือกติกาที่
        // ขึ้นกับว่าปลายทางยังเป็นตัวที่เราเขียนอยู่หรือเปล่า
        if allow, !request.boardMayAllow {
            Log.info("the board said allow for \(request.tool), which it may not — asking you")
            reply(Self.answer(.ask))
            return
        }
        reply(Self.answer(allow ? .allow : .deny))
    }

    /// คำตอบจากบอร์ด (หรือจาก `--decide`)
    public func decide(id: String, allow: Bool) {
        work.async { [weak self] in self?.settle(id, allow: allow) }
    }

    /// คำขอที่รอเกินเวลาแล้ว ปล่อยกลับไปที่ terminal
    private func expire(now: Date) {
        for (id, entry) in pending where now >= entry.0.askedAt + decisionWait {
            Log.info("nobody answered \(entry.0.tool) in time — asking you instead")
            settle(id, allow: nil)
        }
    }

    private static func answer(_ answer: Decision.Answer) -> Data {
        (try? Wire.encoder().encode(Decision(answer))) ?? Data(#"{"d":"ask"}"#.utf8)
    }

    /// หนึ่งจังหวะของนาฬิกา — transport ได้เวลาปัจจุบันก่อน แล้วค่อยคิดภาพใหม่
    ///
    /// เรียงลำดับนี้เพราะ tick อาจสลับทางเดิน ซึ่งจะล้าง `lastSent` ผ่าน `onConnect`
    /// การ publish ก่อนแล้วค่อย tick จะทำให้ snapshot ก้อนแรกของทางใหม่ตกหายไปหนึ่งจังหวะ
    private func pulse() {
        let now = Date()
        for t in transports { t.tick(now: now) }
        expire(now: now)
        publish()
        publishPages(now: now)
    }

    // MARK: - หน้าอื่นที่ไม่ใช่มาสคอต

    /// บอร์ดประกาศว่ารู้จักหน้าไหนบ้าง (ADR-0006) — เรียกได้จากเธรดไหนก็ได้
    public func announce(_ kinds: [PageKind]) {
        work.async { [weak self] in
            guard let self else { return }
            self.pages.announce(kinds)
            Log.info("board knows \(kinds.map(\.rawValue))")
            self.publishPages(now: Date())
        }
    }

    /// มีข้อมูลใหม่ของหน้าหนึ่ง — `observedAt` คือตอนที่ Mac ได้ค่ามาจริง ซึ่งเป็น
    /// จุดตั้งต้นของ data age ไม่ใช่ตอนที่เฟรมออกจากเครื่อง
    public func submit(_ frame: any PageFrame, observedAt: Date) {
        work.async { [weak self] in
            guard let self else { return }
            self.pages.submit(frame, observedAt: observedAt)
            self.publishPages(now: Date())
        }
    }

    /// ผู้ใช้ปิดหน้านั้นแล้ว
    public func drop(_ kind: PageKind) {
        work.async { [weak self] in self?.pages.drop(kind) }
    }

    /// ผู้ใช้เปลี่ยนค่าตั้งของจอ — มีผลทันที ไม่ต้องรีสตาร์ตอะไร
    public func submit(_ plan: PagePlan) {
        work.async { [weak self] in
            guard let self else { return }
            self.pages.submit(plan)
            self.publishPages(now: Date())
        }
    }

    /// ส่งเฉพาะหน้าที่บอร์ดรู้จักและเนื้อหาเปลี่ยนจริง — data age ที่ขยับทุกวินาที
    /// ไม่นับว่าเปลี่ยน เพราะบอร์ดนับต่อเองอยู่แล้ว
    private func publishPages(now: Date) {
        let frames = pages.drain(now: now)
        guard !frames.isEmpty else { return }
        for t in transports where t.isConnected {
            for data in frames { t.send(data) }
        }
    }

    /// ส่งเมื่อภาพเปลี่ยนจริงเท่านั้น — ไม่งั้นบอร์ดโดนยิงทุกวินาทีโดยเปล่าประโยชน์
    private func publish() {
        let now = Date()
        var snapshot = store.snapshot(now: now)
        snapshot.usage = UsageReader.read(now: now)
        guard let data = try? snapshot.encoded() else { return }
        guard data != lastSent else { return }
        lastSent = data
        Log.debug("publish \(snapshot.sessions.map { "\($0.project):\($0.state.rawValue)" })"
            + " cards=\(snapshot.cards.count)")
        for t in transports where t.isConnected {
            t.send(data)
        }
        onPublish?(snapshot)
    }
}
