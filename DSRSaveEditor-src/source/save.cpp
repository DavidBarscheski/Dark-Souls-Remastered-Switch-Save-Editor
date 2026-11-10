#include "save.h"
#include "cut_count.h"

#include <algorithm>
#include <cstring>

#include "md5.h"


namespace dsr {

// Addresses come from the EdiZon Dark Souls Remastered config. Ranges are
// deliberately conservative guards, not values taken from that config.
const FieldDef kFields[] = {
    {"Souls",        0xF0, 0, 999999999},
    {"Level",        0xEC, 1, 802},
    {"Vitality",     0x9C, 1, 99},
    {"Attunement",   0xA4, 1, 99},
    {"Endurance",    0xAC, 1, 99},
    {"Strength",     0xB4, 1, 99},
    {"Dexterity",    0xBC, 1, 99},
    {"Resistance",   0xE4, 1, 99},
    {"Intelligence", 0xC4, 1, 99},
    {"Faith",        0xCC, 1, 99},
    {"HP",           0x70, 0, 9999},
    {"Max HP",       0x78, 0, 9999},
    {"Stamina",      0x8C, 0, 999},
    {"Max Stamina",  0x94, 0, 999},
};
const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

// Goods whose quantity can be edited and added. Generated from the game's own EquipParamGoods table
// (research/gen_addable.py): the stack limit is the game's maxNum, the sort key is its sortId.
const ItemDef kEditableItems[] = {
#define GOODS(id, name, key, max, kind) {id, name, max},
#include "goods_data.inc"
#undef GOODS
};
const size_t kEditableItemCount = sizeof(kEditableItems) / sizeof(kEditableItems[0]);

// Everything that can be added as a new inventory entry. The sort key goes into the upper bits of
// the entry's handle (handle = sortKey << 12 | slot). It is the game's sortId for goods and rings and
// sortId * 100 for weapons and armor, checked against every item in six real Switch saves.
const AddableDef kAddableItems[] = {
#define GOODS(id, name, key, max, kind) {kGoodsTag, id, name, key, 0, max, kind},
#include "goods_data.inc"
#undef GOODS
#include "addable_data.inc"
#include "cut_data.inc"
};
const size_t kAddableCount = sizeof(kAddableItems) / sizeof(kAddableItems[0]);
const size_t kNormalAddableCount = kAddableCount - DSR_CUT_COUNT;

const char* statusText(LoadStatus s) {
    switch (s) {
        case LoadStatus::Ok:               return "OK - checksum verified";
        case LoadStatus::TooSmall:         return "File is too small to be a valid save";
        case LoadStatus::BadBase:          return "Unexpected header (base offset out of range)";
        case LoadStatus::ChecksumMismatch: return "Checksum does not match - layout differs, READ-ONLY";
    }
    return "";
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void putLe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// Returns false if the header cannot describe a valid region. `base` is set
// whenever the base pointer itself is usable.
static LoadStatus locate(const std::vector<uint8_t>& b, uint32_t& base, int64_t forcedBase = -1) {
    if (b.size() < kBasePtrOffset + 4) return LoadStatus::TooSmall;
    base = forcedBase >= 0 ? (uint32_t)forcedBase : le32(b.data() + kBasePtrOffset);
    uint64_t end = (uint64_t)base + kChecksumSize + kHashedSize;
    if (end > b.size()) return LoadStatus::BadBase;
    // Every field must also lie inside the hashed region.
    for (size_t i = 0; i < kFieldCount; i++) {
        uint64_t f = (uint64_t)base + kFields[i].addr + 4;
        if (f > end) return LoadStatus::BadBase;
    }
    return LoadStatus::Ok;
}

bool Save::checksumValid(const std::vector<uint8_t>& bytes, int64_t forcedBase) {
    uint32_t base = 0;
    if (locate(bytes, base, forcedBase) != LoadStatus::Ok) return false;
    uint8_t digest[16];
    md5(bytes.data() + base + kChecksumSize, kHashedSize, digest);
    return std::memcmp(digest, bytes.data() + base, kChecksumSize) == 0;
}

// Lists every BND4 entry that looks like a character: inventory capacity 0x800 and a valid checksum.
// The file's own base pointer (offset 0x28) is the entry the game treats as character slot 1.
std::vector<CharacterInfo> Save::listCharacters(const std::vector<uint8_t>& b) {
    std::vector<CharacterInfo> out;
    if (b.size() < 0x40 || std::memcmp(b.data(), "BND4", 4) != 0) return out;
    uint32_t n = le32(b.data() + 0x0C);
    if (n == 0 || n > 64) return out;
    uint32_t primary = le32(b.data() + kBasePtrOffset);
    for (uint32_t i = 0; i < n; i++) {
        size_t e = 0x40 + (size_t)i * 0x20;
        if (e + 0x20 > b.size()) break;
        uint32_t base = le32(b.data() + e + 0x10);
        uint32_t cur = 0;
        if (locate(b, cur, base) != LoadStatus::Ok) continue;
        if (le32(b.data() + base + kInvCapacityOff) != kInvCapacity) continue;
        if (!checksumValid(b, base)) continue;
        CharacterInfo ci;
        ci.entry = (int)i;
        ci.base = base;
        ci.level = le32(b.data() + base + kFields[kFieldLevel].addr);
        ci.souls = le32(b.data() + base + kFields[0].addr);
        ci.isFileDefault = base == primary;
        out.push_back(ci);
    }
    return out;
}

LoadStatus Save::load(std::vector<uint8_t> bytes, int64_t forcedBase) {
    orig_ = std::move(bytes);
    cur_ = orig_;
    base_ = 0;

    items_.clear();
    LoadStatus s = locate(orig_, base_, forcedBase);
    if (s == LoadStatus::Ok && !checksumValid(orig_, forcedBase)) s = LoadStatus::ChecksumMismatch;
    status_ = s;
    levelOffset_ = 82;
    if (readable()) {
        int64_t sum = 0;
        for (size_t i = kFirstStat; i <= kLastStat; i++) sum += readU32(orig_, i);
        int64_t off = sum - (int64_t)readU32(orig_, kFieldLevel);
        if (off > 0 && off <= 200) levelOffset_ = (int)off;
        indexItems();
    }
    return status_;
}

// Finds stacks of the editable items. Silently finds nothing if the inventory header does not
// look exactly like the one we studied, so a different layout can never be edited by mistake.
bool Save::invRecognised() const {
    if (!readable()) return false;
    const uint8_t* p = orig_.data() + base_;
    // The u32 at kInvEntrySizeOff is NOT a reliable entry size: it is 0x1C in three saves, 0x14 in
    // another Switch save and 0x15 in a PC save, while the real stride is 0x1C in all of them.
    // So it is ignored. Instead require the known capacity and check that every non-empty entry has a known type tag.
    // (Handles are NOT checked: 49 entries in real saves have unrelated handles.)
    uint32_t cap = le32(p + kInvCapacityOff);
    if (cap != kInvCapacity) return false;
    uint64_t arrayEnd = (uint64_t)kInvArrayOff + (uint64_t)cap * kInvEntrySize;
    if (arrayEnd > (uint64_t)kChecksumSize + kHashedSize) return false;
    for (uint32_t slot = 0; slot < cap; slot++) {
        const uint8_t* e = p + kInvArrayOff + (size_t)slot * kInvEntrySize;
        uint32_t tag = le32(e);
        if (tag == 0xFFFFFFFFu) continue;
        if (tag != 0x00000000u && tag != 0x10000000u && tag != 0x20000000u && tag != kGoodsTag) return false;
    }
    return true;
}

// Rebuilds the list of editable stacks from the current contents (including anything added in
// this session). Finds nothing if the inventory header is not exactly the one we studied.
void Save::indexItems() {
    items_.clear();
    if (!invRecognised()) return;
    uint32_t cap = le32(orig_.data() + base_ + kInvCapacityOff);

    for (uint32_t slot = 0; slot < cap; slot++) {
        const uint8_t* e = cur_.data() + slotOffset(slot);
        if (le32(e) != kGoodsTag) continue;
        uint32_t id = le32(e + 4), qty = le32(e + 8);
        for (size_t d = 0; d < kEditableItemCount; d++)
            if (kEditableItems[d].id == id) {
                if (qty >= 1 && qty <= kEditableItems[d].maxNum) items_.push_back({slot, &kEditableItems[d]});
                break;
            }
    }
}

const char* Save::itemName(size_t i) const { return i < items_.size() ? items_[i].def->name : ""; }

uint32_t Save::itemQty(size_t i) const {
    return i < items_.size() ? le32(cur_.data() + itemQtyOffset(i)) : 0;
}

uint32_t Save::itemOriginalQty(size_t i) const {
    return i < items_.size() ? le32(orig_.data() + itemQtyOffset(i)) : 0;
}

void Save::setItemQty(size_t i, int64_t value) {
    if (!editable() || i >= items_.size()) return;
    value = std::max<int64_t>(1, std::min<int64_t>(items_[i].def->maxNum, value));
    putLe32(cur_.data() + itemQtyOffset(i), (uint32_t)value);
}

void Save::adjustItemQty(size_t i, int64_t delta) { setItemQty(i, (int64_t)itemQty(i) + delta); }
void Save::setItemMax(size_t i) { if (i < items_.size()) setItemQty(i, items_[i].def->maxNum); }
uint32_t Save::itemMaxQty(size_t i) const { return i < items_.size() ? items_[i].def->maxNum : 0; }
bool Save::itemIsNew(size_t i) const {
    return i < items_.size() && le32(orig_.data() + slotOffset(items_[i].slot)) == 0xFFFFFFFFu;
}

void Save::revertItem(size_t i) {
    if (!editable() || i >= items_.size()) return;
    if (itemIsNew(i)) {
        // Put the slot back exactly as it was in the file, and drop it from the list.
        size_t off = slotOffset(items_[i].slot);
        std::memcpy(cur_.data() + off, orig_.data() + off, kInvEntrySize);
        items_.erase(items_.begin() + (std::ptrdiff_t)i);
        return;
    }
    setItemQty(i, itemOriginalQty(i));
}

size_t Save::findOwned(size_t addable) const {
    if (addable >= kAddableCount || kAddableItems[addable].tag != kGoodsTag) return items_.size();
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i].def->id == kAddableItems[addable].id) return i;
    return items_.size();
}

