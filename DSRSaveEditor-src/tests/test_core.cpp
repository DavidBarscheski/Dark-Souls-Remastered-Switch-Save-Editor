// Host-side tests for the platform-independent core. Build & run with:  make -C tests
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "md5.h"
#include "save.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);              \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

static std::string md5hex(const std::string& s) {
    uint8_t out[16];
    dsr::md5((const uint8_t*)s.data(), s.size(), out);
    return hex(out, 16);
}

static void putLe32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    for (int i = 0; i < 4; i++) v[off + i] = (uint8_t)(x >> (8 * i));
}
static uint32_t getLe32(const std::vector<uint8_t>& v, size_t off) {
    return v[off] | (v[off + 1] << 8) | (v[off + 2] << 16) | ((uint32_t)v[off + 3] << 24);
}

// Builds a synthetic save with a valid checksum.
static std::vector<uint8_t> makeSave(uint32_t base, size_t extra = 0x100) {
    std::vector<uint8_t> v(base + dsr::kChecksumSize + dsr::kHashedSize + extra);
    for (size_t i = 0; i < v.size(); i++) v[i] = (uint8_t)((i * 131 + 7) & 0xFF);
    putLe32(v, dsr::kBasePtrOffset, base);
    // plausible starting values
    for (size_t i = 0; i < dsr::kFieldCount; i++) putLe32(v, base + dsr::kFields[i].addr, 10);

    // Inventory: header + 0x800 blank slots, then a few real-looking entries.
    putLe32(v, base + dsr::kInvEntrySizeOff, 0x1C);
    putLe32(v, base + dsr::kInvCapacityOff, 0x800);
    putLe32(v, base + dsr::kInvCounterOff, 0x100);
    for (uint32_t s = 0; s < 0x800; s++) {
        size_t o = base + dsr::kInvArrayOff + (size_t)s * 0x1C;
        putLe32(v, o + 0, 0xFFFFFFFFu);
        putLe32(v, o + 4, 0xFFFFFFFFu);
        putLe32(v, o + 8, 0);
        putLe32(v, o + 12, 0xFD0000 | s);
        putLe32(v, o + 16, 0);
        putLe32(v, o + 20, 0);
        putLe32(v, o + 24, 0);
    }
    auto put = [&](uint32_t slot, uint32_t tag, uint32_t id, uint32_t qty) {
        size_t o = base + dsr::kInvArrayOff + (size_t)slot * 0x1C;
        putLe32(v, o + 0, tag);
        putLe32(v, o + 4, id);
        putLe32(v, o + 8, qty);
        putLe32(v, o + 16, 1);
    };
    put(5, dsr::kGoodsTag, 1030, 2);      // Titanite Chunk x2
    put(9, dsr::kGoodsTag, 1010, 12);     // Large Titanite Shard x12
    put(12, dsr::kGoodsTag, 500, 3);      // Humanity x3
    put(84, dsr::kGoodsTag, 1234, 5);     // not on the editable list
    put(85, 0x10000000u, 1030, 1);        // armor-type entry with a colliding id: must be ignored
    put(86, dsr::kGoodsTag, 1070, 150);   // impossible stack size: must be ignored
    uint8_t d[16];
    dsr::md5(v.data() + base + 16, dsr::kHashedSize, d);
    std::memcpy(v.data() + base, d, 16);
    return v;
}

static size_t addableIdxByTag(uint32_t tag) {
    for (size_t i = 0; i < dsr::kAddableCount; i++)
        if (dsr::kAddableItems[i].tag == tag) return i;
    return (size_t)-1;
}

