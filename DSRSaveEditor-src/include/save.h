#pragma once
// Platform-independent model of a Dark Souls Remastered (Switch) save file.
//
// Layout, as documented by the community EdiZon config/script for title
// 01004AB00A260000 (first character slot only):
//   * u32 little-endian at file offset 0x28 = "base"
//   * 16-byte MD5 checksum stored at [base, base+16)
//   * checksum covers the 0x60000 bytes starting at base+16
//   * character fields are u32 little-endian at base + fieldAddress
//
// Nothing here touches the Switch, so it can be unit-tested on a PC.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dsr {

struct FieldDef {
    const char* name;
    uint32_t addr; // offset from `base`
    uint32_t minV;
    uint32_t maxV;
};

extern const FieldDef kFields[];
extern const size_t kFieldCount;

// Indices into kFields. Level follows the eight stats (Vitality..Faith).
constexpr size_t kFieldLevel = 1;
constexpr size_t kFirstStat = 2;
constexpr size_t kLastStat = 9;

constexpr uint32_t kBasePtrOffset = 0x28;
constexpr uint32_t kChecksumSize = 16;
constexpr uint32_t kHashedSize = 0x60000;

// Inventory (offsets relative to `base`, like the character fields). Found by studying a real
// save: a header with the slot size and slot count, then a table of fixed-size slots.
//   slot: u32 type tag, u32 item id, u32 quantity, u32 handle, u32 flag, u32, u32
//   unused slots have type == id == 0xFFFFFFFF
constexpr uint32_t kInvEntrySizeOff = 0x374; // old name: this is really the key-item entry count, NOT a size
constexpr uint32_t kInvCapacityOff = 0x378;  // u32, expected 0x800
constexpr uint32_t kInvArrayOff = 0x37C;
constexpr uint32_t kInvEntrySize = 0x1C;
constexpr uint32_t kInvCapacity = 0x800;
constexpr uint32_t kGoodsTag = 0x40000000;
constexpr uint32_t kMaxStack = 99; // largest stack limit of any item the app offers

// Header counter that the game raises by one for every new inventory entry it creates.
constexpr uint32_t kInvCounterOff = 0x370;    // used entries in slots >= kEquipRegionStart
constexpr uint32_t kInvKeyCounterOff = 0x374; // used entries in slots < kEquipRegionStart (key items)
constexpr uint32_t kEquipRegionStart = 64;

// Only these goods can be edited, and only if you already own a stack. Raising the quantity
// of arbitrary items could exceed their real limits, so the list is deliberately short.
struct ItemDef {
    uint32_t id;
    const char* name;
    uint32_t maxNum; // the game's stack limit for this item
};
extern const ItemDef kEditableItems[];
extern const size_t kEditableItemCount;

// Items that can be ADDED as brand-new stacks. Each needs its menu sort key, which the game
// stores in the upper bits of the entry's handle (handle = sortKey << 12 | slot number). The
// keys cannot be derived from the ID; these were read from real saves (see source for details).
struct AddableDef {
    uint32_t tag;        // kGoodsTag, 0x20000000 ring, 0x10000000 armor
    uint32_t id;
    const char* name;
    uint32_t sortKey;
    uint32_t durability; // armor/weapons: fresh (maximum) durability; otherwise 0
    uint32_t maxNum;     // stack limit; 1 for everything except stackable goods and ammunition
    uint32_t keyItem;    // 1: stored in slots 0-63 (key items), which have their own counter
};
extern const AddableDef kAddableItems[];
extern const size_t kAddableCount;       // everything, including the hidden cut items at the end
extern const size_t kNormalAddableCount; // [0, this) is the normal Add list       

enum class AddResult {
    Added,       // a new inventory entry was created
    SetExisting, // you already own a stack, so its quantity was set instead
    NoFreeSlot,  // no suitable empty slot was found
    AlreadyOwned, // ring/armor you already have; nothing changed
    NotAllowed,  // file is read-only or the inventory layout is not recognised
};

enum class LoadStatus {
    Ok,               // layout looks right and the stored checksum matches
    TooSmall,         // file too short to contain the expected structure
    BadBase,          // base pointer points outside the file
    ChecksumMismatch, // layout parsed, but stored checksum != computed one
};

const char* statusText(LoadStatus s);