bool Save::owns(size_t addable) const {
    return addable < kAddableCount && ownsEntry(kAddableItems[addable].tag, kAddableItems[addable].id);
}

bool Save::revertAdded(size_t addable) {
    if (!editable() || addable >= kAddableCount || !invRecognised()) return false;
    uint32_t cap = le32(orig_.data() + base_ + kInvCapacityOff);
    for (uint32_t slot = 0; slot < cap; slot++) {
        size_t off = slotOffset(slot);
        if (le32(orig_.data() + off) == 0xFFFFFFFFu && le32(cur_.data() + off) == kAddableItems[addable].tag &&
            le32(cur_.data() + off + 4) == kAddableItems[addable].id) {
            std::memcpy(cur_.data() + off, orig_.data() + off, kInvEntrySize);
            indexItems();
            return true;
        }
    }
    return false;
}

// True if any inventory entry (of any type) already has this type tag and id.
bool Save::ownsEntry(uint32_t tag, uint32_t id) const {
    if (!invRecognised()) return false;
    uint32_t cap = le32(orig_.data() + base_ + kInvCapacityOff);
    for (uint32_t slot = 0; slot < cap; slot++) {
        const uint8_t* e = cur_.data() + slotOffset(slot);
        if (le32(e) == tag && le32(e + 4) == id) return true;
    }
    return false;
}

