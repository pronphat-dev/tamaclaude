import AppKit
import SwiftUI
import TamaCore

// --- Board ---------------------------------------------------------------------

/// อุปกรณ์: ตัวไหน สว่างแค่ไหน และตอนนี้คุยกันอยู่ทางไหน
struct BoardPane: View {
    @ObservedObject var model: SettingsModel

    var body: some View {
        PaneShell(title: "Board") {
            Card {
                CardRow {
                    Circle()
                        .fill(model.route == .none ? Color(nsColor: .tertiaryLabelColor) : .tama)
                        .frame(width: 8, height: 8)
                    // บรรทัดเดียว ไม่ใช่สอง — `MenuBarApp` ส่ง `detail` มาแทนประโยคปกติ
                    // ไม่ใช่มาต่อท้ายมัน สองบรรทัดที่พูดเรื่องเดียวกันคือหน้าต่างที่ย้ำตัวเอง
                    Text(model.routeDetail.isEmpty
                        ? PanelText.board(route: model.route) : model.routeDetail)
                    Spacer(minLength: 0)
                }
                CardDivider()
                // ช่องกรอกที่อยู่เอง: mDNS เป็นสิ่งแรกที่หายไปเมื่อมี VLAN ของแขก,
                // subnet ที่สอง หรือเราเตอร์ที่กรอง multicast — และตอนนั้น BLE ก็ตายไปแล้ว
                // ไม่มีทางอื่นเหลือ · อยู่ที่นี่ไม่ใช่ในหน้า Wi-Fi เพราะมันตอบว่า "Mac หา
                // บอร์ดเจอไหม" ไม่ใช่ "บอร์ดขึ้นเน็ตไหม" ซึ่งเป็นคนละคำถามและพังคนละแบบ
                LabeledRow(title: "Address", subtitle: "Empty means find it automatically") {
                    TextField("Automatic", text: $model.boardHost)
                        .frame(width: 170)
                        .onSubmit { model.chooseBoardHost(model.boardHost) }
                        .onDisappear { model.chooseBoardHost(model.boardHost) }
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                Card {
                    LabeledRow(title: "Use") {
                        Picker(
                            "",
                            selection: Binding(
                                get: { model.selectedBoard },
                                set: { model.chooseBoard($0) })
                        ) {
                            Text("Any board").tag(UUID?.none)
                            ForEach(model.boards, id: \.id) { board in
                                Text(board.name + (board.isCurrent ? " ✓" : ""))
                                    .tag(UUID?.some(board.id))
                            }
                        }
                        .labelsHidden()
                        .fixedSize()
                    }
                    CardDivider()
                    LabeledRow(title: "Brightness") {
                        Slider(
                            value: Binding(
                                get: { Double(model.brightness) },
                                set: { model.brightness = Int($0.rounded()) }),
                            in: 5...100,
                            // ส่งตอนปล่อยเมาส์ ไม่ใช่ทุกพิกเซลที่ลาก — เขียนบอร์ดทุกเฟรม
                            // ของการลากคือการยิงคำสั่งเป็นร้อยครั้งเพื่อค่าสุดท้ายค่าเดียว
                            onEditingChanged: { editing in
                                if !editing { model.chooseBrightness(model.brightness) }
                            }
                        )
                        .frame(width: 160)
                        Text("\(model.brightness)%")
                            .font(.system(size: 11).monospacedDigit())
                            .foregroundStyle(.secondary)
                            .frame(width: 34, alignment: .trailing)
                    }
                }
                Footnote(
                    text: "The screen stays at one brightness all day — it never dims on its "
                        + "own, so it reads the same at any hour.")
            }
        }
    }
}

// --- Screen --------------------------------------------------------------------

/// ลิสต์หน้า + จังหวะการหมุน — หน้าแรกของหน้าต่างนี้
struct ScreenPane: View {
    @ObservedObject var model: SettingsModel
    let open: (PageKind) -> Void
    @State private var lift: Lift?
    /// ลำดับระหว่างที่ยังลากอยู่ — `nil` คือไม่ได้ลาก ให้อ่านจาก model ตามปกติ
    ///
    /// มีอยู่เพราะการเขียนลำดับกลับเข้า model ทุกเฟรมของการลากทำให้ลิสต์ถูกสร้างใหม่
    /// สองรอบต่อเฟรม (model -> UserDefaults -> บอร์ด -> ป้อนกลับ) ซึ่งเห็นเป็นการกระพริบ
    @State private var draft: [PageKind]?
    /// ระยะจริงต่อแถว วัดจากการ์ดตอนวาด — ค่าตั้งต้นเป็นแค่ค่าก่อนวัดครั้งแรก
    @State private var pitch: CGFloat = Metrics.row

    private static let space = "screen.pages"

    private static let help =
        "The board keeps its own clock and caches every page, so the screen keeps turning "
        + "while this Mac sleeps. Drag a page to change where it falls in the round. A page "
        + "you switch off is not merely left out of the round — the board is told to forget "
        + "it, so it cannot come back around showing yesterday's figures."

