# Dark Souls Remastered (Switch) save format — working notes

Everything here was worked out from real saves (three Switch backups of one character, an Xbox 360
package) plus public references. "Verified" means checked against real data; anything else is
inference. Offsets are relative to `base` (the character slot's start) unless stated.

## Container and checksum (verified)
- File: `DRAKS0005.sl2`. `u32 LE` at file offset `0x28` = `base` (0x2C0 in the studied file).
- `[base, base+16)` = MD5 of the next `0x60000` bytes (`base+16 .. base+16+0x60000`). Recomputing
  it over an unedited file reproduces the stored value exactly.
- Only slot 1 is mapped. Other slots were not examined on Switch.

## Character fields (u32 LE; offsets from the community EdiZon config, verified by reading values)
Souls 0xF0, Level 0xEC, Vitality 0x9C, Attunement 0xA4, Endurance 0xAC, Strength 0xB4,
Dexterity 0xBC, Resistance 0xE4, Intelligence 0xC4, Faith 0xCC, HP 0x70, Max HP 0x78,
Stamina 0x8C, Max Stamina 0x94. Level = (sum of the eight stats) − class offset (82 for a
Warrior start: 176 − 94). A souls edit (6,801 → 6,900) was accepted by the game.

## Inventory (verified)
- `base+0x370` u32: counter, raised by 1 every time the game creates a new entry
  (0xFF → 0x100 → 0x101 across the captures).
- `base+0x374` u32: entry size, 0x1C. `base+0x378` u32: capacity, 0x800 entries.
- Entries start at `base+0x37C`, 28 bytes each, seven u32 LE fields:

| # | field | notes |
|---|---|---|
| 0 | type tag | `0x00000000` weapons/arrows/bolts, `0x10000000` armor, `0x20000000` rings, `0x40000000` goods (consumables, materials, keys, spells), `0xFFFFFFFF` empty. A parser's notes also list `0x30000000` as unknown. |
| 1 | item id | decimal "param row" id |
| 2 | quantity | goods stack, 1..99 for materials; 1 for gear |
| 3 | handle | `(sortKey << 12) | slot index at creation`; see below |
| 4 | flag | 1 on every real entry |
| 5 | durability | 0 for goods |
| 6 | durability hits | 0..9, then durability drops by one (per the dsfp parser's notes) |

- Weapon and armor ids carry their upgrade level in the last two digits (id = base + level; the
  game's param tables only define rows at multiples of 100). A weapon seen going from 1453000+0 to
  1453000+5 matches a +5 upgrade.
- Empty slots: "freed" ones keep a handle whose low 12 bits are their own index and stale
  durability values; "never used" ones have handle `0xFFFFFFFF`.
- **Sort key (handle >> 12)** is static per item (105 of 105 goods identical across three saves) and is
  not derivable from the id. Materials run 1001..1014. Between the Xbox 360 and Switch versions:
  armor 65/65 identical, weapons 6/6, materials 7/7, but only 33/69 consumables and 8/14 rings.
- **Adding an item, as the game does it (2 real captures: Titanite Shard, War Pick):** write a freed
  slot (first one after the first weapon/armor entry), set type/id/qty, handle = key<<12 | slot,
  flag 1, durability fields, and add 1 to the counter. Replaying the Titanite Shard capture with
  this code reproduced the game's slot and counter exactly, and the editor's "Add" screen for
  upgrade materials was confirmed working on a real Switch.
- Not understood: why the game chose slots 211/212 over lower freed slots (rule used is a fit to two
  observations); the many other bytes that change between saves (world/event state, timers).

## Xbox 360 package (original Dark Souls) — extracted and read
- STFS `CON ` package. File table is at data block 1; block `n` is at file offset
  `0xA000 + 0x1000 * (n + 1 + n // 170)`. Entries are 0x40 bytes (name, block count at +0x29,
  start block at +0x2F, size big-endian at +0x34).
- 11 files `USER_DATA` .. `USER_DATA0a`, 0x60000 bytes each, **big-endian**, no MD5 header.
  `USER_DATA01` is a character slot with the same 28-byte inventory (array preceded by three words:
  counter, 8, 0x800). Slots 02/04/0a contain goods-like rows but their table was not located.
- `USER_DATA03` contains Havok animation data, not a character.

## Item universes (row ids from a Cheat Engine table's in-memory param lists)
Goods 272 rows, weapons 1,245, armor 324, rings (accessories) 41. Names come from a community
item-ID spreadsheet (goods 247/272, weapons 1,197/1,245, armor 290/324, rings 41/41 named).
`items.csv` combines these with every sort key found, labelled by trust:
`high` = read from a Switch save; `medium` = Xbox value in a category that matched 100% (armor,
materials); `low` = Xbox weapon (only 6 overlaps); `unreliable` = Xbox consumable/ring (does not
match Switch); blank = unknown.

## Still missing / how to get it
1. Sort keys for consumables, rings, spells and most weapons on Switch: hold one of each item and
   send a save. Keys are permanent, so each save adds coverage.
2. Per-item stack limits (`maxNum`) and durabilities live in the game's encrypted
   `regulation.regbnd.dcx.enc` (present in the save backup, not readable here).
3. Armor/weapon "add": needs per-item max durability and a decoded durability-hits convention.

## Sources
- Community Dark Souls item-ID spreadsheet (provided by the user).
- EdiZon_CheatsConfigsAndScripts, Dark Souls Remastered config (stat offsets, checksum).
- dsfp (Darksouls Save File Parser) docs: inventory field meanings, BND4 slot structure.
- Souls Modding Wiki, EquipParamGoods (sortId, maxNum field definitions) and EquipParamWeapon.
- JohrnaJohrna/RemasterCETable (row-id lists for the param tables).

## PC save findings (Version 3 "all items" save, 10 identical slots, 640 items)
- PC `DRAKS0005.sl2` is BND4, each slot AES-128-CBC (key 0123456789ABCDEFFEDCBA9876543210, IV = first
  16 bytes of the entry). Decrypted plaintext has a 4-byte length prefix. Past body offset 0x1C0 the
  Switch body sits 16 bytes later than PC.
- Same 28-byte inventory entries. Sort keys agree PC vs Switch: armor 123/123, rings 20/20,
  weapons 10/10, materials all; consumables/Humanity differ (52/79) -> not trusted.
- Durability (field 6) of weapons/armor in the PC save equals the Switch value for every shared item
  (armor 128/128, weapons 11/11): it is the item's fresh/max durability. `items.csv` now has
  `pc_key`, `pc_durability`, `pc_qty_seen` columns.
- Caveat: modded third-party save; quantities (goods 1/5/10/99, weapons 999) are not game limits.

## Add-item coverage (current build)
`source/addable_data.inc` is generated from `items.csv`: 27 rings (Switch key where known, else PC) and 234
base armor pieces (key from Switch where present, else PC; durability from PC). Ring/armor entries are
quantity 1, flag 1, durability field = fresh durability (0 for rings), field 7 = 0. Not hardware-tested.

## Second Switch save (found online; level 216, 189 items) - findings
- Same BND4 layout, MD5 verified on slot 1 (slot 11 is system data and has no character).
- **The u32 at base+0x374 is not an entry size.** It is 0x1C in the first three saves, 0x14 here, 0x15 in
  the PC save, yet the real stride is 0x1C in all of them (handle low 12 bits == slot for 189/189 here).
  The app previously refused any save where it wasn't 0x1C; it now ignores it and instead requires
  capacity 0x800 and a sane table (every non-empty entry has a valid type tag). Handle low bits == slot
  is NOT a safe check: 49 entries in the first Switch saves don't follow it.
- Sort keys: 29 goods, 40 weapons, 47 armor and 16 rings agreed with earlier Switch/PC data (no
  conflicts with earlier Switch keys). 36 new keys (26 goods, 10 weapons) and 21 goods that disagreed
  with PC (confirming PC goods keys are wrong for Switch) were added to items.csv as `switch2`/high.
  8 of the weapon rows were matched through the base id (upgraded weapon in the save).
- Slot layout: goods live in slots 0-63 (16-23 partly freed, 24-63 never used), equipment starts at
  slot 64, and slots 233+ are never used. This save has NO freed slot after the first equip entry, so the
  "first freed slot after first equip" rule finds nothing here. Where the game would put a new item in
  such a save is unobserved (the earlier saves put new pickups in freed slots ~211-212 even though
  freed slots existed at 18, 22 and 30), so the app reports "no free slot" instead of guessing.

## Consumables added to the Add screen
24 goods (incl. Humanity 500, which was already editable) whose Switch key is confirmed (`switch`/`switch2`,
high) AND that were seen at qty 99 in a real Switch save: souls 400-408, Eye of Death, Purple Moss,
Firebomb, Throwing Knife, Dung Pie, Alluring Skull, Transient Curse, Homeward Bone, Prism Stone, boss
souls 700/701/703/704, Twin Humanities. The other ~100 goods with confirmed keys (spells, keys, tools,
soapstones, etc.) need their per-item max stack, which lives in the unreadable regulation file.
Test: adding every addable item to a real save fills freed slots until none remain, then reports
no-free-slot; the result verifies, reloads, and only slot bytes, counter and checksum change.

## Third Switch save (level 500, 9,999,999 souls, 169 items)
- BND4 layout as before, all MD5s valid, but the character is in the SECOND user-data entry (file
  offset 0x602D0); the first entry is empty. The app only edits the entry the file's base pointer
  (0x28) names, so it cannot edit this save yet (a character-slot picker would fix that).
- 95 keys checked against earlier Switch keys: 0 disagreements (goods 49, weapons 25 via base id,
  armor 11, rings 9). Weapon/armor keys are the same for upgraded variants (+N) as for the base item.
- New keys: 26 goods, 26 weapon bases, 4 rings (now 31 rings addable). 7 more consumables seen at qty 99
  were added to the Add screen (Black Firebomb, Copper Coin, Gold/Charcoal/Rotten Pine Resin, Repair
  Powder, Poison Throwing Knife). Spells (qty 99 there, meaning unclear) and Estus Flask+N were NOT added.
- Few freed slots after the first equip entry here, so adds mostly report "no free slot".

## All character slots scanned (every Switch save) - findings
- Your own save has a SECOND character (BND4 entry 1: level 710, 18 items, souls 0); sw3 holds its only
  character in entry 1; sw2 and the PC saves use entry 0. A file's base pointer (0x28) names entry 0 even
  when the character is elsewhere. `Save::listCharacters` now finds every entry with capacity 0x800 and a
  valid MD5, and the app asks which to edit.
- **Header counters (verified on 9 character slots + the PC save):** base+0x370 = number of used entries
  in slots >= 64; base+0x374 = number of used entries in slots < 64. The latter was previously
  mistaken for an "entry size" (it varies: 28, 4, 20, 16, 21). Slots 0-63 hold key items only (2xxx keys,
  smithboxes, embers 802/813, Peculiar Doll 384); everything else lives in slots 64+. The add code now
  never uses slots < 64 and updates the right counter.
- Durability: for gear the u32 at +20 is CURRENT durability and +24 counts hits (e.g. 199 with 3 hits on
  fresh-looking armor). PC values equal the maximum, so they are right for newly added items.
- New key: Estus Flask (id 201) = 2 (not offered: it is a special item).

## Fourth Switch save (user's own character after editing + playing + taking weapons out of the box)
- **The game accepts editor-added entries.** The character was edited with the earlier build (hundreds of
  materials/consumables/rings/armor added into freed slots 211+), then played (level 94 -> 111). Every
  added entry is still there with its handle, and the game even raised quantities/upgraded gear normally.
- **Slot choice with no freed slot:** the ~100 weapons taken out of the box were put in consecutive
  never-used slots 387..486, starting at (last used slot + 1). The add code now does the same when no
  freed slot remains after the first equip entry. (Freed slots below slot 64 are left alone: key items.)
- Weapons from the box keep their original handles (low 12 bits are the slot they were created in, not
  where they are now), and the list is not sorted by key.
- Weapon keys are per infusion row (id - id%100) and constant across reinforcement; 128 weapon rows now
  have Switch keys (87 agreed, 41 new, 0 conflicts) and 139/139 agree with the PC save (weapons+rings).
  Greatsword of Artorias (307000) showed the same key as Broken Straight Sword (21100) in this save,
  which looks like an id edit by the user, so it was left on its Xbox key and is not offered.
- A bug in the earlier merge left ring/weapon PC columns empty (category name mismatch); fixed. That
  surfaced 10 more PC-only ring keys (41 rings now addable) and PC durability for weapons.
- `items.csv` gained `sw_durability` (max seen in Switch saves). `research/gen_addable.py` regenerates
  `source/addable_data.inc` (rings, armor, weapons) from the CSV.

## Boss-soul transformations keep the old handle (from the user)
Upgrading Broken Straight Sword and imbuing it with a boss soul turns it into Greatsword of Artorias (a few
other boss-soul weapons work the same way). The entry's id changes but its handle - and so its sort key -
stays the source weapon's. So a key read from a transformed weapon is NOT that item's own key: Greatsword of
Artorias (307000) showed Broken Straight Sword's 21100. Keys are therefore only trusted when the PC save or a
second save (where the item was picked up normally) agrees. Weapons seen only in the 4th save (Crystal
Straight Sword, Demon's Spear - possibly a boss-soul weapon too, Lucerne) are withheld via UNCORROBORATED in
gen_addable.py. Weapons from your 4th save that also appear in older saves (Skull Lantern, Wooden Shield) keep
their keys. Greatsword of Artorias' real key is still unknown.

## Game parameter tables decoded (supersedes the "key unknown" notes above)
Source: `GameParam` from the user's own cartridge: `dvdbnd1.bhd5` (BHD5, index offset at 0x20, 16-byte
buckets, 16-byte entries {u32 hash, u32 size, u64 offset}) -> hash of `/param/gameparam/gameparam.parambnd.dcx`
(x37 string hash, lowercase) -> 498,591 bytes at 95,082,640 in `dvdbnd1.bdt` -> DCX (zlib) -> BND3 (28-byte
entries) -> 41 `.param` files, parsed with the Paramdex DS1R `Defs`. `research/extract_params.py`,
`paramlib.py` and `param_items.csv` reproduce this.
- **Sort key = the item's `sortId`** (goods, rings) or **`sortId x 100`** (weapons, armor). Confirmed for
  every Switch-confirmed key except items the game transformed, which keep the source's handle
  (Estus Flask+N keep Estus's key 2; infused weapons keep the base weapon's; Greatsword of Artorias had
  Broken Straight Sword's 21100 although its own sortId gives 38900). Param rows with `<-source>` in the
  Japanese name are exactly the boss-soul/infusion products (206 weapon rows).
- `goodsType`: 0 normal, 1 key item (always slots 0-63), 2 material, 4 Humanity fragment, 5 spell.
  `maxNum` is the stack limit (99, 20 for Estus, 1). Owned spells are stored with quantity 1.
- Weapon/armor `durability` = `durabilityMax` = the value in fresh items (PC and Switch saves agree).
- Rows count: goods 272, weapons 1245, armor 324, rings 41; every row has a unique weapon sortId.

## Everything but +N gear (latest build)
Added on top of the param-driven tables: key items (goodsType 1, 45 non-[Unused] rows; same 28-byte entry,
key = sortId, qty 1, flag 1, stored in slots 0-63 and counted in base+0x374), maxNum-1 goods (soapstones,
Darksign, Fire Keeper Souls, carvings...) and ammunition (weapon-category 13/14 rows: qty 999 as seen in two
Switch saves and the PC save, durability 0, key = sortId*100). Estus Flask (ids 200-215), gestures (sortId -1)
and `[Unused]` rows are excluded. Adding every addable item to each real character slot verifies and reloads,
and both header counters equal the real entry counts in both regions.

## Debug screen (REMOVED; kept as history)
`research/gen_addable.py` also writes `source/debug_data.inc` (+ `debug_count.h`): the cut/unused rows left
out of the normal list - blank goods 130-145, `[Unused]` goods/key items, Humanity 350, `[Ghost]` weapons and `Unused - Design Content` shields. They share
the add code (same entry format, key = sortId or sortId*100) and appear on a fourth screen. Whether the game
accepts them is unknown.

The `[Unused]` armor sets (28 rows) have no sortId/durability in the tables, so they are excluded entirely.

## Debug-screen results (user test, Switch hardware)
- The unused weapons/shields from the Debug screen equip fine but are placeholders: no stats or textures
  (design-contest shields look identical to existing shields). So the game accepts entries for those param
  rows; they are usable ids/keys/slots for custom items, not hidden content.
- Open issue: items added through the normal Add screen (equipped) show the low/broken-durability icon
  under the stamina bar while the stats screen shows durability normally. Genuine entries in the user's
  own save have f5 (+20) == param durability == durabilityMax and f6 (+24) = hit counter (0-9, durability
  falls slowly), flag (+16) == 1, handle key == sortId (x100 for gear) - which is exactly what the editor
  writes. Cause not found yet; needs a before/after save with an equipped added item.
