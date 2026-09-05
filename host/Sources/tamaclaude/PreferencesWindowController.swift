import AppKit
import SwiftUI
import TamaCore

/// หน้าตั้งค่าเต็มรูป — ที่เดียวของทุกสวิตช์ที่เคยอยู่หลังปุ่มเฟือง
///
/// เมนูเฟืองรับหน้า WiFi ไม่ไหว: รายชื่อเครือข่ายยาวไม่แน่นอน มีช่องรหัสผ่านที่ต้องพิมพ์
/// และมีสถานะที่เปลี่ยนเองระหว่างที่ผู้ใช้มองอยู่ ทั้งสามอย่างเป็นสิ่งที่ `NSMenu` ทำไม่ได้
/// (เมนูปิดตัวเองทันทีที่คลิก และไม่มีที่ให้ข้อความสถานะยาวๆ)
///
/// controller ตัวนี้ไม่รู้จัก BLE, ไฟล์ หรือ UserDefaults เลย — ทุกอย่างเข้าออกผ่าน closure
/// ที่ `MenuBarApp` ผูกให้ เพื่อให้ที่เก็บสถานะจริงยังมีที่เดียวเหมือนเดิม
///
/// เนื้อในเป็น SwiftUI ตั้งแต่รอบ redesign แต่ **หน้าตาสาธารณะของใบนี้ไม่เปลี่ยนสักตัว**:
/// closure ชุดเดิม เมธอด `show*` ชุดเดิม `MenuBarApp` จึงไม่ต้องรู้เลยว่าข้างในเปลี่ยนไป
/// เปลือกยังเป็น `NSWindowController` เพราะแอปนี้เป็น `.accessory` ที่ปกติไม่มีหน้าต่าง —
/// `Settings` scene ของ SwiftUI ต้องการ `App` lifecycle ซึ่งใบนี้ไม่ได้ใช้
final class PreferencesWindowController: NSWindowController {
    /// สถานะทั้งหมดอยู่ที่นี่ใบเดียว — `show*` เขียนลงมัน แล้ว SwiftUI วาดตาม
    private let model = SettingsModel()

    // --- ทางออกไปหา MenuBarApp ---------------------------------------------
    var onSelectBoard: ((UUID?) -> Void)? {
        get { model.onSelectBoard } set { model.onSelectBoard = newValue }
    }
    var onBrightness: ((Int) -> Void)? {
        get { model.onBrightness } set { model.onBrightness = newValue }
    }
    var onInterval: ((PollInterval) -> Void)? {
        get { model.onInterval } set { model.onInterval = newValue }
    }
    var onSetSessionKey: (() -> Void)? {
        get { model.onSetSessionKey } set { model.onSetSessionKey = newValue }
    }
    var onInstallHooks: (() -> Void)? {
        get { model.onInstallHooks } set { model.onInstallHooks = newValue }
    }
    var onToggleStatusline: (() -> Void)? {
        get { model.onToggleStatusline } set { model.onToggleStatusline = newValue }
    }
    var onToggleLogin: (() -> Void)? {
        get { model.onToggleLogin } set { model.onToggleLogin = newValue }
    }
    var onToggleAutoStart: (() -> Void)? {
        get { model.onToggleAutoStart } set { model.onToggleAutoStart = newValue }
    }
    var onOpenLog: (() -> Void)? {
        get { model.onOpenLog } set { model.onOpenLog = newValue }
    }
    var onOpenProject: (() -> Void)? {
        get { model.onOpenProject } set { model.onOpenProject = newValue }
    }
    var onScan: (() -> Void)? {
        get { model.onScan } set { model.onScan = newValue }
    }
    var onJoin: ((String, String?) -> Void)? {
        get { model.onJoin } set { model.onJoin = newValue }
    }
    var onForget: ((String) -> Void)? {
        get { model.onForget } set { model.onForget = newValue }
    }
    /// ที่อยู่บอร์ดที่ผู้ใช้กรอกเอง — สตริงว่างคือกลับไปให้แอปหาเอง
    var onBoardHost: ((String) -> Void)? {
        get { model.onBoardHost } set { model.onBoardHost = newValue }
    }
    /// ค่าตั้งของหน้าอากาศเปลี่ยน — หน้าต่างไม่รู้จัก UserDefaults เหมือนทุกอย่างที่นี่
    var onWeather: ((WeatherSettings) -> Void)? {
        get { model.onWeather } set { model.onWeather = newValue }
    }
    /// พฤติกรรมของจอเปลี่ยน (หน้าไหนเปิด ลำดับ รอบหมุน) — ทางเดียวกับทุกอย่างที่นี่
    var onPages: ((PageSettings) -> Void)? {
        get { model.onPages } set { model.onPages = newValue }
    }
    /// watchlist ของหน้าคริปโตเปลี่ยน — ทั้งก้อนเสมอ ไม่ใช่ "เพิ่มตัวนี้" ทีละคำสั่ง
    var onCrypto: ((CryptoSettings) -> Void)? {
        get { model.onCrypto } set { model.onCrypto = newValue }
    }
    /// watchlist ของหน้าหุ้นเปลี่ยน — ทั้งก้อนเหมือนคริปโต
    var onStocks: ((StockSettings) -> Void)? {
        get { model.onStocks } set { model.onStocks = newValue }
    }
    /// ผู้ใช้กดตั้ง key ของ Finnhub — ช่องกรอกแบบปิดบังเด้งจากฝั่ง `MenuBarApp`
    /// ด้วยเหตุผลเดียวกับ `sessionKey`: หน้าต่างนี้ไม่เคยแตะไฟล์เอง
    var onFinnhubKey: (() -> Void)? {
        get { model.onFinnhubKey } set { model.onFinnhubKey = newValue }
    }
    /// ปฏิทินที่ผู้ใช้ติ๊กให้ขึ้นจอเปลี่ยน — ทั้งก้อนเหมือน watchlist
    var onCalendar: ((CalendarSettings) -> Void)? {
        get { model.onCalendar } set { model.onCalendar = newValue }
    }
    /// ผู้ใช้กดขอสิทธิ์ปฏิทิน — กล่องของระบบเด้งจากฝั่ง `MenuBarApp` ไม่ใช่จากที่นี่
    var onCalendarAccess: (() -> Void)? {
        get { model.onCalendarAccess } set { model.onCalendarAccess = newValue }
    }