// Chooses the slot for a new entry. In both real captures the game used the first freed slot
// after the first weapon/armor entry (the front of the table holds the starting items), so this
// does the same. A "freed" slot is an empty one whose handle still carries its own slot number.
// Never-used slots are only used to append after the last used slot (see below).
int Save::pickFreeSlot(bool keyItem) const {
    if (!invRecognised()) return -1;
    uint32_t cap = le32(orig_.data() + base_ + kInvCapacityOff);
    if (cap > 0xFFF) cap = 0xFFF; // slot number must fit in the handle's low 12 bits

    auto freed = [&](uint32_t slot) {
        const uint8_t* e = cur_.data() + slotOffset(slot);
        uint32_t handle = le32(e + 12);
        return le32(e) == 0xFFFFFFFFu && le32(e + 4) == 0xFFFFFFFFu && handle != 0xFFFFFFFFu &&
               (handle & 0xFFF) == slot;
    };

    int firstEquip = -1;
    for (uint32_t slot = 0; slot < cap && firstEquip < 0; slot++) {
        uint32_t t = le32(cur_.data() + slotOffset(slot));
        if (t == 0x10000000u || t == 0x00000000u) firstEquip = (int)slot;
    }
    // Slots below kEquipRegionStart hold key items and have their own header counter; never use them.
    if (keyItem) {
        // Key items live in slots 0-63. No real save shows the game adding one, so use the same
        // idea as elsewhere: a freed slot first, else the slot right after the last used one.
        uint32_t lim = cap < kEquipRegionStart ? cap : kEquipRegionStart;
        for (uint32_t slot = 0; slot < lim; slot++)
            if (freed(slot)) return (int)slot;
        uint32_t last = 0;
        bool any = false;
        for (uint32_t slot = 0; slot < lim; slot++)
            if (le32(cur_.data() + slotOffset(slot)) != 0xFFFFFFFFu) { last = slot; any = true; }
        uint32_t nxt = any ? last + 1 : 0;
        if (nxt < lim) {
            const uint8_t* e = cur_.data() + slotOffset(nxt);
            if (le32(e) == 0xFFFFFFFFu && le32(e + 4) == 0xFFFFFFFFu && le32(e + 12) == 0xFFFFFFFFu) return (int)nxt;
        }
        return -1;
    }
    uint32_t from = (uint32_t)(firstEquip < 0 ? 0 : firstEquip + 1);
    if (from < kEquipRegionStart) from = kEquipRegionStart;
    for (uint32_t slot = from; slot < cap; slot++)
        if (freed(slot)) return (int)slot;

    // No freed slot is left. Observed in a real save: after every freed slot was used up, the game
    // put 100 weapons taken out of the box into consecutive never-used slots, starting right after
    // the last used slot. Do the same: append at (last used slot + 1), if that slot is never-used.
    uint32_t lastUsed = 0;
    for (uint32_t slot = 0; slot < cap; slot++)
        if (le32(cur_.data() + slotOffset(slot)) != 0xFFFFFFFFu) lastUsed = slot;
    uint32_t next = lastUsed + 1;
    if (next >= from && next < cap) {
        const uint8_t* e = cur_.data() + slotOffset(next);
        if (le32(e) == 0xFFFFFFFFu && le32(e + 4) == 0xFFFFFFFFu && le32(e + 12) == 0xFFFFFFFFu) return (int)next;
    }
    return -1;
}

