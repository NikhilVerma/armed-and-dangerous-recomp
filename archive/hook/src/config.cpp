#include "config.h"

#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

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

const char* ConfigIniPath()
{
    return IniPath();
}

// Values are read once and cached: Windows serves GetPrivateProfile* through the registry's INI mapping, and a few
// settings are read per draw (a 4K walk spent ~5% of its CPU samples in NtOpenKeyEx / ZwQueryValueKey). Writers call
// ConfigForget() after WritePrivateProfileString.
namespace
{
const char* const kAbsent = "";  // GetPrivateProfileString default: the key is missing
std::mutex g_cacheLock;
std::unordered_map<std::string, std::string> g_cache;  // "section|key" -> value, or kAbsent

// The raw value, or nullptr when the key is absent. The pointer stays valid until ConfigForget().
const std::string* Raw(const char* section, const char* key)
{
    std::string k = std::string(section) + '|' + key;
    std::lock_guard<std::mutex> g(g_cacheLock);
    auto it = g_cache.find(k);
    if (it == g_cache.end())
    {
        char buf[1024];
        GetPrivateProfileStringA(section, key, kAbsent, buf, sizeof(buf), IniPath());
        it = g_cache.emplace(std::move(k), buf).first;
    }
    return it->second == kAbsent ? nullptr : &it->second;
}
}  // namespace

void ConfigForget()
{
    std::lock_guard<std::mutex> g(g_cacheLock);
    g_cache.clear();
}

int ConfigInt(const char* section, const char* key, int fallback)
{
    const std::string* v = Raw(section, key);
    return v ? static_cast<int>(strtol(v->c_str(), nullptr, 10)) : fallback;  // as GetPrivateProfileInt
}

float ConfigFloat(const char* section, const char* key, float fallback)
{
    const std::string* v = Raw(section, key);
    return v && !v->empty() ? static_cast<float>(atof(v->c_str())) : fallback;
}

void ConfigString(const char* section, const char* key, const char* fallback, char* out, unsigned size)
{
    const std::string* v = Raw(section, key);
    const char* s = v ? v->c_str() : fallback;
    if (size)
        strncpy_s(out, size, s, _TRUNCATE);
}

bool ConfigOriginalDefaults()
{
    static const bool original = ConfigInt("remaster", "original_defaults", 0) != 0;
    return original;
}

int ConfigIntDefault(const char* section, const char* key, int original, int remaster)
{
    return ConfigInt(section, key, ConfigOriginalDefaults() ? original : remaster);
}

float ConfigFloatDefault(const char* section, const char* key, float original, float remaster)
{
    return ConfigFloat(section, key, ConfigOriginalDefaults() ? original : remaster);
}
