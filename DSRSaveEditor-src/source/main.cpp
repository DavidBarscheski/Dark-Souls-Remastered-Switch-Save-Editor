// Dark Souls Remastered save editor for Nintendo Switch homebrew (libnx).
//
// Flow: warning -> pick user -> mount the game's save -> verify -> edit -> back up -> write -> commit.
#include <switch.h>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "save.h"

namespace {

constexpr u64 kDsrTitleId = 0x01004AB00A260000ULL;
constexpr const char* kMount = "save";
constexpr const char* kAppDir = "sdmc:/switch/DSRSaveEditor";
constexpr const char* kBackupDir = "sdmc:/switch/DSRSaveEditor/backups";

PadState g_pad;

// ---------------------------------------------------------------- input / screen

u64 pollDown() {
    padUpdate(&g_pad);
    return padGetButtonsDown(&g_pad);
}

// Blocks until one of `mask` is pressed. Returns false if the app is asked to close.
bool waitFor(u64 mask, u64* pressed = nullptr) {
    while (appletMainLoop()) {
        u64 d = pollDown();
        if (d & mask) {
            if (pressed) *pressed = d & mask;
            return true;
        }
        consoleUpdate(nullptr);
    }
    return false;
}

void fatalScreen(const char* title, const char* detail) {
    consoleClear();
    std::printf("\x1b[31m%s\x1b[0m\n\n%s\n\nPress + to exit.\n", title, detail);
    consoleUpdate(nullptr);
    waitFor(HidNpadButton_Plus);
}

// ---------------------------------------------------------------- files

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)sz);
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Overwrites an existing file in place without truncating it first, so the file
// never exists in a half-written, shorter state.
bool overwriteInPlace(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) return false;
    size_t put = std::fwrite(data.data(), 1, data.size(), f);
    bool ok = (put == data.size()) && (std::fflush(f) == 0);
    std::fclose(f);
    return ok;
}

bool writeNewFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t put = std::fwrite(data.data(), 1, data.size(), f);
    bool ok = (put == data.size()) && (std::fflush(f) == 0);
    std::fclose(f);
    return ok;
}

struct Entry {
    std::string path; // relative to save:/
    long size;
    bool isDir;
};