AddResult Save::addItem(size_t addable, uint32_t qty, size_t* itemIndex) {
    if (!editable() || addable >= kAddableCount || !invRecognised()) return AddResult::NotAllowed;
    const AddableDef& def = kAddableItems[addable];
    qty = std::max<uint32_t>(1, std::min<uint32_t>(def.maxNum, qty));

    if (def.tag != kGoodsTag || def.maxNum == 1) {
        // Single, non-stackable entries (rings, armor, weapons, key items, spells, one-off goods).
        // If you already own one, nothing is changed.
        if (ownsEntry(def.tag, def.id)) return AddResult::AlreadyOwned;
        qty = 1;
    }
    // Ammunition (a weapon-type entry with a stack) is also left alone if owned.
    if (def.tag != kGoodsTag && def.maxNum > 1) qty = def.maxNum;
    size_t owned = findOwned(addable);
    if (owned < items_.size()) {
        setItemQty(owned, qty);
        if (itemIndex) *itemIndex = owned;
        return AddResult::SetExisting;
    }

    int slot = pickFreeSlot(def.keyItem != 0);
    if (slot < 0) return AddResult::NoFreeSlot;

    uint8_t* e = cur_.data() + slotOffset((uint32_t)slot);
    putLe32(e + 0, def.tag);
    putLe32(e + 4, def.id);
    putLe32(e + 8, qty);
    putLe32(e + 12, (def.sortKey << 12) | (uint32_t)slot);
    putLe32(e + 16, 1); // flag seen on every real entry
    putLe32(e + 20, def.durability); // fresh durability (0 for goods and rings)
    putLe32(e + 24, 0);

    indexItems();
    if (def.tag != kGoodsTag) { // not in the editable list; nothing to select
        if (itemIndex) *itemIndex = items_.size();
        return AddResult::Added;
    }
    size_t idx = items_.size();
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i].slot == (uint32_t)slot) idx = i;
    if (itemIndex) *itemIndex = idx;
    return AddResult::Added;
}

