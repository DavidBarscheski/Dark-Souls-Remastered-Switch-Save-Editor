# DSR Save Editor (Nintendo Switch homebrew)

An on-console editor for your **Dark Souls: Remastered** save (title ID `01004AB00A260000`).
Runs as an `.nro` from the Homebrew Menu, reads the game's save data directly, edits values,
fixes the save checksum, and writes it back.

## What it edits

Souls, Level, Vitality, Attunement, Endurance, Strength, Dexterity, Resistance, Intelligence,
Faith, HP, Max HP, Stamina, Max Stamina, for the character you pick. If a save file holds more than one character (some do), the app lists them with level and souls and asks which to edit; the first one is the game's slot 1.

The offsets and checksum scheme come from the community EdiZon config/script for this game
(marked "beta, first slot only" upstream).

**Items screen (press `-`):** change the quantity (1-99) of stacks you **already own** of Humanity
and the upgrade materials (Titanite Shard, Large, Green, Chunk, Blue/White/Red Chunk, Slab,
Blue/White/Red Slab, Dragon Scale, Demon Titanite, Twinkling Titanite). Names and IDs come from a
community item-ID spreadsheet and were checked against a real save (103 of its 104 goods matched).
The inventory layout was worked out from that save: a header (slot size 0x1C, 2048 slots) followed by
a table of slots, each holding a type tag, item id and quantity. Only that quantity is changed; the
app does not create new inventory entries, weapons, armor, or key items, and it offers no items at
all if the inventory header doesn't look exactly as expected.

**Add screen (press `-` twice): EXPERIMENTAL.** Creates a new inventory entry for any regular item
you don't own: 217 goods (materials, consumables, 72 spells, 45 key items such as embers,
Lordvessel and Master Key), 41 rings, 232 armor pieces and 210 weapons/shields including 14 kinds of
ammunition (x999). Everything is the +0 version; upgraded or infused (+N) gear is not offered. Goods
stack to the game's own limit, spells and the rest are x1. The list scrolls; L/R jump 10 entries, X adds,
Y undoes an add made in this session. Anything you already own is left alone. KEY ITEMS can skip story
progress, so only add them if you know what they do. Not offered: Estus Flask (its upgrade path changes the
item's id), gestures and cut or unused rows (the unobtainable Elite Cleric and Mage Smith armor is hidden: press ZL+ZR together on the Add screen to show those 8 pieces at the end of the list, and again to hide them). Inventory space is limited, so a very large batch will stop
with "no free slot".

Every item's menu sort key, stack limit and durability now come from the game's own parameter tables
(`GameParam`, read from a cartridge dump): the sort key stored in an entry's handle is the item's
`sortId` (goods and rings) or `sortId x 100` (weapons and armor). This was checked against 1,700+
entries in six real Switch saves; the only exceptions are items the game created by transforming
another one (infusions, boss-soul weapons, Estus upgrades), which keep the SOURCE item's handle. So
weapons made from a boss soul, such as Greatsword of Artorias, are added with the source weapon's key.
Key items are stored in slots 0-63 with their own counter; the app keeps that counter right. Excluded: Estus
Flask, gestures, cut/unused rows, and infused or upgraded gear. `research/param_items.csv` holds the
decoded tables and `research/gen_addable.py` regenerates the item lists.

What "add" writes, copied from real game behaviour: a type/ID/quantity/handle entry in an empty slot
(the first freed slot after the first weapon/armor entry; if none is left, the slot after the last
used one, which is what the game did when 100 weapons were taken out of the box), plus +1 on the
header entry counter. Slots 0-63 (key items) are never touched. Materials were confirmed to work on a
real console, and a save edited by an earlier build of this app loaded and played normally with all
of its added items intact. Weapons, ammunition, spells, key items and the newly added consumables are the least-tested parts (no real
save shows the game adding a key item, so those use a freed slot, else the slot after the last used one).
Try them offline with a backup first.

Notes:
- **Level follows your stats automatically.** Level = (sum of the eight stats) - 82, which holds for
  every class in the game; the offset is read from your own save. Changing a stat updates Level, and
  reverting the stat restores it. You can still set Level by hand afterwards; the next stat change
  recomputes it.
- Value limits (e.g. stats 1–99, level 1–802, HP up to 9999) are conservative guards chosen for this
  app, not limits copied from the game files.

## Safety design

- **Checksum gate.** On load the app recomputes the save's MD5 and compares it to the stored one.
  If they differ (different game version, unknown layout), the file is shown **read-only** and nothing
  can be written.
- **Backup first.** Before every write, the untouched original is saved to
  `sdmc:/switch/DSRSaveEditor/backups/`. If the backup fails, the save is not written.
- **No truncation.** The file is overwritten in place at its original size, then the save is
  committed and read back and re-verified.
- **Offline only.** Play with an edited save offline. Edited saves can get flagged online, and the
  risk is yours. Keep a backup of your clean save.

## Building

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `switch-dev` group.
2. `export DEVKITPRO=/opt/devkitpro` (path depends on your install), then run `make`.
3. Copy `DSRSaveEditor.nro` to `sdmc:/switch/` on your SD card and launch it from the Homebrew Menu.

If the Makefile misbehaves on your devkitPro version, copy the template at
`$DEVKITPRO/examples/switch/templates/application/Makefile` and keep only the `TARGET`, `SOURCES`,
`INCLUDES` and `APP_*` settings from this one.

## Using it

Close the game completely first, then launch the app (a normal Homebrew Menu launch is fine).

| Button | Action |
| --- | --- |
| Up / Down | Choose field |
| Left / Right | −1 / +1 (hold to repeat) |
| L / R | −10 / +10 |
| ZL / ZR | −1000 / +1000 (stats screen only) |
| X | Set to maximum (99 on the items screen); on the Add screen: add x99 |
| Y | Revert (on the Add screen: undo an add) |
| − | Cycle Stats → Items → Add |
| A | Save (asks to confirm) |
| B or + | Exit (asks if there are unsaved changes) |

## Tests

The save/checksum logic has no Switch dependencies and is tested on a PC:

```
make -C tests
```

This covers MD5 known-answer vectors, edit → re-checksum → reload round trips, value clamping,
tamper detection, malformed headers, and inventory edits (including look-alike entries that must be
left alone). These use synthetic saves. The stats and Titanite Chunk edits were also run on a copy of
one real save, where only the edited quantity and the 16-byte checksum changed.

**Not covered:** the Switch UI/filesystem layer (`source/main.cpp`) has not been compiled or run on
hardware. Run it first against a copy-safe setup and check the SD backup appears before trusting it
with a save you care about.

## Layout

```
include/  md5.h, save.h
source/   main.cpp (UI + FS), save.cpp (format + edits), md5.cpp
tests/    host unit tests
```

## Credits

Save layout and offsets: Jojo's Dark Souls Remastered config/script in
[EdiZon_CheatsConfigsAndScripts](https://github.com/WerWolv/EdiZon_CheatsConfigsAndScripts).
