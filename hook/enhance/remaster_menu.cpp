// In-game "Remaster" options pages (docs/subsystems/menus.md section 3). Switch in [remaster] in and.ini:
//
//   menu=1          add "Remaster" to the Options page (between Cheats and Back) and two pages that list every
//                   setting of the registry (settings.h). 0 = nothing is hooked. Default 1: the pages only add an
//                   entry; every setting on them keeps its own default.
//   menu_rows=11    rows per column (the Graphics page uses rows 1..11)
//   menu_row_px=24  row spacing in 640x480 pixels (the game's own spacing is 24)
//
// Pages (one column each, so bars, their value texts and checkbox labels never overlap; seen in
// work/shots/upnav.png, 2026-10-04: a second column at ref_text_04 starts near x = 370 of 640, the bars' value texts
// are drawn near x = 550):
//   "Remaster"  (bank page pui_directorselect): sliders and lists, then "More...", Back.
//   "Remaster2" (bank page pui_tcrc42):         toggles; more than menu_rows continue in a second column at
//                                               ref_text_04 (no bars on this page), Back.
// Labels come from a short-label table keyed by section.key (long labels are shrunk by the game's auto-fit,
// 0x00465720, so every label is kept to about 19 characters, which drew at full size in the screenshot).
//
// How (evidence: tools/recover.py --asm of every address below):
// - 0x0048ff90 (UI manager build, stdcall(ui), returns ui) is hooked. After it, the two screens are allocated from
//   the game heap and constructed like the game's own screens. They and the "Options" screen get copies of the
//   default screen vtable 0x0058fce0 with slot 5 (InitItem) and slot 6 (Activate) wrapped. Unknown codes keep going
//   to the original handlers.
// - 0x004629b0 (UI build from the bank, stdcall(bank, x), runs on every level load) is hooked. After it, one
//   ui_text instance per setting is spawned exactly like its text-instance loop (0x004634b0..0x00463575), plus
//   titles, footers, "More...", Back items (code 5) and the Options entry (id 8313, code 83).
// - 0x00495f00 Text_Lookup (eax = id, ebx = bank, [nullOnFail], callee pops 4) is hooked: ids starting "and_"
//   return our labels, everything else goes to the original.
// - Item binding uses the game's own setup functions (checkbox 0x0046cb60, slider 0x0046cce0, list 0x0046cd50).
//   A checkbox is toggled by its HandleInput 0x0046cbf0 before Activate runs (Update 0x004929e0 calls slot 6 when
//   the focused item's HandleInput returns 0 with the select bit); sliders and lists call our callback
//   (cdecl cb(item, ctx), 0x0046d030) on left/right.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