// Lists everything on the mounted save, a few levels deep.
void scanDir(const std::string& rel, int depth, std::vector<Entry>& out) {
    std::vector<std::string> subdirs;
    DIR* d = opendir(("save:/" + rel).c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string r = rel.empty() ? n : rel + "/" + n;
        bool isDir = (e->d_type == DT_DIR);
        long size = 0;
        if (!isDir) {
            struct stat st;
            if (stat(("save:/" + r).c_str(), &st) == 0) size = (long)st.st_size;
        }
        out.push_back({r, size, isDir});
        if (isDir) subdirs.push_back(r);
    }
    closedir(d);
    if (depth < 3)
        for (const std::string& s : subdirs) scanDir(s, depth + 1, out);
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Finds the DRAKS*.sl2 save file anywhere on the mounted save (case-insensitive).
// Returns its path relative to save:/ or "" if none; `all` receives the full listing.
std::string findSaveFile(std::vector<Entry>& all) {
    all.clear();
    scanDir("", 0, all);
    std::vector<std::string> hits;
    for (const Entry& e : all) {
        if (e.isDir) continue;
        size_t slash = e.path.find_last_of('/');
        std::string base = lower(slash == std::string::npos ? e.path : e.path.substr(slash + 1));
        if (base.size() > 9 && base.compare(0, 5, "draks") == 0 && base.compare(base.size() - 4, 4, ".sl2") == 0)
            hits.push_back(e.path);
    }
    if (hits.empty()) return "";
    std::sort(hits.begin(), hits.end());
    return hits.front();
}

std::string describeListing(const std::vector<Entry>& all) {
    if (all.empty()) return "(the save is empty)";
    std::string s;
    size_t shown = 0;
    for (const Entry& e : all) {
        if (shown >= 24) break;
        char line[200];
        if (e.isDir)
            std::snprintf(line, sizeof(line), "  %s/\n", e.path.c_str());
        else
            std::snprintf(line, sizeof(line), "  %s  (%ld bytes)\n", e.path.c_str(), e.size);
        s += line;
        shown++;
    }
    if (all.size() > shown) s += "  ... and " + std::to_string(all.size() - shown) + " more\n";
    return s;
}

std::string backupPath(const std::string& savePath) {
    size_t slash = savePath.find_last_of('/');
    std::string saveName = (slash == std::string::npos) ? savePath : savePath.substr(slash + 1);
    char stamp[32];
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    if (lt)
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", lt);
    else
        std::snprintf(stamp, sizeof(stamp), "t%lld", (long long)t);
    return std::string(kBackupDir) + "/" + saveName + "." + stamp + ".bak";
}

// ---------------------------------------------------------------- accounts

std::string userName(AccountUid uid) {
    std::string name = "(unknown)";
    AccountProfile prof;
    if (R_SUCCEEDED(accountGetProfile(&prof, uid))) {
        AccountProfileBase base;
        std::memset(&base, 0, sizeof(base));
        if (R_SUCCEEDED(accountProfileGet(&prof, nullptr, &base))) name = base.nickname;
        accountProfileClose(&prof);
    }
    return name;
}

// Returns false if the user quit.
bool pickUser(AccountUid& chosen, std::string& name) {
    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 total = 0;
    Result rc = accountListAllUsers(uids, ACC_USER_LIST_SIZE, &total);
    if (R_FAILED(rc) || total <= 0) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Could not list user profiles (rc=0x%X).", rc);
        fatalScreen("No user profiles", msg);
        return false;
    }

    std::vector<std::string> names;
    for (s32 i = 0; i < total; i++) names.push_back(userName(uids[i]));

    if (total == 1) {
        chosen = uids[0];
        name = names[0];
        return true;
    }

    int sel = 0;
    bool redraw = true;
    while (appletMainLoop()) {
        if (redraw) {
            consoleClear();
            std::printf("Select the user whose Dark Souls save to edit:\n\n");
            for (int i = 0; i < total; i++) std::printf(" %s %s\n", i == sel ? ">" : " ", names[i].c_str());
            std::printf("\nUp/Down: choose   A: select   +: exit\n");
            redraw = false;
        }
        u64 d = pollDown();
        if (d & HidNpadButton_Plus) return false;
        if (d & HidNpadButton_Up) { sel = (sel + total - 1) % total; redraw = true; }
        if (d & HidNpadButton_Down) { sel = (sel + 1) % total; redraw = true; }
        if (d & HidNpadButton_A) {
            chosen = uids[sel];
            name = names[sel];
            return true;
        }
        consoleUpdate(nullptr);
    }
    return false;
}

// ---------------------------------------------------------------- editor UI

enum class Mode { Edit, ConfirmSave, ConfirmQuit };
enum class Screen { Stats, Items, Add };

static inline bool isAddScreen(Screen s) { return s == Screen::Add; }
static inline int addOffset(Screen) { return 0; }
static bool g_showCut = false; // ZL+ZR on the Add screen reveals the unobtainable (cut) armor
static inline int addTotal(Screen) { return (int)(g_showCut ? dsr::kAddableCount : dsr::kNormalAddableCount); }

void fmtNumber(char* out, size_t n, uint32_t v) {
    // 1234567 -> "1,234,567"
    char raw[16];
    std::snprintf(raw, sizeof(raw), "%u", v);
    std::string s = raw, r;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        r.insert(r.begin(), s[i]);
        if (++c % 3 == 0 && i > 0) r.insert(r.begin(), ',');
    }
    std::snprintf(out, n, "%s", r.c_str());
}