    var body: some View {
        PaneShell(title: "Screen") {
            VStack(alignment: .leading, spacing: 6) {
                HeadingRow(title: "Pages, in the order they come round", help: Self.help)
                Card {
                    ForEach(Array(order.enumerated()), id: \.element) { index, kind in
                        if index > 0 { CardDivider() }
                        pageRow(index: index, kind: kind)
                            .reorderMenu(index: index, count: order.count, move: { from, to in
                                shift(from: from, to: to)
                                commit()
                            })
                            .offset(y: liftOffset(lift, at: index))
                            .zIndex(lift?.index == index ? 1 : 0)
                    }
                }
                // แถวที่ *ไม่ได้* ถูกลากต้องเลื่อนหลบ ไม่ใช่กระโดดสลับที่ — จังหวะเดียว
                // ของหน้าต่างนี้ที่มีการเคลื่อนไหว และเป็นจังหวะที่บอกว่าการลากได้ผลแล้ว
                .animation(.snappy(duration: 0.16), value: order)
                .measureRowPitch(count: order.count, into: $pitch)
                .coordinateSpace(name: Self.space)
            }

            VStack(alignment: .leading, spacing: 6) {
                Card {
                    // "Change" ไม่ใช่ "Turn" ทั้งสองแถว — บนแถวที่มีสวิตช์ "Turn" อ่านได้ว่า
                    // turn *on* ก่อนจะอ่านว่าพลิกหน้า และแถวล่างก็ใช้คำเดียวกันอยู่ คำที่
                    // แบกสองความหมายในการ์ดใบเดียวคือคำที่ต้องอ่านสองรอบทุกครั้ง
                    LabeledRow(
                        title: "Change pages automatically",
                        subtitle: "Off, and only a swipe changes the page"
                    ) {
                        Toggle(
                            "",
                            isOn: Binding(
                                get: { model.pages.autoTurn },
                                set: { model.setAutoTurn($0) })
                        )
                        .labelsHidden()
                        .toggleStyle(.switch)
                    }
                    CardDivider()
                    // ช่องนี้ *ไม่* ดับตามสวิตช์ข้างบน เพราะมันยังทำงานอยู่ตอนรอบหมุนปิด:
                    // เป็นระยะที่มาสคอตอยู่บนจอหลังการเด้ง ก่อนจอกลับไปหน้าที่ผู้ใช้ปัดค้างไว้
                    // แถวที่ดับคือคำสัญญาว่าค่านั้นไม่มีผล — ดับช่องนี้จึงเป็นการโกหก
                    LabeledRow(
                        title: "Change every",
                        subtitle: model.pages.autoTurn
                            ? nil : "How long the mascot stays after it jumps"
                    ) {
                        measure(
                            value: Binding(
                                get: { model.pages.rotation },
                                set: { model.setRotation($0) }),
                            range: PageSettings.rotationRange, unit: "seconds")
                    }
                    CardDivider()
                    // ส่วนแถวนี้ดับจริง — ไม่มีรอบหมุนก็ไม่มีอะไรให้การยึดกันไว้
                    LabeledRow(
                        title: "Hold a swiped page",
                        subtitle: "Before it rejoins the round"
                    ) {
                        measure(
                            value: Binding(
                                get: { model.holdMinutes },
                                set: { model.setHoldMinutes($0) }),
                            range: 0...(PageSettings.holdRange.upperBound / SettingsModel.holdStep),
                            unit: "minutes")
                    }
                    .disabled(!model.pages.autoTurn)
                    CardDivider()
                    LabeledRow(
                        title: "Jump to the mascot",
                        subtitle: "When a session needs a hand"
                    ) {
                        Toggle(
                            "",
                            isOn: Binding(
                                get: { model.pages.attentionJump },
                                set: { model.setAttentionJump($0) })
                        )
                        .labelsHidden()
                        .toggleStyle(.switch)
                    }
                }
            }
        }
    }

