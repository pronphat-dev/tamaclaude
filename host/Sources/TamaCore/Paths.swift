import Foundation

/// ที่อยู่ไฟล์ทั้งหมดของฝั่ง Mac
public enum Paths {
    public static var home: URL {
        URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true)
    }

    public static var stateDir: URL {
        home.appendingPathComponent(".tamaclaude", isDirectory: true)
    }

    /// Unix socket ที่ hook คุยกับ daemon
    /// sun_path จำกัด 104 ไบต์ พาธนี้สั้นพอเสมอเพราะอยู่ใต้ home
    public static var socket: URL {
        stateDir.appendingPathComponent("daemon.sock")
    }

    public static var toolConfig: URL {
        stateDir.appendingPathComponent("tools.json")
    }

    /// จังหวะเวลาของท่ามาสคอต — ค่าที่ควรลองปรับได้โดยไม่ต้อง build ใหม่
    /// รูปแบบไฟล์อยู่ที่ `Timings.load`
    public static var timingConfig: URL {
        stateDir.appendingPathComponent("timings.json")
    }

    /// session key ของ claude.ai — credential เต็มบัญชี ต้องเป็น mode 600
    /// อยู่ใต้ state dir ไม่ใช่ใน repo ไหน และไม่เคยผ่าน argv หรือ env
    public static var sessionKey: URL {
        stateDir.appendingPathComponent("session-key")
    }

    /// key ของ Finnhub สำหรับหน้าหุ้น — กติกาไฟล์เดียวกับ `sessionKey` (mode 600,
    /// ไม่ผ่าน argv, ไม่ผ่าน env, ไม่ลง log) แม้จะเป็น credential ที่เล็กกว่ามาก:
    /// มันคือโควตาของผู้ใช้ และกฎที่มีข้อยกเว้นคือกฎที่ไม่มีใครจำได้ว่าใช้กับใบไหน
    public static var finnhubKey: URL {
        stateDir.appendingPathComponent("finnhub-key")
    }

    /// กุญแจปิดผนึกเฟรมบน LAN — 32 ไบต์เป็น hex, mode 600 เหมือน `sessionKey`
    /// คนละอย่างกับ `sessionKey` โดยสิ้นเชิง: อันนี้เปิดได้แค่ช่องคุยกับบอร์ด (`LanKey`)
    public static var lanKey: URL {
        stateDir.appendingPathComponent("lan-key")
    }

    public static var log: URL {
        stateDir.appendingPathComponent("daemon.log")
    }

    /// สคริปต์ที่เรายึดช่อง `statusLine.command` ไว้ — เขียน cache แล้วส่งงานวาดต่อ
    public static var statusline: URL {
        stateDir.appendingPathComponent("statusline.sh")
    }

    /// cache โควตาที่ statusline เขียน — ที่อยู่เดิมของ Claude Usage.app
    /// ใช้ที่เดียวกันเพื่อให้ statusline เดิมของผู้ใช้อ่านต่อได้โดยไม่ต้องแก้อะไร
    public static var usageCache: URL {
        home
            .appendingPathComponent(".claude", isDirectory: true)
            .appendingPathComponent(".statusline-usage-cache")
    }

    @discardableResult
    public static func ensureStateDir() -> Bool {
        (try? FileManager.default.createDirectory(
            at: stateDir, withIntermediateDirectories: true)) != nil
    }
}

/// log แบบง่ายที่สุดที่ยังใช้งานได้ — เขียน stderr เสมอ
///
/// เขียนลงไฟล์ด้วยเมื่อเปิด `toFile` เพราะตอนถูกปล่อยผ่าน LaunchServices (`open`)
/// ไม่มีใครเห็น stderr เลย ซึ่งเป็นวิธีเดียวที่ขอสิทธิ์ Bluetooth ได้สำเร็จ
public enum Log {
    public nonisolated(unsafe) static var verbose = false
    public nonisolated(unsafe) static var toFile = false

    private static let lock = NSLock()

    /// เวลาหน้าบรรทัด — ของไฟล์เท่านั้น ไม่ใช่ของ stderr
    ///
    /// คนที่ดู stderr ดูสดๆ อยู่แล้ว เวลาที่ติดมากับทุกบรรทัดจึงเป็นแค่เสียงรบกวน ส่วนไฟล์
    /// ถูกอ่านทีหลังเสมอ และคำถามแรกของคนที่เปิดมันคือ "บรรทัดนี้เมื่อไหร่" ซึ่งเมื่อก่อน
    /// ตอบไม่ได้เลย · แย่กว่านั้น ไฟล์ถูกลบทั้งก้อนเมื่อโตเกิน 1MB ช่วงเวลาที่มันครอบคลุม
    /// จึงเดาจากขนาดหรืออายุไฟล์ไม่ได้ด้วย
    ///
    /// เวลาท้องถิ่น ไม่ใช่ UTC — คนที่อ่านไฟล์นี้นั่งอยู่หน้าเครื่องเดียวกับที่เขียนมัน
    ///
    /// `yyyy` ไม่ใช่ `YYYY` · `YYYY` คือปีของ *สัปดาห์* ซึ่งต่างกันปีละไม่กี่วันรอบปีใหม่
    /// และเป็นวันที่ไม่มีใครนั่งอ่าน log — บั๊กแบบนี้รอดไปได้เป็นปีก่อนจะมีคนเห็น
    private static let clock: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return f
    }()

    /// เวลาที่จะไปอยู่หน้าบรรทัดในไฟล์ — แยกออกมาให้เทสต์จับรูปแบบได้
    public static func stamp(_ date: Date = Date()) -> String { clock.string(from: date) }

    public static func info(_ msg: @autoclosure () -> String) {
        let line = "[tamaclaude] \(msg())\n"
        FileHandle.standardError.write(Data(line.utf8))
        guard toFile else { return }
        // จับเวลาตอน *เกิดเหตุ* ไม่ใช่ตอนได้คิวเขียน — บรรทัดที่รอ lock อยู่คือบรรทัดที่
        // มีอะไรเกิดขึ้นพร้อมกันหลายอย่าง ซึ่งเป็นตอนที่ลำดับเวลามีความหมายที่สุดพอดี
        let stamped = "\(stamp()) \(line)"
        lock.lock()
        defer { lock.unlock() }
        let url = Paths.log
        // ตัดทิ้งเมื่อโตเกิน 1MB — log ที่โตไม่หยุดคือปัญหาถัดไปที่ไม่อยากได้
        if let size = try? FileManager.default.attributesOfItem(atPath: url.path)[.size]
            as? Int, size > 1 << 20 {
            try? FileManager.default.removeItem(at: url)
        }
        if let handle = try? FileHandle(forWritingTo: url) {
            handle.seekToEndOfFile()
            handle.write(Data(stamped.utf8))
            try? handle.close()
        } else {
            try? Data(stamped.utf8).write(to: url)
        }
    }

    public static func debug(_ msg: @autoclosure () -> String) {
        guard verbose else { return }
        info(msg())
    }
}