namespace
{
// --- addresses and constants (each with its evidence) -------------------------------------------------------

constexpr uint32_t kGameHeap = 0x0063a210;      // heap passed to 0x004e07f0 by 0x0048ff90 for every screen
constexpr uint32_t kScreenTag = 0x0058b5e8;     // allocation tag pushed by 0x0048ff90 (0x004901c1)
constexpr uint32_t kDefaultScreenVtbl = 0x0058fce0;  // 0x004916e0 stores it at +0 (0x004916ef)
constexpr int kVtblSlots = 9;                   // menus.md: 9 slots; slot 8 (+0x20) is the highest called (0x004921a0 case 0x49)
constexpr int kSlotInitItem = 5, kSlotActivate = 6;

// Item fields (menus.md "Common item fields"; 0x004629b0 writes +0x468..+0x488, 0x004659e0 writes +0x4e8..+0x4f4)
constexpr uint32_t kRef = 0x468, kPage = 0x46c, kId = 0x478, kLeft = 0x47c, kRight = 0x480,
                   kUp = 0x484, kDown = 0x488, kY = 0x4ec;
constexpr uint32_t kBarValue = 0x598, kBarIndex = 0x5a0;  // 0x0046cfd0 writes them; 0x0048fef0 / 0x00470f70 read them

// UI bank (g_game+0xa70): +0x7c page count, +0x80 ref count, +0x84 / +0x88 page / ref name lists (0x004629b0).
constexpr uint32_t kGameBank = 0xa70, kGameObjMgr = 0xa84;

// Codes: 83 is our Options entry, 84..97 our settings, 98 "More...", 5 = Back (0x00491b70 case 5, 0x004921a0
// case 5 -> 0x00490b60), 99 = static text (0x00491b70 case 0x63: enabled, inert 0x1000, never selectable). None of
// 83..98 is in the bank or in 0x00491b70 / 0x004710d0 / 0x004921a0 / 0x004714c0 (menus.md section 2). Ids only
// need to be unique per page (0x00463b90 matches page and id); our handlers find items by pointer.
constexpr int kCodeEntry = 83, kCodeFirst = 84, kCodeLastSetting = 97, kCodeMore = 98;
constexpr int kEntryId = kCodeEntry * 100 + 13;  // Options page: ref 67 rows 3..11 step 2 (bank), so row 13
constexpr int kCheatsId = 6611, kOptionsBackId = 509;  // bank text instances on pui_options (ui.json dump)

// Colours and scales copied from bank text instances: pui_gfxopt items 0x00A0FFA0 scale 1 / title 1.4,
// pui_options menu items 0x00D05030 scale 1.3.
constexpr uint32_t kColourPage = 0x00A0FFA0, kColourOptions = 0x00D05030;

constexpr size_t kMaxSettings = 64;
constexpr int kScreens = 2;

// Unused bank pages (menus.md 3.2: no code refers to their names; all pages use the model pui_general, so the
// ref_text_* nodes exist on each: lev_1_m1_big07_obj.big lists "pui_directorselect staticmodel pui_general").
const char* const kPageNames[kScreens] = {"pui_directorselect", "pui_tcrc42"};
const char* const kScreenNames[kScreens] = {"Remaster", "Remaster2"};

// Short labels (about 19 characters at most, " *" included), keyed by and.ini section and key. Settings not listed
// use their registry label.
struct ShortLabel
{
    const char* section;
    const char* key;
    const wchar_t* label;
};
const ShortLabel kShortLabels[] = {
    {"display", "brightness_shader", L"Window gamma (VK)"},
    {"display", "fps_limit", L"Frame rate limit"},
    {"display", "borderless", L"Borderless window"},
    {"display", "render_scale", L"Render scale"},
    {"audio", "master", L"Master volume"},
    {"camera", "hor_plus", L"Widescreen (Hor+)"},
    {"camera", "letterbox", L"Cinematic letterbox"},
    {"camera", "fov", L"Field of view"},
    {"camera", "fov_zoom", L"FOV when zoomed"},
    {"persistence", "decal_pool_choice", L"Decal pool"},
    {"persistence", "decals_keep", L"Keep decals"},
    {"persistence", "bodies_keep", L"Keep bodies"},
    {"qol", "quick_keys", L"F5/F9 quick save"},
    {"qol", "mouse_invert_y", L"Invert mouse Y"},
    {"qol", "mouse_x", L"Mouse speed X"},
    {"qol", "mouse_y", L"Mouse speed Y"},
    {"render", "tessellation", L"Tessellation (VK)"},
    {"render", "anisotropy", L"Anisotropy (VK)"},
    {"render", "tonemap_aces", L"Tone mapping (VK)"},
    {"ui", "anchor", L"4:3 HUD and menus"},
    {"ui", "text_scale", L"Text size"},
};

struct Slot
{
    enum Kind { Setting, Title, Footer, Back, More } kind;
    int screen;    // 0 = Remaster, 1 = Remaster2
    int setting;   // index into RemasterSettings() (Setting only)
    const char* ref;
    int id;        // code * 100 + row (row also places the item: 0x00491b70 subtracts row * 24 px)
    int left, right, up, down;
    void* item;    // spawned world object of the current level, or null
};

// --- state --------------------------------------------------------------------------------------------------

int g_rows = 11, g_rowPx = 24;
size_t g_count;                       // settings listed (snapshot at init)
std::vector<Slot> g_slots;            // layout, fixed at init
std::vector<std::wstring> g_labels;   // per setting, fixed at init (the game keeps the pointers, +0x4c0)
std::vector<std::string> g_textIds;   // "and_sNN", fixed at init (0x00465850 copies them to +0x4a0)
bool g_bools[kMaxSettings];           // checkbox bindings (+0x4fc points here)
bool g_restart[kScreens];             // a setting on this screen takes effect after a restart (footer)
bool g_inSetup;                       // the setup functions call the bar callback once; that is not a change

void* g_screens[kScreens];            // our MenuScreens
void* g_options;                      // the game's "Options" screen
void* g_entry;                        // Options entry item of the current level
int g_pages[kScreens] = {-1, -1}, g_optionsPage = -1;
void* g_remasterVtbl[kVtblSlots];     // shared by both of our screens (items are found by pointer)
void* g_optionsVtbl[kVtblSlots];

using UiBuild_t = uint32_t(__stdcall*)(uint32_t ui);
using UiBankBuild_t = uint32_t(__stdcall*)(uint32_t bank, uint32_t arg);
UiBuild_t g_origUiBuild;
UiBankBuild_t g_origBankBuild;

unsigned g_failed;  // one bit per failure kind, so each is logged once

void Fail(unsigned bit, const char* fmt, ...)
{
    if (g_failed & (1u << bit))
        return;
    g_failed |= 1u << bit;
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    Log("remaster menu: %s", text);
}

uint32_t U(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
void* P(uint32_t v) { return reinterpret_cast<void*>(static_cast<uintptr_t>(v)); }

int CodeOf(void* item)
{
    // 0x00491ae0: code = id / 100 unless id is 0 or -1
    uint32_t id = Field<uint32_t>(item, kId);
    return id == 0 || id == 0xffffffffu ? 0 : static_cast<int>(id / 100);
}

Slot* FindSlot(void* item)
{
    if (!item)
        return nullptr;
    for (Slot& s : g_slots)
        if (s.item == item)
            return &s;
    return nullptr;
}

std::wstring LabelFor(const RemasterSetting& s)
{
    for (const ShortLabel& l : kShortLabels)
        if (_stricmp(l.section, s.section) == 0 && _stricmp(l.key, s.key) == 0)
            return l.label;
    std::wstring label = s.label ? s.label : L"?";
    if (label.size() > 17)
        Log("remaster menu: label of [%s] %s has %u characters; the game may shrink it (add a short label)",
            s.section, s.key, static_cast<unsigned>(label.size()));
    return label;
}

// --- layout ---------------------------------------------------------------------------------------------------

int SettingCode(size_t k)
{
    return kCodeFirst + (k < static_cast<size_t>(kCodeLastSetting - kCodeFirst) ? static_cast<int>(k)
                                                                                : kCodeLastSetting - kCodeFirst);
}

// Rows follow the Graphics page: its bars and left checkboxes sit at ref_text_05 rows 1..11, its right checkboxes at
// ref_text_04 rows 10..12, linked so that ref_text_05 row r pairs with ref_text_04 row r + 1 (5309 <-> 7710). Back
// sits where the Options page has its Back (ref_info, row 9), the title where the Graphics page has its title
// (ref_text_06), the footer under Back (ref_info, row 10).
void AddScreen(int screen, const std::vector<int>& settings, bool more)
{
    size_t first = g_slots.size();
    std::vector<int> rows, right;
    for (size_t k = 0; k < settings.size(); ++k)
    {
        // Screen 0 holds bars: one column only (the bars' value texts reach x = 550 of 640). Screen 1 holds
        // checkboxes: a second column at ref_text_04 when the first is full.
        bool second = screen == 1 && static_cast<int>(k) >= g_rows;
        int row = second ? static_cast<int>(k) - g_rows + 2 : static_cast<int>(k) + 1;
        if (row > 99)
            row = 99;
        g_slots.push_back({Slot::Setting, screen, settings[k], second ? "ref_text_04" : "ref_text_05",
                           SettingCode(k) * 100 + row, -1, -1, -1, -1, nullptr});
        rows.push_back(row), right.push_back(second);
    }
    if (screen == 0 && static_cast<int>(settings.size()) > g_rows)
        Log("remaster menu: %u sliders and lists on one page of %d rows; they run on below",
            static_cast<unsigned>(settings.size()), g_rows);
    if (more)
    {
        int row = static_cast<int>(settings.size()) + 1;
        g_slots.push_back({Slot::More, screen, -1, "ref_text_05", kCodeMore * 100 + (row > 99 ? 99 : row), -1, -1,
                           -1, -1, nullptr});
        right.push_back(false), rows.push_back(row);
    }
    size_t n = g_slots.size() - first;
    for (size_t a = 0; a < n; ++a)
        for (size_t b = 0; b < n; ++b)
            if (!right[a] && right[b] && rows[b] == rows[a] + 1)
                g_slots[first + a].right = g_slots[first + b].id, g_slots[first + b].left = g_slots[first + a].id;
    // One up/down chain: first column, second column, Back.
    int backId = 5 * 100 + 9;
    for (size_t k = 0; k < n; ++k)
    {
        g_slots[first + k].up = k > 0 ? g_slots[first + k - 1].id : -1;
        g_slots[first + k].down = k + 1 < n ? g_slots[first + k + 1].id : backId;
    }
    g_slots.push_back({Slot::Back, screen, -1, "ref_info", backId, -1, -1, n ? g_slots[first + n - 1].id : -1, -1,
                       nullptr});
    g_slots.push_back({Slot::Title, screen, -1, "ref_text_06", 99 * 100 + 0, -1, -1, -1, -1, nullptr});
    if (g_restart[screen])
        g_slots.push_back({Slot::Footer, screen, -1, "ref_info", 99 * 100 + 10, -1, -1, -1, -1, nullptr});
}

void BuildLayout()
{
    auto& all = RemasterSettings();
    std::vector<int> bars, toggles;
    for (size_t i = 0; i < g_count; ++i)
    {
        bool toggle = all[i].kind == RemasterSetting::Toggle;
        (toggle ? toggles : bars).push_back(static_cast<int>(i));
        g_restart[toggle ? 1 : 0] |= all[i].restart;
    }
    AddScreen(0, bars, true);
    AddScreen(1, toggles, false);
    if (static_cast<int>(toggles.size()) > 2 * g_rows)
        Log("remaster menu: %u toggles do not fit in two columns of %d rows; the second column runs on",
            static_cast<unsigned>(toggles.size()), g_rows);
}

// --- UI bank helpers ------------------------------------------------------------------------------------------

// Ref index by name: the "refnames list" at bank+0x88, count at bank+0x80 (0x004629b0: param_1+0x80 / +0x88).
int RefIndex(void* bank, const char* name)
{
    uint32_t count = Field<uint32_t>(bank, 0x80);
    const char* const* names = Field<const char* const*>(bank, 0x88);
    if (!names)
        return -1;
    for (uint32_t i = 0; i < count && i < 256; ++i)
        if (names[i] && _stricmp(names[i], name) == 0)
            return static_cast<int>(i);
    return -1;
}

// 0x00463c50: ebx = page name, edi = bank; walks bank+0x84 (count bank+0x7c) with _stricmp; -1 when absent.
// ecx is only scratch for _stricmp (not read).
int PageIndex(void* bank, const char* name)
{
    return static_cast<int>(orig::fn_00463c50(0, U(name), U(bank)));
}

// Spawns one ui_text instance like 0x004629b0's text loop (0x004634b0..0x00463575):
//   0x0045f020(eax = class name or 0, ecx = "ui_text", [objMgr, 0, 0x3ff, 0, 0, 0, 0, 0xaa, 1])
//   +0x46c = page, +0x468 = ref, 0x004659e0(obj, 0.5, 0.5, 0.5, 0.5), 0x004659b0(obj, scale),
//   0x00465850(ecx = text id, edi = obj, [1]), 0x004659d0(obj, ecx = colour), then +0x478..+0x488.
void* Spawn(const char* cls, const char* textId, int page, int ref, float scale, uint32_t colour, int id, int left,
            int right, int up, int down)
{
    void* objMgr = Field<void*>(g_game, kGameObjMgr);
    void* obj = P(orig::fn_0045f020(U(cls), U("ui_text"), U(objMgr), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 1));
    if (!obj)
    {
        Fail(1, "spawning %s (%s) failed: 0x0045f020 returned null", textId, cls ? cls : "ui_text");
        return nullptr;
    }
    Field<int32_t>(obj, kPage) = page;
    Field<int32_t>(obj, kRef) = ref;
    float half = 0.5f;
    orig::fn_004659e0(U(obj), orig::f2u(half), orig::f2u(half), orig::f2u(half), orig::f2u(half));
    orig::fn_004659b0(U(obj), orig::f2u(scale));
    orig::fn_00465850(U(textId), U(obj), 1);
    orig::fn_004659d0(U(obj), colour);
    Field<int32_t>(obj, kId) = id;
    Field<int32_t>(obj, kLeft) = left;
    Field<int32_t>(obj, kRight) = right;
    Field<int32_t>(obj, kUp) = up;
    Field<int32_t>(obj, kDown) = down;
    // Start hidden, like the bank's own menu items: +0x470 bit 0 is "shown", bit 1 the state saved when a page opens
    // (0x00463d60) and restored when it closes (0x00463df0). Spawned shown, an item's saved state was "shown" and
    // closing the page left it drawn over the game (measured 2026-10-04: menu over gameplay, 6-15 fps).
    Field<uint32_t>(obj, 0x470) &= ~3u;
    return obj;
}

// --- item binding ---------------------------------------------------------------------------------------------

void SetValueText(void* item, size_t index)
{
    const RemasterSetting& s = RemasterSettings()[index];
    wchar_t text[64];
    if (s.kind == RemasterSetting::List)
    {
        int i = Field<int32_t>(item, kBarIndex);
        if (i >= 0 && static_cast<size_t>(i) < s.names.size() && s.names[i])
            wcsncpy_s(text, s.names[i], _TRUNCATE);
        else
            swprintf(text, 64, L"%d", i + static_cast<int>(s.minimum));
    }
    else
    {
        float v = Field<float>(item, kBarValue);
        if (s.step >= 1.0f)
            swprintf(text, 64, L"%d", static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)));
        else
            swprintf(text, 64, L"%.2f", v);
    }
    orig::fn_0046d0e0(U(item), U(text));  // ebx = item, esi = text: copies (< 64 chars) to +0x4f8, sets 0x2000
}