    /// แถวหนึ่งของลิสต์หน้า — ที่จับ · ไอคอน · ชื่อ · สถานะย่อ · สวิตช์ · ลูกศรดริล
    ///
    /// หน้ามาสคอตมีสวิตช์ที่กดไม่ได้ ไม่ใช่ไม่มีสวิตช์ — แถวที่ไม่มีสวิตช์อ่านว่า
    /// "ยังไม่รองรับ" ส่วนสวิตช์ที่ติดค้างและกดไม่ลงบอกตรงๆ ว่ามันปิดไม่ได้
    @ViewBuilder
    private func pageRow(index: Int, kind: PageKind) -> some View {
        let on = model.pages.isOn(kind)
        CardRow {
            ReorderHandle(
                index: index, count: order.count, pitch: pitch, space: Self.space,
                lift: $lift, move: shift, commit: commit)

            Image(systemName: icon(kind))
                .font(.system(size: 13))
                .foregroundStyle(on ? Color.tama : Color(nsColor: .tertiaryLabelColor))
                .frame(width: 20)

            Text(kind.title)
                .foregroundStyle(on ? Color.primary : Color.secondary)
                .frame(width: 86, alignment: .leading)

            Text(model.summary(for: kind))
                .font(.system(size: 11))
                .foregroundStyle(
                    model.isProblem(kind) ? Color(nsColor: .systemRed) : Color.secondary
                )
                .lineLimit(1)
                .truncationMode(.tail)

            Spacer(minLength: 8)

            Toggle(
                "",
                isOn: Binding(
                    get: { on }, set: { model.setPageOn(kind, $0) })
            )
            .labelsHidden()
            .toggleStyle(.switch)
            .controlSize(.small)
            .disabled(kind == .mascot)
            // สวิตช์ที่ดับอยู่ข้างที่จับที่ลากได้ไม่ได้บอกเองว่าอันไหนคุมอะไร — กฎคือ
            // "ปิดไม่ได้" ไม่ใช่ "ย้ายไม่ได้" และลำดับของหน้ามาสคอตมีความหมายจริง
            // (อยู่ท้ายสุด = จอพักที่หน้าอื่นก่อนวนกลับมา)
            .help(
                kind == .mascot
                    ? "The mascot page cannot be switched off, but it can be reordered"
                    : "Show this page in the round")

            if model.hasDetail(kind) {
                Button { open(kind) } label: {
                    Image(systemName: "chevron.right")
                        .font(.system(size: 11, weight: .semibold))
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .handCursor()
                .frame(width: 14)
            } else {
                // ลูกศรที่กดแล้วเจอหน้าว่างคือคำสัญญาที่ผิด — เว้นที่ไว้เท่ากันแทน
                // ไม่งั้นแถวมาสคอตจะกว้างกว่าเพื่อนอยู่แถวเดียว
                Color.clear.frame(width: 14, height: 1)
            }
        }
        .background(Color(nsColor: .controlBackgroundColor))
        .contentShape(Rectangle())
    }

    /// ลำดับที่กำลังแสดงอยู่ — ของชั่วคราวชนะระหว่างลาก
    private var order: [PageKind] { draft ?? model.pages.order }

    /// ย้ายในลำดับชั่วคราวเท่านั้น ไม่แตะ model
    private func shift(from: Int, to: Int) {
        var next = order
        guard next.indices.contains(from), next.indices.contains(to), from != to else { return }
        next.insert(next.remove(at: from), at: to)
        draft = next
    }

    /// ปล่อยแล้วส่งออกครั้งเดียว แล้วคืนลิสต์ให้ model เป็นเจ้าของเหมือนเดิม
    private func commit() {
        guard let draft else { return }
        model.setPageOrder(draft)
        self.draft = nil
    }

    private func icon(_ kind: PageKind) -> String {
        switch kind {
        case .mascot: return "face.smiling"
        case .weather: return "cloud.sun"
        case .crypto: return "bitcoinsign.circle"
        case .stocks: return "chart.line.uptrend.xyaxis"
        case .calendar: return "calendar"
        }
    }

    /// ช่องตัวเลขกับหน่วยของมัน — หน่วยอยู่ *หลัง* ช่อง ไม่ใช่ในป้ายซ้าย ผู้ใช้ที่พิมพ์ทับ
    /// ต้องเห็นว่ากำลังพิมพ์วินาทีหรือนาที ตอนที่สายตาอยู่ที่ช่อง ไม่ใช่ตอนอ่านหัวแถว
    ///
    /// มี stepper ด้วย เพราะช่องเปล่ากับ `NumberFormatter` ยอมให้พิมพ์ 9999 ลงไปเงียบๆ
    /// แล้วค่อยถูกบีบกลับตอนส่งออก — ผู้ใช้เห็นเลขที่ตัวเองพิมพ์เด้งกลับโดยไม่รู้ว่าทำไม
    @ViewBuilder
    private func measure(value: Binding<Int>, range: ClosedRange<Int>, unit: String) -> some View {
        TextField("", value: value, format: .number)
            .multilineTextAlignment(.trailing)
            .frame(width: 52)
        Text(unit).font(.system(size: 11)).foregroundStyle(.secondary)
            .frame(width: 52, alignment: .leading)
        Stepper("", value: value, in: range).labelsHidden()
    }
}

// --- หน้าย่อยของแต่ละหน้าบนจอ ------------------------------------------------------

/// สิ่งที่อยู่หลังลูกศร `›` ของแถวหนึ่ง — ค่าตั้งของหน้านั้นหน้าเดียว
///
/// หน้าที่ถูกปิดอยู่ ตัวควบคุมทุกตัวข้างในดับพร้อมกัน ไม่ใช่ครึ่งเดียว — หน้าต่างที่ยอมให้
/// แก้ watchlist ของหน้าที่ปิดอยู่ได้ครึ่งหนึ่งคือหน้าต่างที่บอกคนละเรื่องกับตัวเอง
struct PageDetailPane: View {
    @ObservedObject var model: SettingsModel
    let kind: PageKind
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        PaneShell(title: kind.title, back: { dismiss() }) {
            if !model.supported(kind) {
                Card {
                    CardRow {
                        Image(systemName: "exclamationmark.triangle")
                            .foregroundStyle(Color(nsColor: .systemRed))
                        Text("The board's firmware does not know this page yet — flash it to use it.")
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            // ตัวควบคุมที่ดับทั้งหน้าโดยไม่บอกเหตุผลอ่านว่า "พัง" ไม่ใช่ "ปิดอยู่"
            if !model.pages.isOn(kind) {
                Footnote(
                    text: "This page is switched off, so nothing here reaches the screen. "
                        + "Turn it on in the list under Screen.")
            }
            // ดับเฉพาะตัวควบคุม ไม่ใช่ทั้งหน้า — ทางกลับต้องกดได้เสมอ ไม่งั้นการปิดหน้า
            // แล้วดริลเข้ามาคือการเข้าห้องที่ไม่มีประตูออก
            Group {
                switch kind {
                case .weather: WeatherDetail(model: model)
                case .crypto: CryptoDetail(model: model)
                case .stocks: StocksDetail(model: model)
                case .calendar: CalendarDetail(model: model)
                case .mascot: EmptyView()
                }
            }
            .disabled(!model.pages.isOn(kind))
        }
        // ไม่ตั้ง `navigationTitle`: มันเขียนทับชื่อหน้าต่างแล้วค้างอยู่อย่างนั้นหลังกดกลับ
        // หน้าต่างที่ชื่อ "Crypto" ทั้งที่แสดงหน้า Board คือหน้าต่างที่หาไม่เจอใน Mission
        // Control · ชื่อหน้าอยู่ในเนื้อหาอยู่แล้ว ที่เดียวพอ
        .navigationBarBackButtonHidden()
    }
}

/// เมืองเป็นชื่อที่พิมพ์เอง ไม่ใช่ CoreLocation: เลี่ยง TCC อีกใบ และของตั้งโต๊ะไม่ได้ย้ายที่
struct WeatherDetail: View {
    @ObservedObject var model: SettingsModel
    @State private var typed = ""

    private static let help =
        "Weather figures come from Open-Meteo, fetched here every 15 minutes. The board "
        + "never goes online itself, and the page always says how old the figures are."

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HeadingRow(title: "Where", help: Self.help)
            Card {
                LabeledRow(title: "City") {
                    // ผูกค่าไว้ที่ตัวแปรของหน้า ไม่ใช่ยิงทุกตัวอักษร — ชื่อเมืองที่พิมพ์ไป
                    // ครึ่งคำเป็นคำขอที่ล้มแน่นอน · ส่งตอนกด Enter หรือตอนออกจากหน้า
                    // เพราะคนที่พิมพ์แล้วกดลูกศรกลับต้องได้เมืองนั้น ไม่ใช่ได้ช่องว่างเปล่า
                    TextField("e.g. Bangkok", text: $typed)
                        .frame(width: 170)
                        .onSubmit { model.chooseWeather(place: typed) }
                        .onDisappear { model.chooseWeather(place: typed) }
                }
                CardDivider()
                LabeledRow(title: "Units") {
                    Picker(
                        "",
                        selection: Binding(
                            get: { model.weather.unit },
                            set: { model.chooseWeather(unit: $0) })
                    ) {
                        ForEach(TempUnit.allCases, id: \.self) { Text($0.title).tag($0) }
                    }
                    .labelsHidden()
                    .fixedSize()
                }
            }
            StatusLine(text: statusText)
        }
        .onAppear { typed = model.weather.place }
        .onChange(of: model.weather.place) { typed = model.weather.place }
    }

