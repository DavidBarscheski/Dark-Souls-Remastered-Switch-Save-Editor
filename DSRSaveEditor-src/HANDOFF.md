# Handoff notes (read this first in a new session)

Project: Nintendo Switch homebrew (libnx/devkitPro) save editor for Dark Souls Remastered
(title 01004AB00A260000). Edits stats, inventory quantities, adds items, multi-character saves.

Read these files in order: README.md, research/SAVE_FORMAT.md (everything learned about the save
format), include/save.h, source/save.cpp, source/main.cpp, tests/test_core.cpp.

State: all host tests pass. Switch build not compiled in the cloud sandbox (no libnx); the user builds it.
Build the tests: g++ -std=c++17 -O1 -Iinclude -Isource tests/test_core.cpp source/save.cpp source/md5.cpp -o tests/test_core
Regenerate the icon: python3 -I tools/make_icon.py
Regenerate item tables: python3 -I research/gen_addable.py  (reads research/param_items.csv)

Done: Stats, Items, Add screens; key items (slots 0-63) and ammo; multi-character picker;
Debug screen was REMOVED (its items were placeholders); Elite Cleric + Mage Smith armor are hidden
behind ZL+ZR on the Add screen (research/gen_addable.py CUT_ARMOR -> source/cut_data.inc).
Not hardware-verified: weapons, ammo, spells, key items, newer consumables.

Open: the "broken item" icon on equipped added items could not be reproduced by the user (closed
unless it returns). Ideas, each to confirm first: custom items using the spare ids/slots, +N upgraded
gear, Estus Flask. Researching a possible GitHub release: add LICENSE, credit Paramdex (soulsmods),
add an online-ban warning, never commit game dumps or saves.

Key facts: BND4 save, entry data offset = u32 at 0x40+0x20*i+0x10; MD5 over next 0x60000 bytes;
inventory header 0x370/0x374 counters, capacity 0x800 at 0x378, slots at 0x37C stride 28;
handle = (sortKey<<12)|slot, sortKey = sortId (goods, rings) or sortId*100 (weapons, armor).
Param extraction: XCI -> NCA -> RomFS -> dvdbnd1.bhd5 -> gameparam.parambnd.dcx -> BND3 -> .param
(research/extract_params.py, paramlib.py). The user owns the cartridge and dumped it themselves.

Icon: icon.jpg (256x256) is drawn by tools/make_icon.py and picked up by APP_ICON in the Makefile.