// cdecl cb(item, ctx), called by 0x0046d030 (push ctx; push item; call; add esp, 8).
void __cdecl BarChanged(void* item, uint32_t index)
{
    if (!item || index >= g_count)
        return;
    const RemasterSetting& s = RemasterSettings()[index];
    if (!g_inSetup)
    {
        float v;
        if (s.kind == RemasterSetting::List)
            v = s.minimum + static_cast<float>(Field<int32_t>(item, kBarIndex));
        else
        {
            v = Field<float>(item, kBarValue);
            if (s.step > 0.0f)
                v = s.minimum + static_cast<float>(static_cast<int>((v - s.minimum) / s.step + 0.5f)) * s.step;
        }
        RemasterSet(index, v);
    }
    SetValueText(item, index);
}

// The original InitItem reached the 0x004710d0 default for our codes: 0x004927a0(item, 0, [screen, 1]) disabled
// and hid the item. Show and enable it again (0x004927a0 with cl = 1 clears 0x40 and sets +0x470 bit 0), and apply
// a row spacing other than the game's 24 px (0x00491bee: y -= row * 24 / 480 when GetFlags has bit 2).
void Show(void* screen, const Slot& slot)
{
    orig::fn_004927a0(U(slot.item), 1, U(screen), 0);
    if (g_rowPx != 24)
        Field<float>(slot.item, kY) += static_cast<float>((slot.id % 100) * (24 - g_rowPx)) / 480.0f;
}

