// The embedded FidelityFX files (rthelper/CMakeLists.txt writes ffx_embed.inc, one array per file, and
// ffx_table.inc, their names).
#include <cstring>
#include <string>

#include "shadow_denoiser.h"

namespace rth
{
namespace
{
#include "ffx_embed.inc"
struct File
{
    const char* name;
    const char* text;
};
const File kFiles[] = {
#include "ffx_table.inc"
};
}  // namespace

const char* FfxSource(const std::string& name)
{
    for (const File& f : kFiles)
        if (name == f.name)
            return f.text;
    return nullptr;
}
}  // namespace rth
