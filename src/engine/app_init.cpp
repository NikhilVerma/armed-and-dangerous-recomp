// Recovered: App_Init, 0x004e3fc0 (S3, Approach B). Faithful to the original; see docs/decision.md.
#include "app.h"

#include <cstring>

#include "addresses.h"
#include "hooks.h"

// Starts the frame timer, completes the start-up configuration with defaults, creates two shared objects,
// runs the virtual setup (renderer and other subsystems), then seeds the game's rand() with GetTickCount.
void __fastcall App_Init(App* self, void*, const AppConfig* config, uint32_t arg2, uint32_t arg3)
{
    orig::Timer_Init();
    if (orig::g_timerInitCount == 0)
        orig::g_timerStartSeconds = static_cast<float>(orig::Timer_Seconds());
    orig::g_timerInitCount++;

    AppConfig cfg;
    if (config)
        cfg = *config;
    else
        memset(&cfg, 0, sizeof(cfg));

    if (!(cfg.present & 1))
    {
        cfg.present |= 1;
        cfg.displayFlags |= 3;
        cfg.width = 640;
        cfg.height = 480;
        cfg.bpp = 32;
    }
    if (!(cfg.present & 4))
    {
        cfg.present |= 4;
        cfg.enable_1c = 1;
        cfg.field_24 = 0x3e9c0ec1;  // float 0.30479
        cfg.field_20 = 10;
    }
    if (!(cfg.present & 2))
    {
        cfg.present |= 2;
        cfg.field_54 = 0;
        cfg.enable_4c = 1;
        cfg.field_50 = 10;
    }

    self->field_08 = cfg.field_48;
    self->displayFlags = cfg.displayFlags;
    self->field_0c = cfg.field_14;
    self->field_10 = cfg.field_18;
    self->object_34 = orig::Singleton_004e43f0();
    self->object_60 = orig::Singleton_004fbab0();

    self->vtbl->Setup(self, nullptr, &cfg, arg2, arg3);

    orig::g_startTickCount = orig::GetTickCount();
    orig::srand(orig::g_startTickCount);
}

HOOK(0x004e3fc0, "App_Init", App_Init, nullptr);