    /// เหตุผลที่หน้านี้ยังไม่ขึ้นจอที่ผู้ใช้แก้ได้จากตรงนี้ — firmware เก่าถูกตอบไปแล้วที่หัวหน้า
    private var statusText: String {
        if !model.weatherStatus.isEmpty { return model.weatherStatus }
        if model.pages.isOn(.weather) && !model.weather.isUsable { return "Type a city to start." }
        return ""
    }
}

/// ผู้ใช้พิมพ์ชื่อที่เขาเรียกมันเอง ("btc", "bitcoin") ไม่ใช่ id ของบริการ การแปลงเป็น id
/// เกิดครั้งเดียวใน `CryptoService` แล้วถูกจำไว้ — คนซื้อคริปโตไม่ได้จำ slug ของ CoinGecko
struct CryptoDetail: View {
    @ObservedObject var model: SettingsModel
    @State private var typed = ""
    @State private var lift: Lift?

    private static let help =
        "Prices come from CoinGecko, fetched here every 60 seconds around the clock — the "
        + "crypto market never closes. Five coins at most: that is what keeps one request per "
        + "round and one frame per page. Gains and losses are told apart by the arrow as well "
        + "as the colour."

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HeadingRow(title: "Watchlist", help: Self.help)
            WatchlistCard(
                space: "crypto.watchlist",
                symbols: model.crypto.coins,
                empty: "No coins yet",
                lift: $lift,
                reorder: { model.setCoinOrder($0) },
                remove: { model.removeCoin($0) },
                entry: {
                    WatchlistEntry(
                        placeholder: "Coin (e.g. btc)",
                        typed: $typed,
                        catalog: LogoCatalog.crypto,
                        taken: model.crypto.coins,
                        hasRoom: model.cryptoHasRoom,
                        add: { model.addCoin($0) })
                })
            StatusLine(text: statusText)
            Footnote(
                text: "A coin with no logo here gets the same blank plate on the board — what "
                    + "you see while adding is what the screen draws.")
        }
    }

    /// เหตุผลที่หน้านี้ยังไม่ขึ้นจอ และผู้ใช้แก้ได้คนละแบบ: ยังไม่ใส่เหรียญ (ใส่) ·
    /// เต็มเพดานแล้ว (ลบก่อน) · ดึงไม่สำเร็จ (รอ) — firmware เก่าถูกตอบไปแล้วที่หัวหน้า
    private var statusText: String {
        if !model.cryptoStatus.isEmpty { return model.cryptoStatus }
        if model.pages.isOn(.crypto) && !model.crypto.isUsable { return "Add a coin to start." }
        if !model.cryptoHasRoom { return "Five coins is the most this page shows." }
        return ""
    }
}