struct CharacterInfo {
    int entry;           // index of the BND4 entry
    uint32_t base;       // pass to Save::load
    uint32_t level, souls;
    bool isFileDefault;  // the entry the file's base pointer names (character slot 1)
};

class Save {
public:
    // `forcedBase` selects a character other than the file's default (see listCharacters).
    LoadStatus load(std::vector<uint8_t> bytes, int64_t forcedBase = -1);
    uint32_t base() const { return base_; }
    static std::vector<CharacterInfo> listCharacters(const std::vector<uint8_t>& bytes);

    LoadStatus status() const { return status_; }
    // Edits are only allowed when the checksum verified, which is strong
    // evidence that our understanding of the layout matches this file.
    bool editable() const { return status_ == LoadStatus::Ok; }
    // Values can be shown whenever the base pointer was usable.
    bool readable() const { return status_ == LoadStatus::Ok || status_ == LoadStatus::ChecksumMismatch; }

    uint32_t get(size_t field) const;
    uint32_t original(size_t field) const;
    bool changed(size_t field) const { return get(field) != original(field); }
    bool dirty() const;

    void set(size_t field, int64_t value); // clamped to the field's range
    void adjust(size_t field, int64_t delta);
    void setMax(size_t field);
    void revert(size_t field);

    // Existing stacks of the editable items listed above (empty if the inventory header
    // is not recognised). Quantities are clamped to 1..kMaxStack.
    size_t itemCount() const { return items_.size(); }
    const char* itemName(size_t i) const;
    uint32_t itemQty(size_t i) const;
    uint32_t itemOriginalQty(size_t i) const;
    bool itemChanged(size_t i) const { return itemQty(i) != itemOriginalQty(i); }
    void setItemQty(size_t i, int64_t value);
    void adjustItemQty(size_t i, int64_t delta);
    void setItemMax(size_t i);
    uint32_t itemMaxQty(size_t i) const; // the game's stack limit for item i
    // For an item added in this session this removes the new entry again.
    void revertItem(size_t i);
    // True if the stack did not exist in the file as loaded (it was added in this session).
    bool itemIsNew(size_t i) const;

    // Adds a stack of kAddableItems[addable] with `qty` (clamped to 1..kMaxStack). If you already
    // own the item its quantity is set instead. `itemIndex` receives the stack's index for the
    // item accessors above.
    AddResult addItem(size_t addable, uint32_t qty, size_t* itemIndex = nullptr);
    // Index of the owned stack for kAddableItems[addable], or itemCount() if none.
    size_t findOwned(size_t addable) const;
    // True if the inventory already has this addable item (any type: goods, ring or armor).
    bool owns(size_t addable) const;
    // Undoes an add made in this session (any type). Returns false if that item was not added now.
    bool revertAdded(size_t addable);

    // Level = sum of the eight stats - levelOffset(). The offset is read from the loaded file
    // (it is 82 in every normal save); if that looks implausible, 82 is used. Changing any stat
    // updates Level automatically; Level can still be set directly afterwards.
    int levelOffset() const { return levelOffset_; }

    // Recomputes the checksum and returns the bytes to write back to disk.
    const std::vector<uint8_t>& finalize();
    const std::vector<uint8_t>& originalBytes() const { return orig_; }

    // True if `bytes` has a valid checksum under the layout above.
    static bool checksumValid(const std::vector<uint8_t>& bytes, int64_t forcedBase = -1);

private:
    uint32_t readU32(const std::vector<uint8_t>& v, size_t field) const;
    void indexItems(); // rebuilds items_ from the CURRENT contents
    size_t slotOffset(uint32_t slot) const { return (size_t)base_ + kInvArrayOff + (size_t)slot * kInvEntrySize; }
    size_t itemQtyOffset(size_t i) const { return slotOffset(items_[i].slot) + 8; }
    bool ownsEntry(uint32_t tag, uint32_t id) const;
    bool invRecognised() const; // inventory header looks like the one we studied
    int pickFreeSlot(bool keyItem = false) const;   // slot for a new entry, or -1

    struct ItemSlot {
        uint32_t slot;
        const ItemDef* def;
    };
    std::vector<ItemSlot> items_;

    void syncLevel();
    int levelOffset_ = 82;

    LoadStatus status_ = LoadStatus::TooSmall;
    uint32_t base_ = 0;
    std::vector<uint8_t> orig_;
    std::vector<uint8_t> cur_;
};

} // namespace dsr
