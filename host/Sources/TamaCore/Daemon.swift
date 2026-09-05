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

        let server = SocketServer(path: Paths.socket) { [weak self] line in
            self?.handle(line)
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

    private func handle(_ line: Data) {
        work.async { [weak self] in
            guard let self else { return }
            do {
                let event = try JSONDecoder().decode(HookEvent.self, from: line)
                Log.debug("event \(event.hookEventName) \(event.toolName ?? "")")
                self.store.apply(event, now: Date())
                self.onEvent?(event)
                self.publish()
            } catch {
                Log.debug("bad event: \(error)")
            }
        }
    }

    /// หนึ่งจังหวะของนาฬิกา — transport ได้เวลาปัจจุบันก่อน แล้วค่อยคิดภาพใหม่
    ///
    /// เรียงลำดับนี้เพราะ tick อาจสลับทางเดิน ซึ่งจะล้าง `lastSent` ผ่าน `onConnect`
    /// การ publish ก่อนแล้วค่อย tick จะทำให้ snapshot ก้อนแรกของทางใหม่ตกหายไปหนึ่งจังหวะ
    private func pulse() {
        let now = Date()
        for t in transports { t.tick(now: now) }
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
