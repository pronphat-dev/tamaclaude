import AppKit
import Combine
import TamaCore

/// สถานะทั้งหมดที่หน้าตั้งค่าแสดง และทางออกทุกเส้นกลับไปหา `MenuBarApp`
///
/// ใบนี้เข้ามาแทนที่ตัวแปร `private let ...Field` หลายสิบตัวใน controller เดิม แต่ **กติกา
/// เดิมไม่เปลี่ยน**: ที่นี่ไม่รู้จัก BLE, ไฟล์ หรือ `UserDefaults` เลย ทุกอย่างเข้ามาทาง
/// `show*` ของ `PreferencesWindowController` และออกไปทาง closure ชุดเดิม ที่เก็บสถานะ
/// จริงยังมีที่เดียวเหมือนเดิม
///
/// `@Published` ทุกตัวเป็นสิ่งที่ view อ่านอย่างเดียว การเขียนกลับเข้ามาต้องผ่านเมธอด
/// `choose*` ข้างล่างเสมอ เพราะการเปลี่ยนค่าหนึ่งค่าคือการส่งค่า *ทั้งก้อน* ออกไป —
/// รูปเดิมของ watchlist และรายการหน้าทุกใบ
@MainActor
final class SettingsModel: ObservableObject {
    // --- ทางออกไปหา MenuBarApp (ชื่อและชนิดตรงกับของเดิมทุกตัว) ------------------
    var onSelectBoard: ((UUID?) -> Void)?
    var onBrightness: ((Int) -> Void)?
    var onInterval: ((PollInterval) -> Void)?
    var onSetSessionKey: (() -> Void)?
    var onInstallHooks: (() -> Void)?
    var onToggleStatusline: (() -> Void)?
    var onToggleLogin: (() -> Void)?
    var onToggleAutoStart: (() -> Void)?
    var onOpenLog: (() -> Void)?
    var onOpenProject: (() -> Void)?
    var onScan: (() -> Void)?
    var onJoin: ((String, String?) -> Void)?
    var onForget: ((String) -> Void)?
    var onBoardHost: ((String) -> Void)?
    var onWeather: ((WeatherSettings) -> Void)?
    var onPages: ((PageSettings) -> Void)?
    var onCrypto: ((CryptoSettings) -> Void)?
    var onStocks: ((StockSettings) -> Void)?
    var onFinnhubKey: (() -> Void)?
    var onCalendar: ((CalendarSettings) -> Void)?
    var onCalendarAccess: (() -> Void)?

    // --- Board ---------------------------------------------------------------
    @Published var boards: [Board] = []
    @Published var selectedBoard: UUID?
    @Published var brightness = 100
    @Published var linked = false
    @Published var route: LanRoute = .none
    @Published var routeDetail = ""
    @Published var boardHost = ""

    // --- Claude --------------------------------------------------------------
    @Published var interval: PollInterval = .minute
    @Published var statusline = false
    @Published var autoStart = false
    @Published var login = false
    @Published var keyState: SessionKeyState = .none
    /// บรรทัดใต้คำว่า "Hooks" — `PanelText.hooks` เป็นคนแต่ง ที่นี่แค่ถือไว้ให้ view อ่าน
    @Published var hooks = ""

    // --- Screen --------------------------------------------------------------
    @Published var pages = PageSettings()

    @Published var weather = WeatherSettings()
    @Published var weatherStatus = ""
    @Published var weatherSupported = true

    @Published var crypto = CryptoSettings()
    @Published var cryptoStatus = ""
    @Published var cryptoSupported = true

    @Published var stocks = StockSettings()
    @Published var stocksStatus = ""
    @Published var stocksSupported = true
    @Published var hasStockKey = false
    @Published var stockKeyRejected = false

    @Published var calendars = CalendarSettings()
    @Published var availableCalendars: [CalendarInfo] = []
    @Published var calendarAccess: CalendarAccess = .notDetermined
    @Published var calendarStatus = ""
    @Published var calendarSupported = true