/// รูปเดียวกับหน้าคริปโตทุกส่วน บวก key — หน้าหุ้นเป็นหน้าเดียวที่ต้องมี credential
/// ของผู้ใช้ก่อนถึงจะมีตัวเลขให้ดู
///
/// key อยู่ที่นี่ ไม่ใช่ในช่อง Claude ที่ `sessionKey` อยู่: มันเป็นของหน้านี้หน้าเดียว
/// และคนที่มาตั้งค่าหน้าหุ้นคือคนที่กำลังจะเจอว่าต้องมีมัน
struct StocksDetail: View {
    @ObservedObject var model: SettingsModel
    @State private var typed = ""
    @State private var lift: Lift?

    private static let help =
        "Quotes come from Finnhub with your own key, so the quota spent is yours — get a free "
        + "one at finnhub.io. The key is stored readable only by you and never leaves this Mac. "
        + "Five symbols at most: Finnhub answers one symbol per request, and nothing is asked "
        + "at all outside US market hours, when prices do not move. The free plan covers US "
        + "listings only."

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HeadingRow(title: "Watchlist", help: Self.help)
            WatchlistCard(
                space: "stocks.watchlist",
                symbols: model.stocks.symbols,
                empty: "No symbols yet",
                lift: $lift,
                reorder: { model.setSymbolOrder($0) },
                remove: { model.removeSymbol($0) },
                entry: {
                    WatchlistEntry(
                        placeholder: "Symbol (e.g. AAPL)",
                        typed: $typed,
                        catalog: LogoCatalog.stocks,
                        taken: model.stocks.symbols,
                        hasRoom: model.stocksHaveRoom,
                        add: { model.addSymbol($0) })
                })
            StatusLine(text: statusText)
        }

        VStack(alignment: .leading, spacing: 6) {
            GroupHeading(text: "Finnhub key")
            Card {
                LabeledRow(title: "API key", subtitle: "Free at finnhub.io") {
                    Button("Set…") { model.onFinnhubKey?() }
                }
            }
            // บรรทัดนี้เป็นทางเดียวที่ผู้ใช้รู้ว่า key เข้าไหม — ช่องกรอกปิดบังตัวอักษร
            // และไม่เคยถูกเติมกลับ (กติกาเดียวกับ `sessionKey`)
            StatusLine(text: keyText, isProblem: model.stockKeyRejected)
        }
    }

    private var keyText: String {
        if model.stockKeyRejected { return "Finnhub refused this key — paste a new one." }
        if model.hasStockKey { return "A key is stored, readable only by you." }
        return "No key yet — get a free one at finnhub.io."
    }

    private var statusText: String {
        if !model.stocksStatus.isEmpty { return model.stocksStatus }
        if model.pages.isOn(.stocks) && !model.hasStockKey { return "Add a Finnhub key to start." }
        if model.pages.isOn(.stocks) && !model.stocks.isUsable { return "Add a symbol to start." }
        if !model.stocksHaveRoom { return "Five symbols is the most this page shows." }
        return ""
    }
}

/// รายการปฏิทินเป็นติ๊ก ไม่ใช่ช่องพิมพ์: ผู้ใช้ไม่ได้ตั้งชื่อปฏิทินเอง เขาเลือกจากที่ macOS
/// sync มาให้ (ADR-0005) และชื่อที่พิมพ์ผิดจะกลายเป็นหน้าที่ว่างโดยไม่บอกอะไร
///
/// ไม่มีปุ่ม "เลือกทั้งหมด": จอนี้วางให้คนอื่นเห็นได้ การเปิดทุกใบด้วยการกดครั้งเดียว
/// คือการเผลอเอาปฏิทินหมอกับปฏิทินครอบครัวขึ้นจอพร้อมกันโดยไม่ได้อ่านชื่อสักใบ
struct CalendarDetail: View {
    @ObservedObject var model: SettingsModel

    private static let help =
        "Appointments are read from the calendars macOS already syncs, so TamaClaude never "
        + "holds a calendar password of its own — add the account in System Settings and tick "
        + "it here. The page shows the next four appointments within seven days, read-only: "
        + "nothing on the desk can change an appointment."

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HeadingRow(title: "Calendars that may show", help: Self.help)
            Card {
                if model.availableCalendars.isEmpty {
                    CardRow {
                        Text(model.calendarAccess == .granted
                            ? "This Mac syncs no calendars yet"
                            : "Nothing to show until access is granted")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    ForEach(Array(model.availableCalendars.enumerated()), id: \.element.id) {
                        index, info in
                        if index > 0 { CardDivider() }
                        CardRow {
                            Toggle(
                                "",
                                isOn: Binding(
                                    get: { model.calendars.isOn(info.id) },
                                    set: { model.setCalendarOn(info.id, $0) })
                            )
                            .labelsHidden()
                            .toggleStyle(.checkbox)
                            // ชื่อบัญชีต่อท้ายเสมอ — ปฏิทินชื่อ "Calendar" สองใบจากคนละ
                            // บัญชีแยกกันไม่ออกถ้าไม่บอกว่ามาจากไหน แล้วผู้ใช้จะติ๊กใบผิด
                            // ขึ้นจอที่คนอื่นมองเห็น
                            Text(info.title)
                            if !info.source.isEmpty {
                                Text(info.source)
                                    .font(.system(size: 11)).foregroundStyle(.secondary)
                            }
                            Spacer(minLength: 0)
                        }
                    }
                }
            }

            // ปุ่มขอสิทธิ์มีความหมายครั้งเดียวในชีวิตของแอป — หลังผู้ใช้ปฏิเสธไปแล้ว macOS
            // จะไม่ถามซ้ำ กดอีกกี่ครั้งก็ไม่มีอะไรเกิดขึ้น ปุ่มจึงต้องหายและประโยคต้องเปลี่ยน
            if model.calendarAccess != .granted {
                ListFooterButtons {
                    Button("Allow access") { model.onCalendarAccess?() }
                        .disabled(model.calendarAccess != .notDetermined)
                }
            }
            StatusLine(text: statusText, isProblem: model.calendarAccess == .denied)
        }
    }

    /// ประโยคของแต่ละสภาพมาจาก `CalendarService` ที่เดียว — หน้านี้เติมเฉพาะสิ่งที่มันรู้
    /// อยู่คนเดียว: ทางออกของสิทธิ์ที่ถูกปฏิเสธ ซึ่งยาวเกินกว่าจะขึ้นจอ 320px ได้
    private var statusText: String {
        if model.calendarAccess == .denied {
            return "Calendar access was refused. Turn TamaClaude on in System Settings > "
                + "Privacy & Security > Calendars."
        }
        return model.calendarStatus
    }
}