    init() {
        // ย่อขยายได้ และจำขนาดไว้ — ของเดิมล็อก 460x520 ใบเดียวให้ทุกแท็บ ซึ่งแปลว่า
        // แท็บ Wi-Fi (ตาราง + รหัสผ่าน + ปุ่มสี่ปุ่ม) อัดแน่นในกรอบเดียวกับแท็บที่มี
        // สองบรรทัด · ไม่มี `.miniaturizable`: หน้าต่างตั้งค่าที่ย่อลง Dock ได้คือหน้าต่าง
        // ที่ผู้ใช้หาไม่เจอตอนกด Settings… ซ้ำแล้วไม่มีอะไรเกิดขึ้น
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 720, height: 520),
            styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        window.title = "TamaClaude Settings"
        window.setFrameAutosaveName("TamaClaudeSettings")
        super.init(window: window)

        let host = NSHostingController(rootView: SettingsView(model: model))
        window.contentViewController = host
        window.contentMinSize = NSSize(width: 660, height: 460)
        window.setContentSize(NSSize(width: 720, height: 520))
        window.center()
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    /// หน้าต่างเปิดอยู่ไหม — ตัวเรียกใช้ตัดสินว่าควรป้อนสถานะที่ขยับเองเข้ามาซ้ำหรือเปล่า
    var isShowing: Bool { window?.isVisible == true }

    func show() {
        // ปกติแอปนี้ไม่มีหน้าต่างเลย (`.accessory`) การเปิดหน้าตั้งค่าจึงต้องดึงแอปขึ้นมา
        // หน้าสุดเอง ไม่งั้นหน้าต่างโผล่หลังหน้าต่างของแอปอื่นแล้วดูเหมือนกดปุ่มไม่ติด
        NSApp.activate(ignoringOtherApps: true)
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
        onScan?()
    }

    // --- ให้ MenuBarApp ป้อนสถานะเข้ามา ------------------------------------
    func showBoards(_ list: [Board], selected: UUID?) {
        model.boards = list
        model.selectedBoard = selected
    }

    func showBrightness(_ value: Int) { model.brightness = value }

    func showInterval(_ interval: PollInterval) { model.interval = interval }

    func showToggles(statusline: Bool, autoStart: Bool, login: Bool) {
        model.statusline = statusline
        model.autoStart = autoStart
        model.login = login
    }

    func showBoardHost(_ host: String) { model.boardHost = host }

    func showPages(_ settings: PageSettings) { model.pages = settings }

    func showWeather(_ settings: WeatherSettings, status: String?, supported: Bool, on: Bool) {
        model.weather = settings
        model.weatherSupported = supported
        model.weatherStatus = status ?? ""
    }

    func showCrypto(_ settings: CryptoSettings, status: String?, supported: Bool, on: Bool) {
        model.crypto = settings
        model.cryptoSupported = supported
        model.cryptoStatus = status ?? ""
    }

    func showStocks(
        _ settings: StockSettings, status: String?, hasKey: Bool, keyRejected: Bool,
        supported: Bool, on: Bool
    ) {
        model.stocks = settings
        model.stocksSupported = supported
        model.stocksStatus = status ?? ""
        model.hasStockKey = hasKey
        model.stockKeyRejected = keyRejected
    }

    func showCalendar(
        _ settings: CalendarSettings, available: [CalendarInfo], access: CalendarAccess,
        status: String?, supported: Bool, on: Bool
    ) {
        model.calendars = settings
        model.availableCalendars = available
        model.calendarAccess = access
        model.calendarSupported = supported
        model.calendarStatus = status ?? ""
    }

    func showKey(_ state: SessionKeyState) { model.keyState = state }

    func showHooks(_ line: String) { model.hooks = line }

    /// ทางที่ snapshot เดินอยู่จริง — คนละเรื่องกับ "บอร์ดต่อ WiFi แล้ว"
    ///
    /// บอร์ดที่ขึ้นเน็ตสำเร็จแต่ Mac หาไม่เจอ (client isolation, คนละ subnet) จะดูดีทุกอย่าง
    /// บนหน้านี้ทั้งที่ทางสำรองใช้ไม่ได้เลย บรรทัดนี้เป็นที่เดียวที่แยกสองอย่างนั้นออก
    func showRoute(_ route: LanRoute, detail: String?) {
        model.route = route
        model.routeDetail = detail ?? ""
    }

    func showLink(_ connected: Bool) {
        model.linked = connected
        // สปินเนอร์ที่หมุนค้างหลังลิงก์หลุดคือคำโกหก
        if !connected { model.networks.linkLost() }
    }

    func apply(_ event: BoardEvent) {
        model.networks.apply(event)
        if case .wifi(let status) = event { model.wifi = status }
    }

    func beginScan() { model.networks.beginScan() }
}
