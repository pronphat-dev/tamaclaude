import Foundation
import TamaCore

let usage = """
tamaclaude — Claude Code session display for the CYD board

usage:
  tamaclaude                     menu bar app (runs the daemon itself)
  tamaclaude --hook              read one hook event on stdin, forward to the daemon
  tamaclaude --daemon [options]  run the daemon
  tamaclaude --install-hooks     write the hook entries into ~/.claude/settings.json
  tamaclaude --install-statusline  take over statusLine.command to capture rate_limits
  tamaclaude --remove-statusline   give the statusLine slot back to the previous command
  tamaclaude --usage-cache       read statusline JSON on stdin, write the usage cache
  tamaclaude --usage-poll        ask claude.ai for the quota once, write the cache, exit
  tamaclaude --send <json>       send one hand-written event (for testing)
  tamaclaude --decide allow|deny <id>  answer a permission request the board is holding
                                 (the id is in ~/.tamaclaude/daemon.log)

--usage-poll:
  reads the claude.ai sessionKey from ~/.tamaclaude/session-key (mode 600, never argv).
  set TAMACLAUDE_ORG_ID to pin an organization instead of discovering one.
  prints one `org <id> <name>` line per organization, then a status line.
  exit 0 wrote the cache · 2 the key was rejected · 3 the key file is unusable · 1 other
  the menu bar app runs this for you on a timer; set the key from its gear menu.

daemon options:
  --print       also print every snapshot to stdout
  --no-ble      skip bluetooth entirely (pairs well with --print)
  -v            verbose logging
"""

let rawArgs = Array(CommandLine.arguments.dropFirst())
Log.verbose = rawArgs.contains("-v")

// LaunchServices แถม `-psn_0_12345` มาให้ตอนเปิดจาก Finder และ `-v` เป็นแค่ระดับ log
// ไม่ใช่โหมด — ทั้งคู่ต้องไม่ทำให้แอปเข้าใจว่าถูกสั่งโหมดที่ไม่รู้จักแล้วออกทันที
let args = rawArgs.filter { $0 != "-v" && !$0.hasPrefix("-psn_") }

/// ต้องถืออ้างอิงไว้ ไม่งั้น DispatchSource ถูกปล่อยแล้วสัญญาณไม่ถึง
var signalSources: [DispatchSourceSignal] = []

func fail(_ msg: String) -> Never {
    Log.info(msg)
    exit(1)
}

/// อาร์กิวเมนต์ที่สองของโหมดที่เขียน cache เป็นพาธปลายทาง มีไว้เพื่อทดสอบท่อทั้งเส้น
/// โดยไม่แตะไฟล์จริง (`NSHomeDirectory()` บน macOS อ่านจาก getpwuid ไม่สน $HOME
/// จึงหลอกด้วย env ไม่ได้) — ค่าที่ขึ้นต้นด้วย `-` คือธงที่พิมพ์ผิด ไม่ใช่พาธ
/// ปล่อยผ่านแล้วจะได้ไฟล์ชื่อ `--no-ble` เงียบๆ แทนที่จะได้ข้อความบอกว่าพิมพ์ผิด
func cacheTarget(_ args: [String]) -> URL {
    guard args.count > 1 else { return Paths.usageCache }
    guard !args[1].hasPrefix("-") else { fail("\(args[1]) is not a cache path") }
    return URL(fileURLWithPath: args[1])
}