void BindSetting(void* screen, Slot& slot)
{
    void* item = slot.item;
    size_t i = static_cast<size_t>(slot.setting);
    const RemasterSetting& s = RemasterSettings()[i];
    const char* textId = g_textIds[i].c_str();
    Show(screen, slot);
    switch (s.kind)
    {
    case RemasterSetting::Toggle:
        // 0x0046cb60(eax = item, esi = bool*, [text id, applyNow]); needs GetFlags bit 0x10 (ui_checkbox).
        // applyNow = 0: bit 1 of +0x474 would make Activate apply and save the game's own options (0x00401ce0).
        g_bools[i] = s.value != 0.0f;
        orig::fn_0046cb60(U(item), U(&g_bools[i]), U(textId), 0);
        break;
    case RemasterSetting::Slider:
    {
        // 0x0046cce0(eax = item, ecx = text id, [steps, ticks, cb, ctx, min, max, value]), as the Sound page
        // (0x00491e33: 0x15, 1, 0x0048fef0, &volume, 0, 1.0, volume).
        int steps = s.step > 0.0f ? static_cast<int>((s.maximum - s.minimum) / s.step + 0.5f) + 1 : 21;
        if (steps < 2)
            steps = 2;  // 0x0046cf70 clamps to 2 as well
        g_inSetup = true;
        orig::fn_0046cce0(U(item), U(textId), static_cast<uint32_t>(steps), steps <= 21 ? 1u : 0u,
                          U(reinterpret_cast<void*>(&BarChanged)), static_cast<uint32_t>(i), orig::f2u(s.minimum),
                          orig::f2u(s.maximum), orig::f2u(s.value));
        g_inSetup = false;
        break;
    }
    case RemasterSetting::List:
    {
        // 0x0046cd50(eax = item, ecx = text id, [count, ticks, cb, ctx, index]), as the Graphics page LOD lists
        // (0x004710d0 case 0x31: 5, 1, 0x00470f70, &copy, index).
        int count = s.names.empty() ? static_cast<int>(s.maximum - s.minimum + 0.5f) + 1 : static_cast<int>(s.names.size());
        if (count < 2)
            count = 2;
        int index = static_cast<int>(s.value - s.minimum + 0.5f);
        if (index < 0)
            index = 0;
        g_inSetup = true;
        orig::fn_0046cd50(U(item), U(textId), static_cast<uint32_t>(count), 1,
                          U(reinterpret_cast<void*>(&BarChanged)), static_cast<uint32_t>(i),
                          static_cast<uint32_t>(index));
        g_inSetup = false;
        break;
    }
    }
}

