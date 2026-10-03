#include "config.h"

#include <windows.h>
#include <cstdlib>

static const char* IniPath()
{
    static char path[MAX_PATH];
    if (!path[0])
    {
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        strcpy(slash ? slash + 1 : path, "and.ini");
    }
    return path;
}

int ConfigInt(const char* section, const char* key, int fallback)
{
    return GetPrivateProfileIntA(section, key, fallback, IniPath());
}

float ConfigFloat(const char* section, const char* key, float fallback)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), IniPath());
    return buf[0] ? static_cast<float>(atof(buf)) : fallback;
}
