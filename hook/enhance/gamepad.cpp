// Game controllers through SDL3 (docs/subsystems/qol.md section 1): DualShock 4, DualSense (USB and Bluetooth,
// SDL's HIDAPI drivers), Xbox pads (XInput) and anything else SDL knows as a gamepad. Switch in [input] in and.ini:
//
//   gamepad=1   remaster default on, original off
//
// Tests: [test] pad_keys=600:START,660:LY-*40 drives an SDL virtual gamepad (used instead of real pads) in presented
// frames: buttons SOUTH EAST WEST NORTH L1 R1 BACK START L3 R3 UP DOWN LEFT RIGHT, triggers L2 R2, stick directions
// LX+ LX- LY+ LY- RX+ RX- RY+ RY- (full deflection); *N holds for N frames (3 by default).
//
// How: the game already has a controller path built around one DirectInput joystick (InputPC 0x004f8600): a 0x34-byte
// state at pc+0x190 (four axes recentred into floats at +0x1f8..+0x204, the D-pad as a POV at +0x1a0, 12 buttons at
// +0x1a4), controller codes -0x2e..-1 for axis directions, D-pad directions and buttons, and the menus, dead zones
// and analogue movement on top of them. A PC player rarely had such a joystick, and the default bindings
// (0x00588ed8) name none. So:
//   - after the game's own poll (hook point g_padPoll, src/input/input_01.cpp) the SDL gamepad's state is written into
//     that block in the game's units, in the standard layout (positions, so the same for PlayStation and Xbox pads);
//   - a third binding column (hook point g_padBinding) gives every action controller codes next to the keyboard and
//     mouse bindings, which stay as they are.
#include <windows.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "log.h"
#include "settings.h"

extern int32_t (*g_padBinding)(int32_t action, int slot);  // src/input/input_01.cpp
extern void (*g_padPoll)(void* pc);
unsigned PresentedFrames();  // hook/src/d3d9_hooks.cpp