void draw(const dsr::Save& save, const std::string& user, const std::string& file, Screen screen, int sel,
          Mode mode, const std::string& message) {
    consoleClear();
    std::printf("Dark Souls Remastered Save Editor\n");
    std::printf("User: %s   File: %s   Character: level %u (entry at 0x%X)\n", user.c_str(), file.c_str(), save.get(dsr::kFieldLevel), (unsigned)save.base());
    std::printf("Status: %s%s\x1b[0m\n", save.editable() ? "\x1b[32m" : "\x1b[31m", dsr::statusText(save.status()));
    std::printf("Screen: %s %s %s   (press - to switch)\n\n", screen == Screen::Stats ? "[Stats]" : " Stats ",
                screen == Screen::Items ? "[Items]" : " Items ", screen == Screen::Add ? "[Add]" : " Add ");

    if (screen == Screen::Stats) {
        for (size_t i = 0; i < dsr::kFieldCount; i++) {
            char cur[24], orig[24];
            fmtNumber(cur, sizeof(cur), save.get(i));
            fmtNumber(orig, sizeof(orig), save.original(i));
            if (save.changed(i))
                std::printf(" %s %-14s \x1b[33m%13s\x1b[0m  (was %s)\n", (int)i == sel ? ">" : " ", dsr::kFields[i].name, cur, orig);
            else
                std::printf(" %s %-14s %13s\n", (int)i == sel ? ">" : " ", dsr::kFields[i].name, cur);
        }
        std::printf("\n Level follows your stats automatically (stats total - %d).\n", save.levelOffset());
    } else if (isAddScreen(screen)) {
        const int total = addTotal(screen), win = 24, off = addOffset(screen);
        if (g_showCut) std::printf(" \x1b[33mCUT ITEMS SHOWN at the end of the list - unobtainable in the real game, untested.\x1b[0m\n");
        int first = sel - win / 2;
        if (first > total - win) first = total - win;
        if (first < 0) first = 0;
        int last = first + win < total ? first + win : total;
        std::printf(" %s (%d/%d)\n", first > 0 ? "^ more above" : "", sel + 1, total);
        for (int i = first; i < last; i++) {
            const dsr::AddableDef& d = dsr::kAddableItems[off + i];
            const char* kind = d.tag == dsr::kGoodsTag ? (d.keyItem ? "KEY ITEM" : "Item") : d.tag == 0x20000000u ? "Ring" : d.tag == 0x10000000u ? "Armor" : d.maxNum > 1 ? "Ammo" : "Weapon";
            size_t owned = save.findOwned((size_t)(off + i));
            char status[40];
            if (d.tag != dsr::kGoodsTag)
                std::snprintf(status, sizeof(status), "%s", save.owns((size_t)(off + i)) ? "owned" : "not owned");
            else if (owned >= save.itemCount())
                std::snprintf(status, sizeof(status), "not owned");
            else
                std::snprintf(status, sizeof(status), "owned x%u%s", save.itemQty(owned), save.itemIsNew(owned) ? "  (new)" : "");
            std::printf(" %s %-8s %-30s #%-5u %s\n", i == sel ? ">" : " ", kind, d.name, (unsigned)d.id, status);
        }
        std::printf(" %s\n", last < total ? "v more below" : "");
        std::printf(" X adds the item (stacks to the game limit; others x1, all +0). KEY ITEMS can skip story\n"
                    " progress (e.g. Lordvessel). Experimental: play offline, keep your backup. L/R jump 10.\n");
    } else if (save.itemCount() == 0) {
        std::printf(" No supported items found in this inventory.\n");
    } else {
        const int total = (int)save.itemCount(), win = 24;
        int first = sel - win / 2;
        if (first > total - win) first = total - win;
        if (first < 0) first = 0;
        int last = first + win < total ? first + win : total;
        std::printf(" %s (%d/%d)\n", first > 0 ? "^ more above" : "", sel + 1, total);
        for (int i = first; i < last; i++) {
            size_t k = (size_t)i;
            if (save.itemChanged(k))
                std::printf(" %s %-32s \x1b[33m%5u\x1b[0m / %-3u (was %u)\n", i == sel ? ">" : " ", save.itemName(k), save.itemQty(k), save.itemMaxQty(k), save.itemOriginalQty(k));
            else
                std::printf(" %s %-32s %5u / %-3u\n", i == sel ? ">" : " ", save.itemName(k), save.itemQty(k), save.itemMaxQty(k));
        }
        std::printf(" %s\n", last < total ? "v more below" : "");
        std::printf("\n Quantities are limited to each item's in-game stack size. To get items you don't own, use the Add screen.\n");
    }

    std::printf("\n");
    if (mode == Mode::ConfirmSave) {
        std::printf("\x1b[33mWrite changes to the Switch save?\x1b[0m  A: yes   B: cancel\n"
                    "(a backup is written to the SD card first)\n");
    } else if (mode == Mode::ConfirmQuit) {
        std::printf("\x1b[33mDiscard unsaved changes and exit?\x1b[0m  A: yes   B: cancel\n");
    } else {
        if (screen == Screen::Stats)
            std::printf("Up/Down: field    Left/Right: -/+1    L/R: -/+10    ZL/ZR: -/+1000\n"
                        "X: set to max     Y: revert field     A: save   -: next   B or +: exit\n");
        else if (screen == Screen::Items)
            std::printf("Up/Down: item     Left/Right: -/+1    L/R: -/+10\n"
                        "X: set to 99      Y: revert item      A: save   -: next   B or +: exit\n");
        else
            std::printf("Up/Down: item\n"
                        "X: add the item   Y: undo an add   A: save   -: next   B or +: exit\n");
    }
    if (!message.empty()) std::printf("\n%s\n", message.c_str());
}