// Pushes one of our screens, as 0x004921a0 case 0x34 (Graphics): UI_PushScreen 0x00490b00(eax = 0, ecx = ui,
// ["GfxOpt"]).
uint32_t Push(void* ui, int screen)
{
    if (!g_screens[screen] || !ui)
    {
        Fail(2, "cannot open %s: the screen does not exist", kScreenNames[screen]);
        return 0;
    }
    Log("remaster menu: opening %s", kScreenNames[screen]);
    return orig::fn_00490b00(0, U(ui), U(kScreenNames[screen]));
}

// --- screen vtable slots (thiscall, 2 stack arguments, callee pops: 0x00491b70 / 0x004921a0 end in ret 8) ---------

uint32_t __fastcall RemasterInitItem(void* screen, void*, void* item, uint32_t enter)
{
    uint32_t r = orig::fn_00491b70(U(screen), U(item), enter);
    if (!item)
        return r;
    if (!(enter & 0xff))
    {
        // The page is closing (0x004919c0 with 0): our items must disappear with it. Showing them here kept them
        // drawn over the game after leaving the menu (measured 2026-10-04: menu text over gameplay, 15 fps).
        if (FindSlot(item))
            orig::fn_004927a0(U(item), 0, U(screen), 1);
        return r;
    }
    Slot* slot = FindSlot(item);
    if (!slot)
    {
        // The pages' own bank items (pui_directorselect: 1502, 1608; pui_tcrc42: 2205, 2307, 2400): codes 15, 16,
        // 22, 23 and 24 already reach the 0x004710d0 default (hidden); hide anything else that is not ours as well.
        orig::fn_004927a0(U(item), 0, U(screen), 1);
        return r;
    }
    if (slot->kind == Slot::Setting && static_cast<size_t>(slot->setting) < g_count)
        BindSetting(screen, *slot);
    else if (slot->kind == Slot::More)
    {
        Show(screen, *slot);
        // A plain ui_text is only reached by navigation (0x00464a10) with the selectable flag +0x474 0x100, which
        // the game sets for its own menu texts by code; measured: without it Down skipped "More..." to Back.
        Field<uint32_t>(slot->item, 0x474) = (Field<uint32_t>(slot->item, 0x474) | 0x100) & ~0x1040u;
    }
    return r;
}