switch args.first {
case "--hook":
    exit(HookClient.run())

case "--send":
    guard args.count > 1, let data = args[1].data(using: .utf8) else {
        fail("--send needs a JSON string")
    }
    let ok = SocketClient(path: Paths.socket).send(data)
    exit(ok ? 0 : 1)

case "--decide":
    // ทางเดียวกับที่บอร์ดใช้ตอบ — มีไว้ทดสอบท่อทั้งเส้นโดยไม่ต้องมีจอ และเป็นทางออก
    // ให้คนที่บอร์ดหลุดไปกลางคำถาม โดยไม่ต้องรอให้หมดเวลาเอง
    guard args.count > 2, let verdict = ["allow": true, "deny": false][args[1]] else {
        fail("--decide needs allow or deny, then the request id")
    }
    guard let payload = try? Wire.encoder().encode(Control(decide: args[2], allow: verdict))
    else { fail("could not encode the decision") }
    exit(SocketClient(path: Paths.socket).send(payload) ? 0 : 1)

case "--install-hooks":
    do {
        try HookInstaller.install()
        Log.info("hooks installed in \(HookInstaller.settingsPath.path)")
    } catch {
        fail("could not install hooks: \(error)")
    }

case "--install-statusline":
    do {
        try StatuslineInstaller.install()
        let prev = StatuslineInstaller.delegatedCommandInScript()
        Log.info("statusline installed at \(Paths.statusline.path)")
        Log.info(prev.map { "delegating rendering to: \($0)" } ?? "no previous statusline to delegate to")
    } catch {
        fail("could not install statusline: \(error)")
    }

case "--remove-statusline":
    do {
        try StatuslineInstaller.uninstall()
        Log.info("statusline slot restored")
    } catch {
        fail("could not remove statusline: \(error)")
    }

case "--usage-cache":
    // เรียกจาก statusline.sh เท่านั้น — ต้องไม่ตายและไม่บ่นไม่ว่า stdin จะเป็นอะไร
    // เพราะ exit code ที่ไม่ใช่ 0 จะไปโผล่เป็นบรรทัด statusline ที่พังของผู้ใช้
    let stdin = FileHandle.standardInput.readDataToEndOfFile()
    let target = cacheTarget(args)
    let short = UsageWriter.ingest(stdin, to: target)
    // เขียน cache ก่อนแล้วค่อยวาด — บรรทัดที่วาดต้องเห็นตัวเลขของ render รอบนี้ ไม่ใช่รอบก่อน
    // ถ้าวาดไม่ออก (payload อ่านไม่ได้) ยังเหลือบรรทัดสั้นแบบเดิมไว้ ดีกว่าไม่มีอะไรเลย
    if let line = StatuslineRender.line(json: stdin, cacheURL: target) ?? short { print(line) }
    exit(0)

case "--usage-poll":
    // โปรเซสอายุสั้นโดยตั้งใจ ไม่ใช่ daemon — ตัวจับเวลาอยู่ที่ผู้เรียก
    // exit code แยก "ผู้ใช้ต้องไปแปะ key ใหม่" ออกจาก "เน็ตสะดุด เดี๋ยวก็หาย"
    do {
        print(PollOutput.render(try UsagePoll.run(cache: cacheTarget(args))))
    } catch let failure as UsagePoll.Failure {
        // สถานะออก stdout ทางเดียว รวมถึงตอนล้มเหลว — ผู้เรียกอ่านที่เดียวได้ทุกกรณี
        // และไม่ต้องมีไฟล์สถานะตัวที่สองให้ค้างเป็นค่าเก่าตอนลูกตายกลางคัน
        // รายการ org ไปด้วยแม้รอบนี้ล้ม ไม่งั้นการพินไว้ที่ org ที่หายไปจะล็อกตัวเอง:
        // ยิงพลาดทุกรอบ และไม่เคยได้รายการมาให้ผู้เรียกเปลี่ยนใจ
        print(PollOutput.render(UsagePoll.Report(orgs: failure.orgs, summary: failure.message)))
        exit(failure.code)
    } catch {
        print("usage poll failed: \(error)")
        exit(1)
    }

case "--daemon":
    Paths.ensureStateDir()
    Log.toFile = true
    let store = SessionStore(
        toolMap: ToolMap.loadOrDefault(Paths.toolConfig),
        timings: Timings.loadOrDefault(Paths.timingConfig))
    var transports: [Transport] = []
    if !args.contains("--no-ble") { transports.append(BLETransport()) }
    if args.contains("--print") || args.contains("--no-ble") {
        transports.append(PrintTransport())
    }
    let daemon = Daemon(store: store, transports: transports)
    do {
        try daemon.start()
    } catch {
        fail("daemon failed to start: \(error)")
    }
    // ปิดให้เรียบร้อยเสมอ ไม่งั้นไฟล์ socket ค้างและรอบหน้าสตาร์ทไม่ขึ้น
    for sig in [SIGINT, SIGTERM] {
        signal(sig, SIG_IGN)
        let src = DispatchSource.makeSignalSource(signal: sig, queue: .main)
        src.setEventHandler {
            daemon.stop()
            exit(0)
        }
        src.resume()
        signalSources.append(src)
    }
    dispatchMain()

case "--help", "-h":
    print(usage)

case nil:
    // ไม่มีอาร์กิวเมนต์ = ถูกเปิดแบบ .app ปกติ -> เมนูบาร์ (ซึ่งเป็น daemon ในตัว)
    MenuBar.run()

default:
    fail("unknown option \(args[0])\n\n\(usage)")
}