    // --- Wi-Fi ---------------------------------------------------------------
    @Published var networks = NetworkList()
    /// เลือกวงที่บอร์ดกำลังพูดถึงให้เอง *ครั้งเดียว* — ผู้ใช้ที่พิมพ์รหัสผิดเปิดหน้านี้มาเจอ
    /// ชื่อวงอยู่บนหัวข้ออยู่แล้ว แต่ปุ่ม Connect ยังเทาเพราะไม่มีแถวไหนถูกเลือก · เขียนทับ
    /// เฉพาะตอนยังไม่มีใครเลือก ไม่งั้นสถานะที่ไหลเข้ามาทุกครั้งจะกระชากแถวที่ผู้ใช้เพิ่งกด
    @Published var wifi: WiFiStatus? {
        didSet {
            guard selectedSSID == nil, let ssid = wifi?.ssid, !ssid.isEmpty else { return }
            selectedSSID = ssid
        }
    }
    @Published var selectedSSID: String?
    @Published var password = ""

    // --- สิ่งที่แถวหนึ่งในลิสต์หน้าพูดถึงตัวเอง --------------------------------------

    /// ประโยคขวาสุดของแถว — สิ่งที่ทำให้การดริลเข้าไม่มีต้นทุน
    ///
    /// ของเดิมซ่อนคำตอบพวกนี้ไว้ท้ายแท็บของแต่ละหน้า ผู้ใช้ที่อยากรู้ว่า "ทำไมหน้าหุ้น
    /// ยังไม่ขึ้นจอ" ต้องเปิดแท็บนั้นถึงจะเจอ · ตอนนี้มันอยู่ในแถวเดียวกับสวิตช์ที่เปิดมัน
    func summary(for kind: PageKind) -> String {
        switch kind {
        case .mascot:
            return "Always on"
        case .weather:
            guard supported(.weather) else { return unsupportedShort }
            guard weather.isUsable else { return "No city yet" }
            return "\(weather.place) · °\(weather.unit.rawValue)"
        case .crypto:
            guard supported(.crypto) else { return unsupportedShort }
            guard crypto.isUsable else { return "No coins yet" }
            return crypto.coins.map { $0.uppercased() }.joined(separator: " · ")
        case .stocks:
            guard supported(.stocks) else { return unsupportedShort }
            guard hasStockKey else { return stockKeyRejected ? "Key refused" : "No key yet" }
            guard stocks.isUsable else { return "No symbols yet" }
            return stocks.symbols.joined(separator: " · ")
        case .calendar:
            guard supported(.calendar) else { return unsupportedShort }
            switch calendarAccess {
            case .denied: return "Access refused"
            case .notDetermined: return "Needs access"
            case .granted:
                let count = calendars.calendars.count
                guard count > 0 else { return "No calendars picked" }
                return count == 1 ? "1 calendar" : "\(count) calendars"
            }
        }
    }

    /// แถวนี้บอกเรื่องที่ต้องลงมือแก้ไหม — แดงมาจากระบบ ไม่ใช่สีที่สองของแอป
    ///
    /// "ยังไม่ใส่เหรียญ" ไม่ใช่ความผิดพลาด มันคือขั้นถัดไปของการตั้งค่า ส่วน "key ถูก
    /// ปฏิเสธ" กับ "firmware ไม่รู้จักหน้านี้" คือของที่พังจริงและผู้ใช้ต้องไปทำอย่างอื่น
    func isProblem(_ kind: PageKind) -> Bool {
        guard supported(kind) else { return true }
        switch kind {
        case .stocks: return stockKeyRejected
        case .calendar: return calendarAccess == .denied
        default: return false
        }
    }

    private let unsupportedShort = "Firmware too old"

    func supported(_ kind: PageKind) -> Bool {
        switch kind {
        case .mascot: return true
        case .weather: return weatherSupported
        case .crypto: return cryptoSupported
        case .stocks: return stocksSupported
        case .calendar: return calendarSupported
        }
    }

    /// หน้านี้มีอะไรให้ตั้งไหม — หน้ามาสคอตไม่มี จึงไม่มีลูกศรดริลเข้า
    ///
    /// ลูกศรที่กดแล้วเจอหน้าว่างคือคำสัญญาที่ผิด — กติกาเดียวกับที่ `PanelText`
    /// ซ่อนลูกศรสลับ org ตอนบัญชีมี org เดียว
    func hasDetail(_ kind: PageKind) -> Bool { kind != .mascot }