uint32_t __fastcall RemasterActivate(void* screen, void*, void* ui, void* item)
{
    Slot* slot = FindSlot(item);
    if (slot && slot->kind == Slot::More)
        return Push(ui, 1);
    if (slot && slot->kind == Slot::Setting)
    {
        size_t i = static_cast<size_t>(slot->setting);
        if (i < g_count && RemasterSettings()[i].kind == RemasterSetting::Toggle)
            RemasterSet(i, g_bools[i] ? 1.0f : 0.0f);  // HandleInput 0x0046cbf0 has already toggled g_bools[i]
        return 0;  // bars change with left/right (callback); select does nothing
    }
    return orig::fn_004921a0(U(screen), U(ui), U(item));  // Back (code 5) and anything else
}

uint32_t __fastcall OptionsInitItem(void* screen, void*, void* item, uint32_t enter)
{
    uint32_t r = orig::fn_00491b70(U(screen), U(item), enter);
    if (!item)
        return r;
    if (!(enter & 0xff))
    {
        if (item == g_entry)  // the Options page is closing: the entry goes with it
            orig::fn_004927a0(U(item), 0, U(screen), 1);
        return r;
    }
    if (item == g_entry)
    {
        // Code 83 reached the 0x004710d0 default (disabled, hidden); 0x100 (selectable) was set before the switch.
        orig::fn_004927a0(U(item), 1, U(screen), 0);
    }
    else if (g_entry)
    {
        int id = Field<int32_t>(item, kId);
        if (id == kCheatsId)
            Field<int32_t>(item, kDown) = kEntryId;
        else if (id == kOptionsBackId)
            Field<int32_t>(item, kUp) = kEntryId;
    }
    return r;
}

uint32_t __fastcall OptionsActivate(void* screen, void*, void* ui, void* item)
{
    if (item && item == g_entry && CodeOf(item) == kCodeEntry)
        return Push(ui, 0);
    return orig::fn_004921a0(U(screen), U(ui), U(item));
}

bool SwapVtable(void* screen, void** copy, void* initItem, void* activate, const char* name)
{
    void** current = *reinterpret_cast<void***>(screen);
    if (reinterpret_cast<uintptr_t>(current) != kDefaultScreenVtbl)
    {
        Log("remaster menu: screen %s has vtable %p, not the default 0x%08x; left alone", name, current,
            kDefaultScreenVtbl);
        return false;
    }
    for (int i = 0; i < kVtblSlots; ++i)
        copy[i] = current[i];
    copy[kSlotInitItem] = initItem;
    copy[kSlotActivate] = activate;
    *reinterpret_cast<void***>(screen) = copy;
    return true;
}

// --- hooks ----------------------------------------------------------------------------------------------------