int main() {
    // --- MD5 known-answer tests (RFC 1321 and common vectors) ---
    CHECK(md5hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(md5hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(md5hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    CHECK(md5hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    CHECK(md5hex(std::string(55, 'a')) == "ef1772b6dff9a122358552954ad0df65"); // padding edge: 55
    CHECK(md5hex(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218"); // padding edge: 56
    CHECK(md5hex(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367"); // exact block
    CHECK(md5hex(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21");

    // --- Save model ---
    {
        dsr::Save s;
        CHECK(s.load(makeSave(0x2C0)) == dsr::LoadStatus::Ok);
        CHECK(s.editable());
        CHECK(!s.dirty());
        CHECK(s.get(0) == 10); // Souls
    }
    {
        // Edit, finalize, reload: values persist and checksum is valid again.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.set(0, 999999999);
        s.set(1, 120);
        CHECK(s.dirty() && s.changed(0) && s.changed(1) && !s.changed(2));
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, 0x2C0 + 0xF0) == 999999999);
        CHECK(getLe32(out, 0x2C0 + 0xEC) == 120);

        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.get(0) == 999999999 && t.get(1) == 120);

        // Only intended bytes differ: field bytes + 16 checksum bytes.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 8 + 16);
    }
    {
        // Clamping and revert.
        dsr::Save s;
        s.load(makeSave(0x40));
        s.set(2, 1000);          // Vitality max 99
        CHECK(s.get(2) == 99);
        s.set(2, -5);            // min 1
        CHECK(s.get(2) == 1);
        s.adjust(0, -100);       // Souls floor at 0
        CHECK(s.get(0) == 0);
        s.setMax(0);
        CHECK(s.get(0) == 999999999);
        s.adjust(0, 1);          // stays at max
        CHECK(s.get(0) == 999999999);
        s.revert(0);
        CHECK(s.get(0) == 10 && !s.changed(0));
    }
    {
        // Tampered data -> checksum mismatch -> read-only, edits ignored.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 1000] ^= 0xFF;
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::ChecksumMismatch);
        CHECK(!s.editable() && s.readable());
        s.set(0, 5);
        CHECK(s.get(0) == 10);
    }
    {
        // Bad header / truncated file.
        dsr::Save s;
        CHECK(s.load(std::vector<uint8_t>(10)) == dsr::LoadStatus::TooSmall);
        auto v = makeSave(0x2C0);
        putLe32(v, dsr::kBasePtrOffset, 0xFFFFFF00u);
        CHECK(s.load(v) == dsr::LoadStatus::BadBase);
        auto w = makeSave(0x2C0, 0);
        w.resize(w.size() - 1);
        CHECK(s.load(w) == dsr::LoadStatus::BadBase);
        CHECK(!s.readable() && s.get(0) == 0);
    }

    // --- Inventory ---
    {
        dsr::Save s;
        CHECK(s.load(makeSave(0x2C0)) == dsr::LoadStatus::Ok);
        // Found: Titanite Chunk (slot 5), Large Titanite Shard (9), Humanity (12), in slot order.
        // Ignored: unlisted id 1234, armor-tagged 1030, impossible stack of 150.
        CHECK(s.itemCount() == 3);
        CHECK(std::string(s.itemName(0)) == "Titanite Chunk" && s.itemQty(0) == 2);
        CHECK(std::string(s.itemName(1)) == "Large Titanite Shard" && s.itemQty(1) == 12);
        CHECK(std::string(s.itemName(2)) == "Humanity" && s.itemQty(2) == 3);
        CHECK(!s.dirty());
    }
    {
        // Max the Titanite Chunk stack, finalize, reload.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.setItemMax(0);
        CHECK(s.itemQty(0) == 99 && s.itemChanged(0) && s.dirty());
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));

        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.itemQty(0) == 99 && t.itemQty(1) == 12 && t.itemQty(2) == 3);

        // Only the quantity (4 bytes) and the checksum (16 bytes) may differ.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 4 + 16);
        // Untouched neighbours, including the look-alikes, keep their exact bytes.
        size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };
        CHECK(getLe32(out, slotOff(84) + 8) == 5);
        CHECK(getLe32(out, slotOff(85) + 8) == 1);
        CHECK(getLe32(out, slotOff(86) + 8) == 150);
    }
    {
        // Clamp to 1..99, adjust, revert.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        s.setItemQty(1, 0);
        CHECK(s.itemQty(1) == 1);
        s.setItemQty(1, 1000);
        CHECK(s.itemQty(1) == 99);
        s.adjustItemQty(1, -10);
        CHECK(s.itemQty(1) == 89);
        s.revertItem(1);
        CHECK(s.itemQty(1) == 12 && !s.dirty());
    }
    {
        // Read-only (checksum mismatch) files cannot have items edited.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 5000] ^= 0xFF;
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::ChecksumMismatch);
        s.setItemMax(0);
        CHECK(s.itemQty(0) == 2);
    }
    {
        // Unrecognised inventory header: no items offered, stats still work.
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kInvCapacityOff, 0x400);
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        CHECK(s.itemCount() == 0);
        CHECK(s.get(0) == 10);
    }

    // --- The u32 at 0x374 varies between real saves and must not matter ---
    {
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kInvEntrySizeOff, 0x14);
        dsr::Save s;
        CHECK(s.load(v) != dsr::LoadStatus::TooSmall);
        CHECK(s.itemCount() == 3);
        // A table entry that is not a valid item (bad tag) makes the inventory unrecognised.
        auto w = makeSave(0x2C0);
        putLe32(w, 0x2C0 + dsr::kInvArrayOff + 5 * 0x1C, 0x30000000u);
        dsr::Save t; t.load(w);
        CHECK(t.itemCount() == 0);
    }

    // --- Header counters and character slots ---
    {
        // Adding to the equipment region raises the >=64 counter only.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        CHECK(s.addItem(addableIdxByTag(0x20000000u), 1) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == 0x101);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvKeyCounterOff) == getLe32(s.originalBytes(), 0x2C0 + dsr::kInvKeyCounterOff));

        // Two characters in one BND4-like file.
        const uint32_t base1 = 0x2C0, base2 = 0x2C0 + 0x60010;
        auto v = makeSave(base1);
        v.resize(base2 + 0x60010 + 0x100);
        std::memcpy(v.data(), "BND4", 4);
        putLe32(v, 0x0C, 2);
        putLe32(v, 0x40 + 0x10, base1);
        putLe32(v, 0x40 + 0x20 + 0x10, base2);
        std::memcpy(v.data() + base2, v.data() + base1, 0x60010);
        putLe32(v, base2 + dsr::kFields[dsr::kFieldLevel].addr, 77);
        uint8_t dg[16];
        dsr::md5(v.data() + base2 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + base2, dg, 16);

        auto chars = dsr::Save::listCharacters(v);
        CHECK(chars.size() == 2);
        CHECK(chars.size() == 2 && chars[0].base == base1 && chars[0].isFileDefault && !chars[1].isFileDefault);
        CHECK(chars.size() == 2 && chars[1].base == base2 && chars[1].level == 77);

        dsr::Save t;
        CHECK(t.load(v, base2) == dsr::LoadStatus::Ok);
        CHECK(t.base() == base2 && t.get(dsr::kFieldLevel) == 77);
        t.set(dsr::kFirstStat, 50);
        auto o2 = t.finalize();
        CHECK(dsr::Save::checksumValid(o2, base2));
        CHECK(std::memcmp(o2.data(), v.data(), base2) == 0); // first character untouched
        dsr::Save fresh;
        CHECK(fresh.load(o2) == dsr::LoadStatus::Ok && fresh.get(dsr::kFirstStat) == 10); // default still char 1
    }

    // --- Key items and ammunition ---
    {
        size_t key = (size_t)-1, ammo = (size_t)-1, plain = (size_t)-1;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            const dsr::AddableDef& d = dsr::kAddableItems[i];
            if (d.keyItem && key == (size_t)-1) key = i;
            if (d.tag == 0 && d.maxNum > 1 && ammo == (size_t)-1) ammo = i;
            if (d.tag == 0 && d.maxNum == 1 && plain == (size_t)-1) plain = i;
        }
        CHECK(key != (size_t)-1 && ammo != (size_t)-1 && plain != (size_t)-1);
        CHECK(dsr::kAddableItems[ammo].maxNum == 999 && dsr::kAddableItems[ammo].durability == 0);
        dsr::Save s;
        s.load(makeSave(0x2C0));
        uint32_t keyCtr0 = getLe32(s.originalBytes(), 0x2C0 + dsr::kInvKeyCounterOff);
        uint32_t ctr0 = getLe32(s.originalBytes(), 0x2C0 + dsr::kInvCounterOff);
        CHECK(s.addItem(key, 1) == dsr::AddResult::Added);
        CHECK(s.addItem(key, 1) == dsr::AddResult::AlreadyOwned);
        CHECK(s.addItem(ammo, 5) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        // The key item went below slot 64 and raised only the key counter; ammo went to the main region.
        CHECK(getLe32(out, 0x2C0 + dsr::kInvKeyCounterOff) == keyCtr0 + 1);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == ctr0 + 1);
        int keySlot = -1, ammoSlot = -1;
        for (uint32_t sl = 0; sl < 0x800; sl++) {
            size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)sl * 0x1C;
            if (getLe32(out, o) == dsr::kGoodsTag && getLe32(out, o + 4) == dsr::kAddableItems[key].id) keySlot = (int)sl;
            if (getLe32(out, o) == 0 && getLe32(out, o + 4) == dsr::kAddableItems[ammo].id) {
                ammoSlot = (int)sl;
                CHECK(getLe32(out, o + 8) == 999);
            }
        }
        CHECK(keySlot >= 0 && keySlot < 64);
        CHECK(ammoSlot >= 64);
        CHECK(dsr::kAddableItems[plain].maxNum == 1);
    }

    // --- Adding rings and armor ---
    {
        size_t ring = (size_t)-1, armor = (size_t)-1;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            if (dsr::kAddableItems[i].tag == 0x20000000u && ring == (size_t)-1) ring = i;
            if (dsr::kAddableItems[i].tag == 0x10000000u && armor == (size_t)-1) armor = i;
        }
        CHECK(ring != (size_t)-1 && armor != (size_t)-1);
        const size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };
        dsr::Save s;
        s.load(makeSave(0x2C0));
        CHECK(!s.owns(ring) && !s.owns(armor));
        CHECK(s.addItem(ring, 99) == dsr::AddResult::Added);
        CHECK(s.addItem(armor, 99) == dsr::AddResult::Added);
        CHECK(s.owns(ring) && s.owns(armor));
        CHECK(s.addItem(ring, 1) == dsr::AddResult::AlreadyOwned);
        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x102);
        // The first freed slot after the first equip entry is 23, then 24 if it is also freed.
        int found = 0;
        for (uint32_t slot = 0; slot < 0x800; slot++) {
            uint32_t tag = getLe32(out, slotOff(slot));
            uint32_t id = getLe32(out, slotOff(slot) + 4);
            if (tag == 0x20000000u && id == dsr::kAddableItems[ring].id) {
                found++;
                CHECK(getLe32(out, slotOff(slot) + 8) == 1);
                CHECK(getLe32(out, slotOff(slot) + 12) == ((dsr::kAddableItems[ring].sortKey << 12) | slot));
                CHECK(getLe32(out, slotOff(slot) + 20) == 0);
            }
            if (tag == 0x10000000u && id == dsr::kAddableItems[armor].id) {
                found++;
                CHECK(getLe32(out, slotOff(slot) + 8) == 1);
                CHECK(getLe32(out, slotOff(slot) + 12) == ((dsr::kAddableItems[armor].sortKey << 12) | slot));
                CHECK(getLe32(out, slotOff(slot) + 20) == dsr::kAddableItems[armor].durability);
                CHECK(dsr::kAddableItems[armor].durability > 0);
            }
        }
        CHECK(found == 2);
        // Undo: removing both returns the file to unchanged.
        dsr::Save u;
        u.load(makeSave(0x2C0));
        u.addItem(ring, 1); u.addItem(armor, 1);
        CHECK(u.dirty());
        CHECK(u.revertAdded(ring) && u.revertAdded(armor) && !u.revertAdded(ring));
        CHECK(!u.dirty());
        // Read-only saves refuse.
        auto bad = makeSave(0x2C0); bad[0x2C0 + 20] ^= 1;
        dsr::Save ro; ro.load(bad);
        CHECK(ro.addItem(ring, 1) == dsr::AddResult::NotAllowed);
    }

    // --- Adding items ---
    {
        auto addableIdx = [](uint32_t id) {
            for (size_t i = 0; i < dsr::kAddableCount; i++)
                if (dsr::kAddableItems[i].tag == dsr::kGoodsTag && dsr::kAddableItems[i].id == id) return i;
            return (size_t)-1;
        };
        // Table sanity: unique ids and keys, and every addable item is also editable afterwards.
        for (size_t a = 0; a < dsr::kAddableCount; a++) {
            if (dsr::kAddableItems[a].tag == dsr::kGoodsTag) {
                bool editable = false;
                for (size_t e = 0; e < dsr::kEditableItemCount; e++)
                    editable |= dsr::kEditableItems[e].id == dsr::kAddableItems[a].id;
                CHECK(editable);
            }
            CHECK(dsr::kAddableItems[a].sortKey > 0 && dsr::kAddableItems[a].sortKey < 0x100000);
            for (size_t b2 = a + 1; b2 < dsr::kAddableCount; b2++) {
                bool sameTag = dsr::kAddableItems[a].tag == dsr::kAddableItems[b2].tag;
                CHECK(!(sameTag && dsr::kAddableItems[a].id == dsr::kAddableItems[b2].id));
                // Weapons/armor may legitimately share a key (transformed weapons carry their source's key).
                bool uniqueKeys = dsr::kAddableItems[a].tag == dsr::kGoodsTag || dsr::kAddableItems[a].tag == 0x20000000u;
                CHECK(!(sameTag && uniqueKeys && dsr::kAddableItems[a].sortKey == dsr::kAddableItems[b2].sortKey));
            }
        }

        const size_t base = 0x2C0;
        auto slotOff = [&](uint32_t slot) { return base + dsr::kInvArrayOff + (size_t)slot * 0x1C; };

        // Add a new stack: goes to the first freed slot after the first armor entry (85) -> 87,
        // because slot 86 is occupied.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t blue = addableIdx(1040), idx = 999;
        CHECK(blue != (size_t)-1);
        CHECK(s.addItem(blue, 99, &idx) == dsr::AddResult::Added);
        CHECK(s.itemCount() == 4 && idx < s.itemCount());
        CHECK(std::string(s.itemName(idx)) == "Blue Titanite Chunk");
        CHECK(s.itemQty(idx) == 99 && s.itemIsNew(idx) && s.itemChanged(idx) && s.dirty());

        std::vector<uint8_t> out = s.finalize();
        CHECK(dsr::Save::checksumValid(out));
        CHECK(getLe32(out, slotOff(87) + 0) == dsr::kGoodsTag);
        CHECK(getLe32(out, slotOff(87) + 4) == 1040);
        CHECK(getLe32(out, slotOff(87) + 8) == 99);
        CHECK(getLe32(out, slotOff(87) + 12) == ((1006u << 12) | 87u));
        CHECK(getLe32(out, slotOff(87) + 16) == 1);
        CHECK(getLe32(out, slotOff(87) + 20) == 0 && getLe32(out, slotOff(87) + 24) == 0);
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x101);

        // Calling finalize again must not raise the counter a second time.
        std::vector<uint8_t> again = s.finalize();
        CHECK(again == out);

        // Only the new slot (<=28 bytes), the counter (<=4) and the checksum (<=16) may differ.
        const auto& orig = s.originalBytes();
        size_t diff = 0;
        for (size_t i = 0; i < out.size(); i++) diff += (out[i] != orig[i]);
        CHECK(diff <= 28 + 4 + 16);
        CHECK(getLe32(out, slotOff(86) + 4) == 1070 && getLe32(out, slotOff(85) + 4) == 1030);

        // Reload: the new stack is an ordinary, editable stack now.
        dsr::Save t;
        CHECK(t.load(out) == dsr::LoadStatus::Ok);
        CHECK(t.itemCount() == 4);
        size_t k = t.findOwned(blue);
        CHECK(k < t.itemCount() && t.itemQty(k) == 99 && !t.itemIsNew(k) && !t.dirty());
    }
    {
        // Two additions use consecutive slots and raise the counter by two.
        const size_t base = 0x2C0;
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t a = 0, b2 = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++) {
            if (dsr::kAddableItems[i].id == 1050) a = i;
            if (dsr::kAddableItems[i].id == 1130) b2 = i;
        }
        CHECK(s.addItem(a, 5) == dsr::AddResult::Added);
        CHECK(s.addItem(b2, 7) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 87 * 0x1C + 4) == 1050);
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 88 * 0x1C + 4) == 1130);
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x102);
        CHECK(dsr::Save::checksumValid(out));
    }
    {
        // Owning the item already: its quantity is set, no new slot, counter untouched.
        const size_t base = 0x2C0;
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t chunk = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++)
            if (dsr::kAddableItems[i].id == 1030) chunk = i;
        size_t idx = 0;
        CHECK(s.addItem(chunk, 99, &idx) == dsr::AddResult::SetExisting);
        CHECK(!s.itemIsNew(idx) && s.itemQty(idx) == 99 && s.itemCount() == 3);
        auto out = s.finalize();
        CHECK(getLe32(out, base + dsr::kInvCounterOff) == 0x100);
        CHECK(getLe32(out, base + dsr::kInvArrayOff + 87 * 0x1C) == 0xFFFFFFFFu); // slot 87 still blank
    }
    {
        // Reverting a new item removes it and leaves the file byte-for-byte as it was.
        dsr::Save s;
        auto orig = makeSave(0x2C0);
        s.load(orig);
        size_t a = 0;
        for (size_t i = 0; i < dsr::kAddableCount; i++)
            if (dsr::kAddableItems[i].id == 1060) a = i;
        size_t idx = 0;
        CHECK(s.addItem(a, 50, &idx) == dsr::AddResult::Added);
        CHECK(s.itemCount() == 4);
        s.revertItem(idx);
        CHECK(s.itemCount() == 3 && !s.dirty());
        CHECK(s.finalize() == orig);
    }
    {
        // No freed slot left (all blanks are never-used ones): the game appends after the last
        // used slot, so that is what happens (slot 86 is the last used one -> 87, then 88).
        auto v = makeSave(0x2C0);
        for (uint32_t slot = 0; slot < 0x800; slot++) {
            size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)slot * 0x1C;
            if (getLe32(v, o) == 0xFFFFFFFFu) putLe32(v, o + 12, 0xFFFFFFFFu);
        }
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        size_t w = addableIdxByTag(0x00000000u);
        CHECK(w != (size_t)-1);
        CHECK(s.addItem(5, 10) == dsr::AddResult::Added);
        CHECK(s.addItem(w, 1) == dsr::AddResult::Added);
        auto out = s.finalize();
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 87 * 0x1C + 4) == dsr::kAddableItems[5].id);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 4) == dsr::kAddableItems[w].id);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 0) == 0u);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvArrayOff + 88 * 0x1C + 20) == dsr::kAddableItems[w].durability);
        CHECK(getLe32(out, 0x2C0 + dsr::kInvCounterOff) == 0x102);

        // A save whose last slot is used has nowhere to append: refuse, change nothing.
        auto full = v;
        size_t o = 0x2C0 + dsr::kInvArrayOff + (size_t)0x7FF * 0x1C;
        putLe32(full, o + 0, dsr::kGoodsTag); putLe32(full, o + 4, 1234); putLe32(full, o + 8, 1); putLe32(full, o + 12, 0x7FF);
        dsr::md5(full.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(full.data() + 0x2C0, dg, 16);
        dsr::Save f;
        CHECK(f.load(full) == dsr::LoadStatus::Ok);
        CHECK(f.addItem(5, 10) == dsr::AddResult::NoFreeSlot);
        CHECK(!f.dirty() && f.finalize() == full);
    }
    {
        // Read-only file, bad index, and unrecognised inventory header are all refused.
        auto v = makeSave(0x2C0);
        v[0x2C0 + 16 + 5000] ^= 0xFF;
        dsr::Save ro;
        CHECK(ro.load(v) == dsr::LoadStatus::ChecksumMismatch);
        CHECK(ro.addItem(5, 10) == dsr::AddResult::NotAllowed);

        dsr::Save ok;
        ok.load(makeSave(0x2C0));
        CHECK(ok.addItem(dsr::kAddableCount, 10) == dsr::AddResult::NotAllowed);

        auto w = makeSave(0x2C0);
        putLe32(w, 0x2C0 + dsr::kInvCapacityOff, 0x400);
        uint8_t dg[16];
        dsr::md5(w.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(w.data() + 0x2C0, dg, 16);
        dsr::Save un;
        un.load(w);
        CHECK(un.addItem(5, 10) == dsr::AddResult::NotAllowed);
    }
    {
        // Quantity is clamped to 1..99.
        dsr::Save s;
        s.load(makeSave(0x2C0));
        size_t idx = 0;
        CHECK(s.addItem(2, 0, &idx) != dsr::AddResult::NotAllowed); // kAddableItems[2]: quantity 0 is raised to 1
        CHECK(s.itemQty(idx) == 1);
        s.setItemQty(idx, 500);
        CHECK(s.itemQty(idx) == 99);
    }

    // --- Automatic Level ---
    {
        CHECK(std::string(dsr::kFields[dsr::kFieldLevel].name) == "Level");
        CHECK(std::string(dsr::kFields[dsr::kFirstStat].name) == "Vitality");
        CHECK(std::string(dsr::kFields[dsr::kLastStat].name) == "Faith");
        CHECK(dsr::kLastStat - dsr::kFirstStat == 7); // exactly eight stats

        dsr::Save s;
        s.load(makeSave(0x2C0)); // eight stats of 10 (sum 80), Level 10 -> offset 70
        CHECK(s.levelOffset() == 70);
        s.set(2, 15); // Vitality 15 -> sum 85
        CHECK(s.get(1) == 15 && s.changed(1));
        s.adjust(5, 5); // Strength 15 -> sum 90
        CHECK(s.get(1) == 20);
        s.revert(2);
        CHECK(s.get(1) == 15);
        s.revert(5);
        CHECK(s.get(1) == 10 && !s.changed(1) && !s.dirty());

        // Level can still be set directly, and stays put until a stat changes again.
        s.set(1, 50);
        CHECK(s.get(1) == 50);
        s.set(0, 123); // souls do not touch Level
        CHECK(s.get(1) == 50);

        // Clamping: all stats to 1 would give a level below 1.
        dsr::Save lo;
        lo.load(makeSave(0x2C0));
        for (size_t i = dsr::kFirstStat; i <= dsr::kLastStat; i++) lo.set(i, 1);
        CHECK(lo.get(1) == 1);

        // Saved level survives a write and reload, and the offset is unchanged.
        dsr::Save w;
        w.load(makeSave(0x2C0));
        w.set(3, 25); // Attunement 25 -> sum 95 -> Level 25
        auto out = w.finalize();
        CHECK(dsr::Save::checksumValid(out));
        dsr::Save r;
        CHECK(r.load(out) == dsr::LoadStatus::Ok);
        CHECK(r.get(1) == 25 && r.get(3) == 25 && r.levelOffset() == 70);
    }
    {
        // Implausible offset in the file (Level far above the stat total): fall back to 82.
        auto v = makeSave(0x2C0);
        putLe32(v, 0x2C0 + dsr::kFields[dsr::kFieldLevel].addr, 500);
        uint8_t dg[16];
        dsr::md5(v.data() + 0x2C0 + 16, dsr::kHashedSize, dg);
        std::memcpy(v.data() + 0x2C0, dg, 16);
        dsr::Save s;
        CHECK(s.load(v) == dsr::LoadStatus::Ok);
        CHECK(s.levelOffset() == 82);
        s.set(2, 99); // sum 169 -> Level 87
        CHECK(s.get(1) == 87);
    }

    if (failures == 0) std::printf("All tests passed.\n");
    return failures ? 1 : 0;
}