    // --- การเปลี่ยนค่า: แก้ก้อนที่ถืออยู่ แล้วส่งออกทั้งก้อน ---------------------------

    func chooseBoard(_ id: UUID?) {
        selectedBoard = id
        onSelectBoard?(id)
    }

    func chooseBrightness(_ value: Int) {
        brightness = value
        onBrightness?(value)
    }

    func chooseInterval(_ value: PollInterval) {
        interval = value
        onInterval?(value)
    }

    func chooseBoardHost(_ host: String) {
        let trimmed = host.trimmingCharacters(in: .whitespaces)
        boardHost = trimmed
        onBoardHost?(trimmed)
    }

    // --- Screen --------------------------------------------------------------

    func setPageOn(_ kind: PageKind, _ on: Bool) {
        pages.setOn(kind, on)
        emitPages()
    }

    /// ลำดับใหม่ทั้งชุด — เรียกครั้งเดียวตอนผู้ใช้ปล่อยแถวที่ลาก ไม่ใช่ทุกเฟรมของการลาก
    ///
    /// ทางออกของค่านี้ไม่ได้จบที่ตัวแปร: มันไป `UserDefaults` ไปเป็นเฟรมบนสายถึงบอร์ด
    /// แล้วป้อนสถานะกลับเข้าหน้าต่างอีกรอบ การเรียกมันระหว่างลากจึงสร้างลิสต์ใหม่สองรอบ
    /// ต่อหนึ่งเฟรม — หน้าต่างกระพริบ และบอร์ดได้ลำดับกลางทางที่ผู้ใช้ไม่เคยตั้งใจส่ง
    func setPageOrder(_ order: [PageKind]) {
        guard order != pages.order else { return }
        pages = PageSettings(
            order: order, off: pages.off, autoTurn: pages.autoTurn, rotation: pages.rotation,
            hold: pages.hold, attentionJump: pages.attentionJump)
        onPages?(pages)
    }

    func setAutoTurn(_ on: Bool) {
        pages.autoTurn = on
        emitPages()
    }

    func setRotation(_ seconds: Int) {
        pages.rotation = seconds
        emitPages()
    }

    func setHoldMinutes(_ minutes: Int) {
        pages.hold = minutes * Self.holdStep
        emitPages()
    }

    func setAttentionJump(_ on: Bool) {
        pages.attentionJump = on
        emitPages()
    }

    /// นาทีเข้า วินาทีออก — ค่าบนสายเป็นวินาทีเสมอ ส่วนช่องนี้ถามเป็นนาทีเพราะ
    /// ค่าตั้งต้นคือ 5 นาที และไม่มีใครอยากอ่านหรือพิมพ์เลข 300
    static let holdStep = 60

    var holdMinutes: Int { pages.hold / Self.holdStep }

    /// ค่าที่ส่งออกถูกบีบเข้าช่วงที่ยอมรับได้ระหว่างทาง — แสดงกลับเสมอ ไม่ใช่เฉพาะตอนเปิด
    /// หน้าต่าง ไม่งั้นช่องจะค้างเลข 9999 ที่ไม่มีใครใช้ทั้งที่จอหมุนตาม 600
    private func emitPages() {
        let settled = PageSettings(
            order: pages.order, off: pages.off, autoTurn: pages.autoTurn,
            rotation: pages.rotation, hold: pages.hold, attentionJump: pages.attentionJump)
        pages = settled
        onPages?(settled)
    }

    // --- Weather -------------------------------------------------------------

    func chooseWeather(place: String? = nil, unit: TempUnit? = nil) {
        let settings = WeatherSettings(
            place: (place ?? weather.place).trimmingCharacters(in: .whitespaces),
            unit: unit ?? weather.unit)
        weather = settings
        onWeather?(settings)
    }

    // --- Crypto --------------------------------------------------------------

    var cryptoHasRoom: Bool { crypto.coins.count < CryptoSettings.maxCoins }