// 0x0048ff90 builds the UI manager and registers every screen in the list at ui+0xf8 (each 0x004916e0 call links
// the screen through 0x004929a0: +4 next). Returns ui.
uint32_t __stdcall Hook_UiBuild(uint32_t ui)
{
    uint32_t r = g_origUiBuild(ui);
    if (!ui)
        return r;
    void* list = P(ui + 0xf8);
    // Our screens, as 0x0048ff90 builds "Options" (0x004901c1..: alloc(0x63a210, [0x10, 0x68, 2, tag]),
    // 0x004916e0(ecx = flags 0, esi = memory, edi = name, [ui + 0xf8, page name, 0.0f])).
    for (int i = 0; i < kScreens; ++i)
    {
        void* mem = P(orig::fn_004e07f0(kGameHeap, 0x10, 0x68, 2, kScreenTag));
        if (!mem)
        {
            Fail(3, "allocating screen %s failed", kScreenNames[i]);
            continue;
        }
        orig::fn_004916e0(0, U(mem), U(kScreenNames[i]), U(list), U(kPageNames[i]), orig::f2u(0.0f));
        if (SwapVtable(mem, g_remasterVtbl, reinterpret_cast<void*>(&RemasterInitItem),
                       reinterpret_cast<void*>(&RemasterActivate), kScreenNames[i]))
            g_screens[i] = mem;
    }
    // The Options screen: walk the list like 0x00490a80 (+0xc name, +4 next).
    for (void* s = Field<void*>(list, 0); s; s = Field<void*>(s, 4))
        if (_stricmp(reinterpret_cast<const char*>(s) + 0xc, "Options") == 0)
        {
            if (SwapVtable(s, g_optionsVtbl, reinterpret_cast<void*>(&OptionsInitItem),
                           reinterpret_cast<void*>(&OptionsActivate), "Options"))
                g_options = s;
            break;
        }
    Log("remaster menu: screens %s %p (%s), %s %p (%s), Options screen %p", kScreenNames[0], g_screens[0],
        kPageNames[0], kScreenNames[1], g_screens[1], kPageNames[1], g_options);
    if (!g_options)
        Fail(4, "Options screen not found or not hooked; the entry will not appear");
    return r;
}

void SpawnAll()
{
    for (Slot& s : g_slots)
        s.item = nullptr;
    g_entry = nullptr;
    if (!g_game || !g_screens[0] || !g_options)
        return;
    void* bank = Field<void*>(g_game, kGameBank);
    void* objMgr = Field<void*>(g_game, kGameObjMgr);
    if (!bank || !objMgr)
    {
        Fail(5, "no UI bank or object manager");
        return;
    }
    // 0x004629b0 skips text instances when the "ui_text" object is missing (0x0045abe0(ecx = name, esi = objMgr)).
    if (!orig::fn_0045abe0(U("ui_text"), U(objMgr)))
    {
        Fail(6, "this level has no ui_text object; nothing spawned");
        return;
    }
    for (int i = 0; i < kScreens; ++i)
        g_pages[i] = PageIndex(bank, kPageNames[i]);
    g_optionsPage = PageIndex(bank, "pui_options");
    if (g_pages[0] < 0 || g_optionsPage < 0)
    {
        Fail(7, "page index: %s %d, pui_options %d; nothing spawned", kPageNames[0], g_pages[0], g_optionsPage);
        return;
    }
    int spawned = 0;
    for (Slot& s : g_slots)
    {
        int page = g_pages[s.screen];
        if (page < 0 || !g_screens[s.screen])
        {
            Fail(10, "%s unavailable (page %d); its items are not spawned", kScreenNames[s.screen], page);
            continue;
        }
        int ref = RefIndex(bank, s.ref);
        if (ref < 0)
        {
            Fail(8, "ref %s not in the UI bank", s.ref);
            continue;
        }
        switch (s.kind)
        {
        case Slot::Setting:
        {
            bool toggle = RemasterSettings()[static_cast<size_t>(s.setting)].kind == RemasterSetting::Toggle;
            s.item = Spawn(toggle ? "ui_checkbox" : "ui_progressbar", g_textIds[static_cast<size_t>(s.setting)].c_str(),
                           page, ref, 1.0f, kColourPage, s.id, s.left, s.right, s.up, s.down);
            break;
        }
        case Slot::More:
            if (g_screens[1] && g_pages[1] >= 0)
                s.item = Spawn(nullptr, "and_more", page, ref, 1.0f, kColourPage, s.id, s.left, s.right, s.up, s.down);
            break;
        case Slot::Back:
            s.item = Spawn(nullptr, "ui_shellmenu_back", page, ref, 1.2f, kColourPage, s.id, -1, -1, s.up, s.down);
            break;
        case Slot::Title:
            s.item = Spawn(nullptr, "and_title", page, ref, 1.4f, kColourPage, s.id, -1, -1, -1, -1);
            break;
        case Slot::Footer:
            s.item = Spawn(nullptr, "and_footer", page, ref, 0.9f, kColourPage, s.id, -1, -1, -1, -1);
            break;
        }
        spawned += s.item ? 1 : 0;
    }
    int ref = RefIndex(bank, "ref_options_menu");
    if (ref >= 0)
        g_entry = Spawn(nullptr, "and_entry", g_optionsPage, ref, 1.3f, kColourOptions, kEntryId, -1, -1, kCheatsId,
                        kOptionsBackId);
    else
        Fail(9, "ref ref_options_menu not in the UI bank; no Options entry");
    Log("remaster menu: spawned %d of %u items on pages %d and %d, Options entry %p on page %d", spawned,
        static_cast<unsigned>(g_slots.size()), g_pages[0], g_pages[1], g_entry, g_optionsPage);
}