// --- ชิ้นส่วนที่ watchlist สองใบใช้ร่วมกัน ---------------------------------------------

/// การ์ดรายการของ watchlist — แถวลากได้ ลบได้ พร้อมแถวเพิ่มอยู่ในใบเดียวกัน
///
/// สองหน้าใช้ใบนี้ร่วมกันทั้งหมด เพราะมันต่างกันแค่คำในช่องกรอกกับตารางโลโก้ —
/// การมีสองใบที่เหมือนกัน 95% คือสองใบที่จะดริฟต์กันภายในการแก้ครั้งถัดไป
struct WatchlistCard<Entry: View>: View {
    /// ชื่อพื้นที่พิกัดของการ์ดใบนี้ — คนละใบต้องคนละชื่อ
    let space: String
    /// ลำดับที่ model ถืออยู่ — ของจริงเมื่อไม่ได้ลาก
    let symbols: [String]
    let empty: String
    @Binding var lift: Lift?
    /// ลำดับใหม่ทั้งชุด ส่งครั้งเดียวตอนปล่อย — เหตุผลเดียวกับลิสต์หน้า
    let reorder: ([String]) -> Void
    let remove: (Int) -> Void
    @ViewBuilder var entry: Entry

    /// ลำดับระหว่างที่ยังลากอยู่ — `nil` คือไม่ได้ลาก
    @State private var draft: [String]?
    @State private var pitch: CGFloat = Metrics.row

    private var order: [String] { draft ?? symbols }

    private func shift(from: Int, to: Int) {
        var next = order
        guard next.indices.contains(from), next.indices.contains(to), from != to else { return }
        next.insert(next.remove(at: from), at: to)
        draft = next
    }

    private func commit() {
        guard let draft else { return }
        reorder(draft)
        self.draft = nil
    }

    var body: some View {
        Card {
            if order.isEmpty {
                CardRow { Text(empty).foregroundStyle(.secondary) }
            } else {
                ForEach(Array(order.enumerated()), id: \.element) { index, symbol in
                    if index > 0 { CardDivider() }
                    CardRow {
                        ReorderHandle(
                            index: index, count: order.count, pitch: pitch, space: space,
                            lift: $lift, move: shift, commit: commit)
                        LogoBadge(symbol: symbol)
                        Text(symbol.uppercased())
                        Spacer(minLength: 8)
                        Button {
                            remove(index)
                        } label: {
                            Image(systemName: "minus.circle")
                                .font(.system(size: 12))
                                .foregroundStyle(.secondary)
                        }
                        .buttonStyle(.plain)
                        .handCursor()
                        .help("Remove \(symbol.uppercased())")
                    }
                    .background(Color(nsColor: .controlBackgroundColor))
                    .reorderMenu(index: index, count: order.count, move: { from, to in
                        shift(from: from, to: to)
                        commit()
                    })
                    .offset(y: liftOffset(lift, at: index))
                    .zIndex(lift?.index == index ? 1 : 0)
                }
            }
            CardDivider()
            CardRow { entry }
        }
        .animation(.snappy(duration: 0.16), value: order)
        // นับเฉพาะแถวของรายการ แถวเพิ่มกับเส้นคั่นของมันไม่ใช่แถวที่ลากได้
        .measureRowPitch(count: order.count + 1, into: $pitch)
        .coordinateSpace(name: space)
    }
}

/// แถวเพิ่มของ watchlist — ช่องพิมพ์ กับเมนูของตัวที่ *มี logo*
///
/// เมนูเป็น **pull-down** ที่เลือกแล้วเพิ่มทันที ไม่ใช่ pop-up ที่จำค่าไว้รอปุ่ม Add:
/// ปุ่ม Add ปุ่มเดียวที่รับของจากสองแหล่งคือปุ่มที่ไม่มีอะไรบอกว่ามันกำลังจะเพิ่มตัวไหน ·
/// ผิดก็กดลบในแถวที่เพิ่งโผล่ ซึ่งอยู่เหนือแถวนี้พอดี
///
/// ตัวที่มีอยู่แล้วถูก **disable ไม่ใช่ถอดออก**: เมนูที่รายการหายไปเรื่อยๆ ทำให้ตำแหน่งที่
/// มือจำไว้ขยับทุกครั้งที่เพิ่ม ส่วนแถวจางบอกว่า "ตัวนี้มีแล้ว" ตรงๆ
struct WatchlistEntry: View {
    let placeholder: String
    @Binding var typed: String
    let catalog: [LogoCatalog.Entry]
    let taken: [String]
    let hasRoom: Bool
    let add: (String) -> Bool