// Writes backup + edited save, commits, and re-verifies.
// `wroteToDisk` is true once the save file on the console may have been modified, which is
// the only case where the in-memory state must be re-synced from disk.
std::string performSave(dsr::Save& save, const std::string& saveName, bool& wroteToDisk) {
    wroteToDisk = false;
    mkdir(kAppDir, 0777);
    mkdir(kBackupDir, 0777);

    std::string bak = backupPath(saveName);
    if (!writeNewFile(bak, save.originalBytes()))
        return "\x1b[31mSave NOT written: could not create the SD card backup. Your edits are kept.\x1b[0m";

    const std::vector<uint8_t>& out = save.finalize();
    std::string path = std::string("save:/") + saveName;

    wroteToDisk = true;
    if (!overwriteInPlace(path, out))
        return "\x1b[31mWrite failed. The original backup is on the SD card:\x1b[0m\n" + bak;

    Result rc = fsdevCommitDevice(kMount);
    if (R_FAILED(rc)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "\x1b[31mCommit failed (rc=0x%X). Changes may not have been kept.\x1b[0m", rc);
        return buf;
    }

    // Read back and verify.
    std::vector<uint8_t> check;
    if (!readFile(path, check) || check != out || !dsr::Save::checksumValid(check))
        return "\x1b[31mWritten file did not verify. Restore the backup:\x1b[0m\n" + bak;

    return "\x1b[32mSaved and verified.\x1b[0m Backup: " + bak;
}