// 0x004629b0 (stdcall: bank = g_game+0xa70, level; ret 8) spawns the level's UI objects; world objects are
// rebuilt per level, so ours are spawned again after each call.
uint32_t __stdcall Hook_BankBuild(uint32_t bank, uint32_t arg)
{
    for (Slot& s : g_slots)
        s.item = nullptr;  // the old level's objects are gone
    g_entry = nullptr;
    uint32_t r = g_origBankBuild(bank, arg);
    SpawnAll();
    return r;
}
}  // namespace

// --- Text_Lookup 0x00495f00 -------------------------------------------------------------------------------------

// Our labels for "and_" ids, or null to let the original look the id up.
static const wchar_t* __cdecl RemasterText(const char* id)
{
    if (!id || id[0] != 'a' || id[1] != 'n' || id[2] != 'd' || id[3] != '_')
        return nullptr;
    const char* rest = id + 4;
    if (strcmp(rest, "title") == 0 || strcmp(rest, "entry") == 0)
        return L"Remaster";
    if (strcmp(rest, "more") == 0)
        return L"More...";
    if (strcmp(rest, "footer") == 0)
        return L"* after a restart";
    if (rest[0] == 's' && rest[1] >= '0' && rest[1] <= '9')
    {
        size_t i = static_cast<size_t>(atoi(rest + 1));
        if (i < g_labels.size())
            return g_labels[i].c_str();
    }
    return nullptr;
}

static void* g_origTextLookup;

// eax = id, ebx = bank, [esp + 4] = nullOnFail; returns eax; callee pops 4 (0x00495f35: ret 4). The original
// clobbers eax ecx edx only; ebx, esi, edi and ebp survive the cdecl call.
static __declspec(naked) void Hook_TextLookup()
{
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call RemasterText
        add esp, 4
        pop edx
        pop ecx
        test eax, eax
        jz forward
        add esp, 4
        ret 4
    forward:
        pop eax
        jmp dword ptr [g_origTextLookup]
    }
}

void RemasterMenuInit()
{
    if (ConfigInt("remaster", "menu", 1) == 0)
    {
        Log("remaster menu: off");
        return;
    }
    g_rows = ConfigInt("remaster", "menu_rows", 11);
    if (g_rows < 1 || g_rows > 20)
        g_rows = 11;
    g_rowPx = ConfigInt("remaster", "menu_row_px", 24);
    if (g_rowPx < 8 || g_rowPx > 48)
        g_rowPx = 24;
    auto& all = RemasterSettings();
    g_count = all.size() < kMaxSettings ? all.size() : kMaxSettings;
    g_labels.reserve(g_count);
    g_textIds.reserve(g_count);
    for (size_t i = 0; i < g_count; ++i)
    {
        std::wstring label = LabelFor(all[i]);
        if (all[i].restart)
            label += L" *";
        g_labels.push_back(label);
        char id[16];
        snprintf(id, sizeof(id), "and_s%02u", static_cast<unsigned>(i));
        g_textIds.push_back(id);
    }
    BuildLayout();
    bool ok = HookAt(reinterpret_cast<void*>(0x00495f00), reinterpret_cast<void*>(&Hook_TextLookup),
                     &g_origTextLookup, "remaster menu: Text_Lookup");
    ok = HookAt(reinterpret_cast<void*>(0x0048ff90), reinterpret_cast<void*>(&Hook_UiBuild),
                reinterpret_cast<void**>(&g_origUiBuild), "remaster menu: UI manager build") && ok;
    ok = HookAt(reinterpret_cast<void*>(0x004629b0), reinterpret_cast<void*>(&Hook_BankBuild),
                reinterpret_cast<void**>(&g_origBankBuild), "remaster menu: UI bank build") && ok;
    Log("remaster menu: %u settings, %u items on 2 pages, rows %d, row %d px, hooks %s",
        static_cast<unsigned>(g_count), static_cast<unsigned>(g_slots.size()), g_rows, g_rowPx, ok ? "ok" : "FAILED");
}