    var body: some View {
        // ช่องถูกล้างเฉพาะตอนที่ของเข้าไปจริง — คำที่ถูกปฏิเสธ (ซ้ำ หรือเต็มแล้ว) ต้องยัง
        // อยู่ให้ผู้ใช้เห็นว่าเขาพิมพ์อะไรไป พร้อมเหตุผลในบรรทัดสถานะ
        TextField(placeholder, text: $typed)
            .frame(width: 170)
            .onSubmit { commit() }
        Button("Add") { commit() }
            .disabled(typed.trimmingCharacters(in: .whitespaces).isEmpty)
        Spacer(minLength: 8)
        Menu("Add from list") {
            ForEach(catalog, id: \.symbol) { item in
                Button {
                    _ = add(item.symbol)
                } label: {
                    Label {
                        // ชื่อที่ซ้ำกับสัญลักษณ์ (XRP, BNB, AMD) ไม่ต้องพูดสองครั้ง —
                        // สัญลักษณ์มาก่อนเพราะมันคือของที่ลงไปใน watchlist จริงและคือของ
                        // ที่ขึ้นบนจอ ชื่อเต็มเป็นแค่ตัวยืนยันว่าเลือกไม่ผิดตัว
                        Text(
                            item.name == item.symbol
                                ? item.symbol : "\(item.symbol) — \(item.name)")
                    } icon: {
                        Image(nsImage: LogoIconImage.menuImage(for: item.symbol) ?? NSImage())
                    }
                }
                .disabled(has(item.symbol))
            }
        }
        .fixedSize()
        .disabled(!hasRoom)
    }

    private func has(_ symbol: String) -> Bool {
        taken.contains { $0.caseInsensitiveCompare(symbol) == .orderedSame }
    }

    private func commit() {
        if add(typed) { typed = "" }
    }
}

// --- Wi-Fi ---------------------------------------------------------------------

/// เอาบอร์ดขึ้นเน็ต แล้วบอกว่า Mac หามันเจอจริงไหม — สองคำถามที่ไม่ใช่คำถามเดียวกัน
struct WiFiPane: View {
    @ObservedObject var model: SettingsModel

    private static let help =
        "The board only talks to this Mac over your LAN — it never contacts claude.ai itself, "
        + "so your session key stays here. Networks that isolate clients from each other "
        + "(hotels, offices) will connect but stay unreachable, which looks like success on "
        + "this page and silence on the desk."

    var body: some View {
        PaneShell(title: "Wi-Fi") {
            Card {
                CardRow {
                    VStack(alignment: .leading, spacing: 1) {
                        Text(model.wifiHeadline)
                            .font(.system(size: 13, weight: .medium))
                            .foregroundStyle(
                                model.wifiFailed ? Color(nsColor: .systemRed) : Color.primary)
                        if !model.wifiDetail.isEmpty {
                            Text(model.wifiDetail)
                                .font(.system(size: 11)).foregroundStyle(.secondary)
                        }
                    }
                    Spacer(minLength: 0)
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                HeadingRow(title: "Networks in range", help: Self.help)
                Card {
                    List(selection: $model.selectedSSID) {
                        ForEach(model.networks.rows, id: \.ssid) { row in
                            HStack(spacing: 8) {
                                Image(systemName: row.secured ? "lock.fill" : "lock.open")
                                    .font(.system(size: 10))
                                    .foregroundStyle(.secondary)
                                Text(row.ssid)
                                    .foregroundStyle(row.inRange ? .primary : .secondary)
                                // เครือข่ายที่บอร์ดจำไว้ต้องแยกออกจากที่เพิ่งเห็น ไม่งั้น
                                // ผู้ใช้ไม่รู้ว่าต้องพิมพ์รหัสซ้ำไหม และปุ่ม Forget จะดู
                                // เหมือนใช้ได้กับทุกแถว
                                if row.saved {
                                    Text("saved")
                                        .font(.system(size: 10))
                                        .foregroundStyle(Color.tama)
                                }
                                Spacer(minLength: 8)
                                // แถวที่จำไว้แต่รอบนี้ไม่เห็นยังต้องเลือกได้ — ช่องความแรง
                                // จึงบอกว่าทำไมมันอยู่ตรงนี้ แทนที่จะแสร้งเป็น 0 dBm
                                Text(row.rssi.map { "\($0) dBm" } ?? "not in range")
                                    .font(.system(size: 10).monospacedDigit())
                                    .foregroundStyle(.secondary)
                            }
                            .tag(row.ssid)
                        }
                    }
                    .listStyle(.plain)
                    .frame(height: 150)
                    .scrollContentBackground(.hidden)

                    CardDivider()
                    CardRow {
                        SecureField("Network password", text: $model.password)
                            .frame(width: 190)
                            .onSubmit { model.join() }
                        Spacer(minLength: 8)
                        if model.networks.scanning {
                            ProgressView().controlSize(.small)
                        }
                        Button("Rescan") { model.rescan() }
                        Button("Forget") { model.forget() }
                            .disabled(model.selectedSSID == nil)
                        Button("Connect") { model.join() }
                            .keyboardShortcut(.defaultAction)
                            .disabled(model.selectedSSID == nil)
                    }
                }
            }
            .disabled(!model.linked)

            // บอร์ดที่ขึ้นเน็ตสำเร็จแต่ Mac หาไม่เจอ (client isolation, คนละ subnet) จะดูดี
            // ทุกอย่างบนหน้านี้ทั้งที่ทางสำรองใช้ไม่ได้เลย — คำตอบว่า "ตกลงถึงกันไหม"
            // อยู่ในหน้า Board ที่เดียว ไม่ใช่ครึ่งหนึ่งที่นี่อีกครึ่งที่โน่น
            Footnote(
                text: "Whether this Mac can actually reach the board is a separate question, "
                    + "answered under Board.")
        }
    }
}

// --- Claude --------------------------------------------------------------------

/// ทุกอย่างที่เป็นเรื่องของ Claude Code ไม่ใช่ของจอ
struct ClaudePane: View {
    @ObservedObject var model: SettingsModel