void editorLoop(dsr::Save& save, const std::string& user, const std::string& file) {
    Screen screen = Screen::Stats;
    int sels[4] = {0, 0, 0, 0}; // selection per screen
    Mode mode = Mode::Edit;
    std::string message;
    if (!save.editable()) message = "Editing is disabled for this file. Nothing will be written.";
    bool redraw = true;
    int holdFrames = 0;

    auto sel = [&]() -> int& { return sels[screen == Screen::Stats ? 0 : screen == Screen::Items ? 1 : screen == Screen::Add ? 2 : 3]; };
    auto listSize = [&]() -> int {
        if (screen == Screen::Stats) return (int)dsr::kFieldCount;
        if (screen == Screen::Items) return (int)save.itemCount();
        return addTotal(screen);
    };
    auto clampItemSel = [&]() {
        int n = (int)save.itemCount();
        if (sels[1] >= n) sels[1] = n ? n - 1 : 0;
    };
    auto adjust = [&](int64_t delta) {
        if (screen == Screen::Stats) save.adjust(sel(), delta);
        else if (screen == Screen::Items) save.adjustItemQty(sel(), delta);
    };
    auto setMax = [&]() {
        if (screen == Screen::Stats) save.setMax(sel());
        else if (screen == Screen::Items) save.setItemMax(sel());
    };
    auto revert = [&]() {
        if (screen == Screen::Stats) save.revert(sel());
        else if (screen == Screen::Items) { save.revertItem(sel()); clampItemSel(); }
    };

    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 d = padGetButtonsDown(&g_pad);
        u64 h = padGetButtons(&g_pad);

        if (mode == Mode::ConfirmSave) {
            if (d & HidNpadButton_A) {
                bool wrote = false;
                message = performSave(save, file, wrote);
                // Once the file may have changed, the file on disk is the new baseline.
                // If nothing was written (e.g. backup failed), keep the user's edits.
                if (wrote) {
                    std::vector<uint8_t> fresh;
                    if (readFile(std::string("save:/") + file, fresh)) save.load(std::move(fresh), save.base());
                    // The item list is rebuilt on load, so keep the cursor inside it.
                    clampItemSel();
                }
                mode = Mode::Edit;
                redraw = true;
            } else if (d & HidNpadButton_B) {
                mode = Mode::Edit;
                message.clear();
                redraw = true;
            }
        } else if (mode == Mode::ConfirmQuit) {
            if (d & HidNpadButton_A) return;
            if (d & HidNpadButton_B) { mode = Mode::Edit; message.clear(); redraw = true; }
        } else {
            if (d & (HidNpadButton_B | HidNpadButton_Plus)) {
                if (save.dirty()) { mode = Mode::ConfirmQuit; redraw = true; }
                else return;
            }
            if (d & HidNpadButton_Minus) {
                screen = (screen == Screen::Stats) ? Screen::Items : (screen == Screen::Items) ? Screen::Add : Screen::Stats;
                message.clear();
                redraw = true;
            }
            int n = listSize(); // may be 0 on the Items screen
            if (n > 0) {
                if (d & HidNpadButton_Up) { sel() = (sel() + n - 1) % n; redraw = true; }
                if (d & HidNpadButton_Down) { sel() = (sel() + 1) % n; redraw = true; }
            }

            if (save.editable() && isAddScreen(screen) && (d & HidNpadButton_X)) {
                size_t idx = 0;
                const size_t pick = (size_t)(addOffset(screen) + sel());
                const uint32_t want = dsr::kAddableItems[pick].maxNum;
                dsr::AddResult r = save.addItem(pick, want, &idx);
                const char* nm = dsr::kAddableItems[pick].name;
                bool goods = dsr::kAddableItems[pick].tag == dsr::kGoodsTag;
                char buf[160];
                switch (r) {
                    case dsr::AddResult::Added:
                        if (goods) std::snprintf(buf, sizeof(buf), "Added %s x%u. Press A to save, or Items screen to adjust.", nm, (unsigned)want);
                        else std::snprintf(buf, sizeof(buf), "Added %s. Press A to save.", nm);
                        break;
                    case dsr::AddResult::SetExisting:
                        std::snprintf(buf, sizeof(buf), "You already own %s: set it to x%u.", nm, (unsigned)want);
                        break;
                    case dsr::AddResult::AlreadyOwned:
                        std::snprintf(buf, sizeof(buf), "You already own %s. Nothing changed.", nm);
                        break;
                    case dsr::AddResult::NoFreeSlot:
                        std::snprintf(buf, sizeof(buf), "No suitable empty inventory slot was found. Nothing changed.");
                        break;
                    default:
                        std::snprintf(buf, sizeof(buf), "Cannot add items to this save.");
                        break;
                }
                message = buf;
                redraw = true;
            }
            if (isAddScreen(screen) && n > 0) {
                if (isAddScreen(screen) && (d & (HidNpadButton_ZL | HidNpadButton_ZR)) &&
                    (padGetButtons(&g_pad) & (HidNpadButton_ZL | HidNpadButton_ZR)) == (HidNpadButton_ZL | HidNpadButton_ZR)) {
                    g_showCut = !g_showCut;
                    if (!g_showCut && sel() >= (int)dsr::kNormalAddableCount) sel() = (int)dsr::kNormalAddableCount - 1;
                    message = g_showCut ? "Cut items shown at the end of the list (unobtainable in the real game)." : "Cut items hidden.";
                    redraw = true;
                }
                if (d & HidNpadButton_L) { sel() = sel() >= 10 ? sel() - 10 : 0; redraw = true; }
                if (d & HidNpadButton_R) { sel() = sel() + 10 < n ? sel() + 10 : n - 1; redraw = true; }
                if (save.editable() && (d & HidNpadButton_Y)) {
                    message = save.revertAdded((size_t)(addOffset(screen) + sel())) ? "Removed the item you added." : "That item was not added in this session.";
                    redraw = true;
                }
            }
            if (save.editable() && !isAddScreen(screen) && n > 0) {
                // Fine steps repeat while held (after a short delay).
                bool left = h & HidNpadButton_Left, right = h & HidNpadButton_Right;
                if (left || right) holdFrames++; else holdFrames = 0;
                bool fire = (d & (HidNpadButton_Left | HidNpadButton_Right)) || (holdFrames > 20 && holdFrames % 3 == 0);
                int step = left ? -1 : 1;
                if (fire && (left || right)) { adjust(step); redraw = true; message.clear(); }

                if (d & HidNpadButton_L) { adjust(-10); redraw = true; message.clear(); }
                if (d & HidNpadButton_R) { adjust(10);  redraw = true; message.clear(); }
                if (screen == Screen::Stats) { // big steps only make sense for souls/HP-sized numbers
                    if (d & HidNpadButton_ZL) { adjust(-1000); redraw = true; message.clear(); }
                    if (d & HidNpadButton_ZR) { adjust(1000);  redraw = true; message.clear(); }
                }
                if (d & HidNpadButton_X) { setMax();  redraw = true; message.clear(); }
                if (d & HidNpadButton_Y) { revert();  redraw = true; message.clear(); }
            }
            if (save.editable() && (d & HidNpadButton_A)) {
                if (save.dirty()) { mode = Mode::ConfirmSave; redraw = true; }
                else { message = "No changes to save."; redraw = true; }
            }
        }

        if (redraw) {
            draw(save, user, file, screen, sel(), mode, message);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }
}

