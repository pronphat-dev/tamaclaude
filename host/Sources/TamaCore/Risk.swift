import Foundation

/// อะไรที่ปุ่มบนบอร์ดอนุญาตแทนคีย์บอร์ดไม่ได้
///
/// จอนี้ถูก *เหลือบมอง* ไม่ได้ถูกอ่าน — นั่นคือทั้งข้อดีและข้อจำกัดของมัน ปุ่มเขียวบน
/// จอที่ถูกเหลือบมองคือการอนุญาตที่ผู้ใช้จ่ายความสนใจให้น้อยที่สุดเท่าที่จะเป็นไปได้
/// ซึ่งพอดีกับคำขอส่วนใหญ่ ("อ่านไฟล์นี้ได้ไหม") และไม่พอเลยกับส่วนน้อยที่ทำลายของ
///
/// เกณฑ์ที่ใช้คือ **ย้อนกลับได้ไหม** ไม่ใช่ *อันตรายแค่ไหน* — `rm -rf ~/work` กับ
/// `git push --force` อยู่คนละระดับความเสียหายโดยสิ้นเชิง แต่เหมือนกันตรงที่กดผิดแล้ว
/// ไม่มีปุ่ม undo · ของที่ย้อนได้ผิดแล้วเสียเวลา ของที่ย้อนไม่ได้ผิดแล้วเสียงาน
/// และจอที่อยู่มุมโต๊ะไม่ควรมีอำนาจแบบหลัง
///
/// สิ่งที่เกิดขึ้นเมื่อเข้าเกณฑ์ไม่ใช่การปฏิเสธ แต่คือการส่งคำถามกลับไปที่ terminal
/// ซึ่งมีข้อความเต็มให้อ่านและมีเวลาให้คิด — บอร์ดยังกด deny ได้เสมอ ทุกกรณี
public enum Risk {
    /// คำที่อ่านเจอแล้วแปลว่า "ไปตอบที่คีย์บอร์ดเถอะ"
    ///
    /// เทียบแบบ substring บนข้อความที่ normalize แล้ว ไม่ใช่ parse — สิ่งที่ parse
    /// เชลล์ได้ครบจริงคือเชลล์ และเราไม่ได้กำลังเขียนเชลล์ · ผลของการเดาพลาดสองทาง
    /// ไม่เท่ากันเลย: จับเกินคือผู้ใช้ลุกไปพิมพ์ที่คีย์บอร์ด จับขาดคือของหายถาวร
    /// รายการนี้จึงกว้างไว้ก่อนโดยตั้งใจ และควรกว้างขึ้นอีกเมื่อนึกอะไรออก
    static let irreversible = [
        // ลบไฟล์
        "rm -r", "rm -f", "rm --", "rmdir ", "unlink ", "shred ", "truncate ",
        "find . -delete", "-exec rm",
        // ยกระดับสิทธิ์ และการเปลี่ยนเจ้าของ
        "sudo ", "doas ", "su -", "chown ", "chmod 777", "chmod -r",
        // git ที่เขียนทับอดีต หรือทิ้งงานที่ยังไม่ได้ commit
        "git push --force", "git push -f", "git reset --hard", "git clean -f",
        "git checkout -- ", "git restore ", "git branch -d", "git rebase",
        "git filter-branch", "git update-ref -d",
        // ดิสก์และเครื่อง
        "dd if=", "dd of=", "mkfs", "diskutil ", "fdisk", "shutdown", "reboot",
        "killall ", "pkill ",
        // ปล่อยของออกสู่โลก — ถอนคืนไม่ได้แม้จะไม่ได้ลบอะไรสักไฟล์
        "npm publish", "npm unpublish", "pod trunk push", "gh release create",
        "cargo publish", "twine upload",
        // ท่อจากเน็ตเข้าเชลล์ — เนื้อคำสั่งจริงยังไม่มีใครเห็นตอนที่ต้องกดอนุญาต
        "| sh", "| bash", "| zsh", "|sh", "|bash",
        // ฐานข้อมูล
        "drop table", "drop database", "truncate table", "delete from",
    ]

    /// เครื่องมือที่ทำได้แค่อ่าน — ตัวที่ปลอดภัยแม้ในวันที่เราอ่านเนื้อคำขอไม่ออก
    ///
    /// มีไว้ตอบกรณีเดียว: `tool_input` มาไม่ถึง (รุ่นเก่า, payload แปลก) · ตอนนั้นเรา
    /// ตาบอด และการตาบอดกับเครื่องมือที่ *เขียน* ได้ ไม่ควรจบลงที่ปุ่มเขียว
    static let readOnly: Set<String> = [
        "Read", "Grep", "Glob", "NotebookRead", "BashOutput", "WebSearch", "WebFetch",
        "TodoWrite", "TaskList", "TaskGet",
    ]

    /// บอร์ดเสนอปุ่ม Allow ให้คำขอนี้ได้ไหม
    ///
    /// `true` = ต้องกลับไปตอบที่คีย์บอร์ด · ไม่ได้แปลว่าปฏิเสธ แปลว่าคำตอบนี้ควรมา
    /// จากที่ที่มีข้อความเต็มให้อ่าน
    public static func needsKeyboard(tool: String, input: String?) -> Bool {
        guard let input, !input.isEmpty else {
            // อ่านคำขอไม่ออก: ตัดสินจากสิ่งเดียวที่ยังรู้ คือชื่อเครื่องมือ
            return !readOnly.contains(tool)
        }
        let flat = normalize(input)
        return irreversible.contains { flat.contains($0) }
    }

    /// ตัวพิมพ์เล็ก และช่องว่างช่วงละหนึ่งตัว — `rm  -rf`, `rm\n-rf` และ `rm \` + ขึ้นบรรทัด
    /// ต้องอ่านได้เหมือน `rm -rf` ทั้งหมด ไม่งั้นการขึ้นบรรทัดใหม่หนึ่งตัวก็พาคำสั่งลบไฟล์
    /// ผ่านด่านไปได้แล้ว
    static func normalize(_ text: String) -> String {
        var out = ""
        var lastWasSpace = false
        for ch in text.lowercased() {
            if ch.isWhitespace || ch == "\\" {
                if !lastWasSpace { out.append(" ") }
                lastWasSpace = true
            } else {
                out.append(ch)
                lastWasSpace = false
            }
        }
        return out
    }
}