    @discardableResult
    func addCoin(_ raw: String) -> Bool {
        var next = crypto
        guard next.add(raw) else { return false }
        crypto = next
        onCrypto?(next)
        return true
    }

    func removeCoin(_ index: Int) {
        var next = crypto
        next.remove(index)
        crypto = next
        onCrypto?(next)
    }

    /// ลำดับใหม่ทั้งชุด — ครั้งเดียวตอนปล่อย ด้วยเหตุผลเดียวกับ `setPageOrder`
    func setCoinOrder(_ coins: [String]) {
        guard coins != crypto.coins else { return }
        let next = CryptoSettings(coins: coins)
        crypto = next
        onCrypto?(next)
    }

    // --- Stocks --------------------------------------------------------------

    var stocksHaveRoom: Bool { stocks.symbols.count < StockSettings.maxSymbols }

    @discardableResult
    func addSymbol(_ raw: String) -> Bool {
        var next = stocks
        guard next.add(raw) else { return false }
        stocks = next
        onStocks?(next)
        return true
    }

    func removeSymbol(_ index: Int) {
        var next = stocks
        next.remove(index)
        stocks = next
        onStocks?(next)
    }

    func setSymbolOrder(_ symbols: [String]) {
        guard symbols != stocks.symbols else { return }
        let next = StockSettings(symbols: symbols)
        stocks = next
        onStocks?(next)
    }

    // --- Calendar ------------------------------------------------------------

    func setCalendarOn(_ id: String, _ on: Bool) {
        var next = calendars
        next.setOn(id, on)
        calendars = next
        onCalendar?(next)
    }

    // --- Wi-Fi ---------------------------------------------------------------

    func rescan() {
        networks.beginScan()
        onScan?()
    }

    /// ช่องรหัสที่ว่างไม่ใช่ "รหัสคือค่าว่าง" เมื่อวงนั้นบอร์ดจำไว้อยู่แล้ว — มันคือ "ใช้ของ
    /// เดิม" ซึ่งเป็นสิ่งที่ผู้ใช้ตั้งใจทุกครั้งที่กลับไปต่อวงที่เคยต่อได้
    func join() {
        guard let ssid = selectedSSID else { return }
        let saved = networks.saved.contains(ssid)
        onJoin?(ssid, password.isEmpty && saved ? nil : password)
        password = ""
    }

    func forget() {
        guard let ssid = selectedSSID else { return }
        onForget?(ssid)
    }

    /// สองบรรทัดบนสุดของหน้า Wi-Fi — สภาพจริงก่อน แล้วค่อยรายละเอียด
    ///
    /// "บอร์ดไม่ได้ต่อ BLE" ต้องมาก่อนทุกอย่าง: หน้านี้ทั้งหน้าสั่งอะไรไม่ได้เลยถ้าไม่มีลิงก์
    /// และปุ่มที่กดได้ทั้งที่คำสั่งไปไม่ถึงคือหน้าต่างที่โกหก
    var wifiHeadline: String {
        guard linked else { return "No board connected over Bluetooth" }
        guard let wifi else { return "Asking the board…" }
        switch wifi.state {
        case .connected: return "Connected to \(wifi.ssid)"
        case .connecting: return "Connecting to \(wifi.ssid)…"
        case .failed: return "\(wifi.ssid): \(wifi.error ?? "failed")"
        case .off: return "No network saved yet"
        }
    }

    var wifiDetail: String {
        guard linked else { return "Wi-Fi can only be set up while the board is in range." }
        guard let wifi else { return "" }
        switch wifi.state {
        case .connected: return "Board address \(wifi.ip)"
        // "ไม่เจอ" บอร์ดรอเองได้ ส่วน "รหัสผิด" ลองอีกกี่รอบก็รหัสเดิม — ประโยคเดียวกัน
        // สองกรณีเคยทำให้ผู้ใช้ที่พิมพ์ผิดนั่งรอการลองใหม่ที่ไม่มีวันสำเร็จ
        case .failed:
            return wifi.needsPassword
                ? "Type the password again below, then press Connect."
                : "The board keeps retrying on its own."
        case .connecting, .off: return ""
        }
    }

    var wifiFailed: Bool { linked && wifi?.state == .failed }
}