uint32_t Save::readU32(const std::vector<uint8_t>& v, size_t field) const {
    if (!readable() || field >= kFieldCount) return 0;
    return le32(v.data() + base_ + kFields[field].addr);
}

uint32_t Save::get(size_t field) const { return readU32(cur_, field); }
uint32_t Save::original(size_t field) const { return readU32(orig_, field); }

bool Save::dirty() const {
    // Compares the working copy with the file as loaded (covers stats, quantities and added
    // entries of every type). Only finalize() touches the counter and checksum.
    return readable() && cur_ != orig_;
}

void Save::set(size_t field, int64_t value) {
    if (!editable() || field >= kFieldCount) return;
    const FieldDef& f = kFields[field];
    value = std::max<int64_t>(f.minV, std::min<int64_t>(f.maxV, value));
    putLe32(cur_.data() + base_ + f.addr, (uint32_t)value);
    if (field >= kFirstStat && field <= kLastStat) syncLevel();
}

void Save::syncLevel() {
    int64_t sum = 0;
    for (size_t i = kFirstStat; i <= kLastStat; i++) sum += readU32(cur_, i);
    const FieldDef& lv = kFields[kFieldLevel];
    int64_t level = std::max<int64_t>(lv.minV, std::min<int64_t>(lv.maxV, sum - levelOffset_));
    putLe32(cur_.data() + base_ + lv.addr, (uint32_t)level);
}

void Save::adjust(size_t field, int64_t delta) { set(field, (int64_t)get(field) + delta); }

void Save::setMax(size_t field) {
    if (field < kFieldCount) set(field, kFields[field].maxV);
}

void Save::revert(size_t field) {
    if (field < kFieldCount) set(field, original(field));
}

const std::vector<uint8_t>& Save::finalize() {
    if (editable()) {
        // Two header counters hold the number of used entries: kInvCounterOff for slots >= 64 and
        // kInvKeyCounterOff for slots < 64 (checked on 9 real character slots). Apply the change
        // in each region to the values the file had, so calling this twice is safe.
        if (invRecognised()) {
            uint32_t cap = le32(orig_.data() + base_ + kInvCapacityOff);
            int64_t delta[2] = {0, 0}; // [0] = slots < 64, [1] = slots >= 64
            for (uint32_t slot = 0; slot < cap; slot++) {
                bool was = le32(orig_.data() + slotOffset(slot)) != 0xFFFFFFFFu;
                bool now = le32(cur_.data() + slotOffset(slot)) != 0xFFFFFFFFu;
                delta[slot >= kEquipRegionStart] += (int)now - (int)was;
            }
            if (delta[1]) putLe32(cur_.data() + base_ + kInvCounterOff, (uint32_t)((int64_t)le32(orig_.data() + base_ + kInvCounterOff) + delta[1]));
            if (delta[0]) putLe32(cur_.data() + base_ + kInvKeyCounterOff, (uint32_t)((int64_t)le32(orig_.data() + base_ + kInvKeyCounterOff) + delta[0]));
        }
        uint8_t digest[16];
        md5(cur_.data() + base_ + kChecksumSize, kHashedSize, digest);
        std::memcpy(cur_.data() + base_, digest, kChecksumSize);
    }
    return cur_;
}

} // namespace dsr