namespace
{
// Controller codes (InputPC 0x004f8b20 / 0x004f9560): buttons -1..-12 (button i = -1 - i); D-pad right -0x24, left
// -0x23, down -0x22, up -0x21 (POV ranges, PovRanges); axes X+ -0x29, X- -0x28, Y- -0x27, Y+ -0x26 (left stick),
// Z+ -0x2e, Z- -0x2d (right stick x), R- -0x2c, R+ -0x2b (right stick y). Y and R grow downwards (DirectInput).
constexpr int32_t Button(int i) { return -1 - i; }
enum : int32_t
{
    kPadRight = -0x24, kPadLeft = -0x23, kPadDown = -0x22, kPadUp = -0x21,
    kLeftXPlus = -0x29, kLeftXMinus = -0x28, kLeftYMinus = -0x27, kLeftYPlus = -0x26,
    kRightXPlus = -0x2e, kRightXMinus = -0x2d, kRightYMinus = -0x2c, kRightYPlus = -0x2b,
};
// The game's 12 buttons in standard-layout positions.
enum
{
    kSouth, kEast, kWest, kNorth,  // Cross Circle Square Triangle / A B X Y
    kL1, kR1, kL2, kR2,           // shoulders, triggers (as buttons past 30 %)
    kBack, kStart,                // Share/Create/View, Options/Menu
    kL3, kR3,                     // stick clicks
    kButtons
};

// Default controller bindings per action (names: 0x004155a0), two slots each.
struct PadDefault
{
    int32_t action, a, b;
};
const PadDefault kDefaults[] = {
    {0, kLeftYMinus, 0},                    // MoveForward
    {1, kLeftYPlus, 0},                     // MoveBackward
    {2, kLeftXMinus, 0},                    // MoveLeft
    {3, kLeftXPlus, 0},                     // MoveRight
    {4, kRightXMinus, 0},                   // TurnLeft
    {5, kRightXPlus, 0},                    // TurnRight
    {6, kRightYMinus, 0},                   // LookUp
    {7, kRightYPlus, 0},                    // LookDown
    {8, Button(kR2), 0},                    // Fire
    {9, Button(kL2), 0},                    // FireSpecialWeapon
    {10, Button(kR3), 0},                   // ToggleZoom
    {12, Button(kWest), 0},                 // Reload
    {13, Button(kSouth), 0},                // TurboJump
    {14, Button(kR1), 0},                   // IncWeapon
    {15, Button(kL1), 0},                   // DecWeapon
    {16, kPadRight, 0},                     // IncSpecialWeapon
    {17, kPadLeft, 0},                      // DecSpecialWeapon
    {18, kPadUp, 0},                        // TeammatesSend
    {19, kPadDown, 0},                      // TeammatesReturn
    {20, Button(kNorth), 0},                // Use
    {21, Button(kStart), 0},                // NRMStart
    {22, Button(kStart), 0},                // NRMEscape
    {23, kPadUp, kLeftYMinus},              // MenuUp
    {24, kPadDown, kLeftYPlus},             // MenuDown
    {25, kPadLeft, kLeftXMinus},            // MenuLeft
    {26, kPadRight, kLeftXPlus},            // MenuRight
    {27, Button(kSouth), 0},                // MenuSelect
    {28, Button(kEast), 0},                 // MenuCancel
    {31, Button(kL3), 0},                   // FastMovement
    {34, Button(kBack), 0},                 // SpecialEvent
};
int32_t g_bindings[35][2];

bool g_enabled, g_started, g_failed;
SDL_Gamepad* g_pad;
uint8_t g_last[0x34];  // the state we wrote last frame (the "previous" state the game compares with)
float g_lastAxes[4];
bool g_wrote;

// [test] pad_keys: a scripted virtual gamepad.
struct PadKey
{
    unsigned at, hold;
    int button;  // SDL_GamepadButton, or -1
    int axis;    // SDL_GamepadAxis when button < 0
    int16_t value;
};
PadKey g_script[64];
int g_scriptCount;
SDL_JoystickID g_virtual;
SDL_Joystick* g_virtualJoy;

void ParseScript()
{
    char buf[1024];
    ConfigString("test", "pad_keys", "", buf, sizeof(buf));
    struct Name
    {
        const char* name;
        int button, axis;
        int16_t value;
    };
    static const Name kNames[] = {
        {"SOUTH", SDL_GAMEPAD_BUTTON_SOUTH, 0, 0}, {"EAST", SDL_GAMEPAD_BUTTON_EAST, 0, 0},
        {"WEST", SDL_GAMEPAD_BUTTON_WEST, 0, 0}, {"NORTH", SDL_GAMEPAD_BUTTON_NORTH, 0, 0},
        {"L1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0, 0}, {"R1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0, 0},
        {"BACK", SDL_GAMEPAD_BUTTON_BACK, 0, 0}, {"START", SDL_GAMEPAD_BUTTON_START, 0, 0},
        {"L3", SDL_GAMEPAD_BUTTON_LEFT_STICK, 0, 0}, {"R3", SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0, 0},
        {"UP", SDL_GAMEPAD_BUTTON_DPAD_UP, 0, 0}, {"DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0, 0},
        {"LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0, 0}, {"RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0, 0},
        {"L2", -1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767}, {"R2", -1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767},
        {"LX+", -1, SDL_GAMEPAD_AXIS_LEFTX, 32767}, {"LX-", -1, SDL_GAMEPAD_AXIS_LEFTX, -32768},
        {"LY+", -1, SDL_GAMEPAD_AXIS_LEFTY, 32767}, {"LY-", -1, SDL_GAMEPAD_AXIS_LEFTY, -32768},
        {"RX+", -1, SDL_GAMEPAD_AXIS_RIGHTX, 32767}, {"RX-", -1, SDL_GAMEPAD_AXIS_RIGHTX, -32768},
        {"RY+", -1, SDL_GAMEPAD_AXIS_RIGHTY, 32767}, {"RY-", -1, SDL_GAMEPAD_AXIS_RIGHTY, -32768},
    };
    for (char* tok = std::strtok(buf, ","); tok && g_scriptCount < 64; tok = std::strtok(nullptr, ","))
    {
        char* colon = std::strchr(tok, ':');
        if (!colon)
            continue;
        *colon = 0;
        PadKey k{static_cast<unsigned>(std::atoi(tok)), 3, -1, 0, 0};
        char* star = std::strchr(colon + 1, '*');
        if (star)
            *star = 0, k.hold = static_cast<unsigned>(std::atoi(star + 1));
        for (const Name& n : kNames)
            if (!_stricmp(n.name, colon + 1))
                k.button = n.button, k.axis = n.axis, k.value = n.value;
        if (k.button >= 0 || k.value)
            g_script[g_scriptCount++] = k;
        else
            Log("gamepad: unknown pad key '%s'", colon + 1);
    }
}

// The test pad: an SDL virtual gamepad with the standard buttons and axes.
void StartVirtual()
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "and test pad";
    g_virtual = SDL_AttachVirtualJoystick(&desc);
    g_virtualJoy = g_virtual ? SDL_OpenJoystick(g_virtual) : nullptr;
    Log("gamepad: test pad %s, %d scripted inputs", g_virtualJoy ? "attached" : SDL_GetError(), g_scriptCount);
}

void RunScript()
{
    if (!g_virtualJoy)
        return;
    const unsigned f = PresentedFrames();
    bool buttons[SDL_GAMEPAD_BUTTON_COUNT] = {};
    int16_t axes[SDL_GAMEPAD_AXIS_COUNT] = {};
    // A virtual joystick's raw axes span -32768..32767 and the gamepad mapping rescales triggers from that range, so
    // a trigger at rest is -32768 (0 read as half pressed, and the fire action never released).
    axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = -32768;
    for (int i = 0; i < g_scriptCount; ++i)
    {
        const PadKey& k = g_script[i];
        if (f < k.at || f >= k.at + k.hold)
            continue;
        if (k.button >= 0)
            buttons[k.button] = true;
        else
            axes[k.axis] = k.value;
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
        SDL_SetJoystickVirtualButton(g_virtualJoy, b, buttons[b]);
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
        SDL_SetJoystickVirtualAxis(g_virtualJoy, a, axes[a]);
}

int32_t Binding(int32_t action, int slot)
{
    if (!g_enabled || !g_pad || action < 0 || action >= 35)
        return 0;
    return g_bindings[action][slot & 1];
}

bool Start()
{
    if (g_started)
        return !g_failed;
    g_started = true;
    // No SDL window: read the pads whatever has the focus (the poll below checks the game window instead), and let
    // SDL's own thread pump the raw input / HID messages.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD))
    {
        g_failed = true;
        Log("gamepad: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    int v = SDL_GetVersion();
    Log("gamepad: SDL %d.%d.%d ready", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    ParseScript();
    if (g_scriptCount)
        StartVirtual();
    return true;
}

// Opens the first gamepad when none is open (or the open one went away).
void Connect()
{
    if (g_pad && SDL_GamepadConnected(g_pad))
        return;
    if (g_pad)
    {
        Log("gamepad: %s disconnected", SDL_GetGamepadName(g_pad));
        SDL_CloseGamepad(g_pad);
        g_pad = nullptr;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0)
    {
        g_pad = SDL_OpenGamepad(g_virtual ? g_virtual : ids[0]);  // a test script uses its virtual pad
        if (g_pad)
            Log("gamepad: %s connected (type %d)", SDL_GetGamepadName(g_pad), int(SDL_GetGamepadType(g_pad)));
    }
    SDL_free(ids);
}

// The game's per-frame controller poll is done; write the SDL pad into its controller state (src/input/input_01.cpp
// InputPC_PollController layout).
void Poll(void* pc)
{
    if (!g_enabled || !Start())
        return;
    RunScript();
    SDL_PumpEvents();  // hot-plug events
    SDL_UpdateGamepads();
    Connect();
    if (!g_pad)
    {
        if (g_wrote)  // leave a neutral state behind once, then let a DirectInput joystick (if any) be
        {
            std::memset(static_cast<uint8_t*>(pc) + 0x190, 0, 0x34);
            Field<int32_t>(pc, 0x1a0) = -1;
            for (int i = 0; i < 4; ++i)
                Field<float>(pc, 0x1f8 + 4 * i) = 0.0f;
            g_wrote = false;
        }
        return;
    }
    // Only while the game is in front (SDL reads the pad in the background).
    HWND hwnd = g_game ? Field<HWND>(g_game, 0x64) : nullptr;
    const bool front = (hwnd && GetForegroundWindow() == hwnd) || g_virtualJoy;

    uint8_t state[0x34] = {};
    int32_t* axis = reinterpret_cast<int32_t*>(state);
    const SDL_GamepadAxis axes[4] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
                                     SDL_GAMEPAD_AXIS_RIGHTY};
    for (int i = 0; i < 4; ++i)
    {
        // The game recentres DirectInput's 0..65535 by -32255 and scales by 1 / 32255 (0x005bcb6c): +-32255 is full.
        int32_t v = front ? SDL_GetGamepadAxis(g_pad, axes[i]) : 0;
        axis[i] = v < -32255 ? -32255 : v > 32255 ? 32255 : v;
    }
    // D-pad as a POV (hundredths of degrees clockwise from up, -1 centred).
    const bool up = front && SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    const bool down = front && SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool left = front && SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    const bool right = front && SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    int32_t pov = -1;
    if (up && right) pov = 4500;
    else if (down && right) pov = 13500;
    else if (down && left) pov = 22500;
    else if (up && left) pov = 31500;
    else if (up) pov = 0;
    else if (right) pov = 9000;
    else if (down) pov = 18000;
    else if (left) pov = 27000;
    std::memcpy(state + 0x10, &pov, 4);
    const SDL_GamepadButton buttons[kButtons] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
        SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
    for (int i = 0; i < kButtons; ++i)
    {
        bool down2 = false;
        if (i == kL2 || i == kR2)
            down2 = SDL_GetGamepadAxis(g_pad, i == kL2 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >
                    32767 * 3 / 10;
        else
            down2 = SDL_GetGamepadButton(g_pad, buttons[i]);
        state[0x14 + i] = front && down2 ? 0x80 : 0;
    }

    // As 0x004f8600: previous state and axes, then the new ones. The previous values are ours from last frame (a
    // DirectInput joystick, if the game found one, was polled just before and is overridden).
    uint8_t* base = static_cast<uint8_t*>(pc);
    std::memcpy(base + 0x1c4, g_wrote ? g_last : state, 0x34);
    std::memcpy(base + 0x190, state, 0x34);
    const float k = GameGlobal<float>(0x005bcb6c);
    for (int i = 0; i < 4; ++i)
    {
        const float f = static_cast<float>(axis[i]) * k;
        Field<float>(pc, 0x208 + 4 * i) = g_wrote ? g_lastAxes[i] : f;
        Field<float>(pc, 0x1f8 + 4 * i) = f;
        g_lastAxes[i] = f;
    }
    std::memcpy(g_last, state, sizeof(g_last));
    g_wrote = true;
}
}  // namespace

void GamepadInit()
{
    using S = RemasterSetting;
    g_enabled = RemasterRegister({"input", "gamepad", L"Controller (DualShock, DualSense, Xbox)", S::Toggle, 0, 1, 1,
                                  {}, 0, false, [](float v) { g_enabled = v != 0.0f; }},
                                 0, 1) != 0.0f;
    for (const PadDefault& d : kDefaults)
        g_bindings[d.action][0] = d.a, g_bindings[d.action][1] = d.b;
    g_padBinding = Binding;
    g_padPoll = Poll;  // returns at once while off: SDL is started on the first poll with the setting on
    Log("gamepad: %s", g_enabled ? "on" : "off");
}