// Lets the user choose among several characters in one save file. Returns the index into `c`,
// or -1 if the app is closing or the user backed out.
int pickCharacter(const std::vector<dsr::CharacterInfo>& c) {
    int sel = 0;
    for (size_t i = 0; i < c.size(); i++)
        if (c[i].isFileDefault) sel = (int)i;
    bool redraw = true;
    while (appletMainLoop()) {
        if (redraw) {
            consoleClear();
            std::printf("Dark Souls Remastered Save Editor\n\nThis save has %u characters. Choose one to edit:\n\n", (unsigned)c.size());
            for (size_t i = 0; i < c.size(); i++)
                std::printf(" %s Character %u   level %-4u  souls %-10u%s\n", (int)i == sel ? ">" : " ", (unsigned)i + 1, c[i].level, c[i].souls,
                            c[i].isFileDefault ? "  (the game's slot 1)" : "");
            std::printf("\nUp/Down: choose   A: edit this character   +: exit\n");
            redraw = false;
        }
        u64 d = pollDown();
        if (d & HidNpadButton_Plus) return -1;
        if (d & HidNpadButton_A) return sel;
        if (d & HidNpadButton_Up) { sel = (sel + (int)c.size() - 1) % (int)c.size(); redraw = true; }
        if (d & HidNpadButton_Down) { sel = (sel + 1) % (int)c.size(); redraw = true; }
        consoleUpdate(nullptr);
    }
    return -1;
}

bool showWarning() {
    consoleClear();
    std::printf("Dark Souls Remastered Save Editor\n\n"
                "This edits your REAL save data on this console.\n\n"
                " - A backup is written to the SD card before every write.\n"
                " - Edited saves can be flagged online. Play OFFLINE with an edited save,\n"
                "   and keep a backup of your untouched save.\n"
                " - If the save holds several characters you will be asked which to edit.\n"
                " - Close the game completely before running this.\n\n"
                "A: continue      +: exit\n");
    u64 pressed = 0;
    if (!waitFor(HidNpadButton_A | HidNpadButton_Plus, &pressed)) return false;
    return pressed & HidNpadButton_A;
}

} // namespace

int main(int, char**) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    bool accountsReady = false, mounted = false;

    do {
        if (!showWarning()) break;

        Result rc = accountInitialize(AccountServiceType_Application);
        if (R_FAILED(rc)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "accountInitialize failed (rc=0x%X).", rc);
            fatalScreen("Account service error", msg);
            break;
        }
        accountsReady = true;

        AccountUid uid;
        std::string user;
        if (!pickUser(uid, user)) break;

        rc = fsdevMountSaveData(kMount, kDsrTitleId, uid);
        if (R_FAILED(rc)) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "Could not open Dark Souls Remastered save data for %s (rc=0x%X).\n\n"
                          "Make sure the game is installed, has been started at least once with this\n"
                          "user (so a save exists), and is not running.",
                          user.c_str(), rc);
            fatalScreen("Cannot mount save", msg);
            break;
        }
        mounted = true;

        std::vector<Entry> listing;
        std::string file = findSaveFile(listing);
        if (file.empty()) {
            std::string msg = "No DRAKS*.sl2 file was found on this save.\n"
                              "If you have several user profiles, try the one that plays Dark Souls.\n\n"
                              "What is actually on the save for " + user + ":\n" + describeListing(listing);
            fatalScreen("No save file found", msg.c_str());
            break;
        }

        std::vector<uint8_t> bytes;
        if (!readFile(std::string("save:/") + file, bytes)) {
            fatalScreen("Read error", "Could not read the save file.");
            break;
        }

        dsr::Save save;
        std::vector<dsr::CharacterInfo> chars = dsr::Save::listCharacters(bytes);
        if (chars.size() > 1) {
            int pick = pickCharacter(chars);
            if (pick < 0) break;
            save.load(std::move(bytes), chars[(size_t)pick].base);
        } else if (chars.size() == 1) {
            save.load(std::move(bytes), chars[0].base);
        } else {
            save.load(std::move(bytes)); // unrecognised layout: shows the reason below
        }
        if (!save.readable()) {
            fatalScreen("Unsupported save", dsr::statusText(save.status()));
            break;
        }

        editorLoop(save, user, file);
    } while (false);

    if (mounted) fsdevUnmountDevice(kMount);
    if (accountsReady) accountExit();
    consoleExit(nullptr);
    return 0;
}