    var body: some View {
        PaneShell(title: "Claude") {
            VStack(alignment: .leading, spacing: 6) {
                GroupHeading(text: "Quota")
                Card {
                    LabeledRow(
                        title: "Session key",
                        subtitle: "A full-account credential — kept in a file only you can read"
                    ) {
                        Button("Set…") { model.onSetSessionKey?() }
                    }
                    CardDivider()
                    LabeledRow(title: "Refresh") {
                        Picker(
                            "",
                            selection: Binding(
                                get: { model.interval },
                                set: { model.chooseInterval($0) })
                        ) {
                            ForEach(PollInterval.allCases, id: \.self) {
                                Text($0.title).tag($0)
                            }
                        }
                        .labelsHidden()
                        .fixedSize()
                    }
                    CardDivider()
                    LabeledRow(
                        title: "Read quota from the statusline",
                        subtitle: "Free, but only moves while Claude Code is open"
                    ) {
                        Toggle("", isOn: toggle(model.statusline, model.onToggleStatusline))
                            .labelsHidden().toggleStyle(.switch)
                    }
                }
                // ช่องกรอก key เป็นแบบปิดบังตัวอักษรและไม่เคยถูกเติมกลับ บรรทัดนี้จึงเป็น
                // ทางเดียวที่ผู้ใช้รู้ว่ากด Save แล้วเข้าไหม
                StatusLine(text: model.keyState.line, isProblem: model.keyState.isProblem)
            }

            VStack(alignment: .leading, spacing: 6) {
                GroupHeading(text: "This Mac")
                Card {
                    LabeledRow(title: "Launch at login") {
                        Toggle("", isOn: toggle(model.login, model.onToggleLogin))
                            .labelsHidden().toggleStyle(.switch)
                    }
                    CardDivider()
                    LabeledRow(
                        title: "Auto-start a session when idle",
                        subtitle: "Only when nothing else is running"
                    ) {
                        Toggle("", isOn: toggle(model.autoStart, model.onToggleAutoStart))
                            .labelsHidden().toggleStyle(.switch)
                    }
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                GroupHeading(text: "Hooks and logs")
                Card {
                    // บรรทัดล่างเคยเป็นพาธของไฟล์ ซึ่งไม่เคยเปลี่ยน จึงไม่เคยตอบอะไรให้ใคร
                    // ตอนนี้มันบอกว่าท่อยังมีชีวิตอยู่ไหม ซึ่งเป็นเรื่องเดียวที่คนเปิดหน้านี้
                    // มาหา · เดินทุกวินาทีตอนหน้าต่างเปิด ตัวเลขที่ขยับเป็นหลักฐานในตัวมันเอง
                    LabeledRow(title: "Hooks", subtitle: model.hooks) {
                        Button("Install") { model.onInstallHooks?() }
                    }
                    CardDivider()
                    LabeledRow(title: "Log") {
                        Button("Open") { model.onOpenLog?() }
                    }
                    CardDivider()
                    // ปลายทางอยู่ในชื่อปุ่ม ไม่ใช่คำว่า "GitHub" — แอปนี้ขอ credential
                    // เต็มบัญชี ลิงก์ที่ซ่อนปลายทางไว้หลังคำสวยๆ เป็นท่าเดียวกับที่ผู้ใช้ควรระวัง
                    //
                    // ไม่ใช้ `.buttonStyle(.link)`: มันวาดด้วยฟ้าของระบบ ซึ่งเป็นสีที่สอง
                    // ในหน้าต่างที่มี accent ได้สีเดียว
                    LabeledRow(title: "Project") {
                        Button(PanelText.projectLink) { model.onOpenProject?() }
                            .buttonStyle(.plain)
                            .foregroundStyle(Color.tama)
                            .handCursor()
                    }
                }
                Footnote(
                    text: "Hooks are what tell TamaClaude a session started, asked for "
                        + "permission, or finished. Without them the mascot never moves.")
            }
        }
    }

    /// สวิตช์ที่ *แจ้งการกด* ไม่ใช่ผูกกับค่า — `MenuBarApp` เป็นคนตัดสินว่าผลลัพธ์คืออะไร
    /// (ติดตั้งสำเร็จไหม สิทธิ์ผ่านไหม) แล้วป้อนสถานะจริงกลับมาทาง `showToggles`
    private func toggle(_ value: Bool, _ action: (() -> Void)?) -> Binding<Bool> {
        Binding(get: { value }, set: { _ in action?() })
    }
}
