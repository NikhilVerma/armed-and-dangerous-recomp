// Recovered: renderer core, Direct3D 9 layer (device reset/restore, texture bucket release, disk textures).
// S2, Approach B.
//   0x00508210 Renderer_ShowErrorBox
//   0x00509040 Renderer_SetDisplayMode
//   0x00509170 Renderer_ReleaseD3D
//   0x005093b0 Renderer_RestoreTextures
//   0x00509560 Renderer_ChangeDisplayMode
//   0x00509620 Renderer_ResetDevice
//   0x005097c0 Renderer_ReleaseAllTextures
//   0x00509890 Renderer_ReleaseTextureResources
//   0x00509920 Renderer_ReleaseDevice
//   0x005099b0 Renderer_RestoreDevice
//   0x0050b8a0 Bitmap_LoadTgaFile
//   0x0050b9e0 Texture_LoadFromDisk
//   0x0050bae0 Texture_UnloadFromDisk
//   0x0050bc50 TextureBucket_ReleaseD3D
//   0x0050bcc0 TextureBucket_Release
//   0x0050be40 Surface_LoadFromSurface
//   0x0050bfe0 Texture_TouchMipLevels
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
const char* Str(uint32_t address)
{
    return reinterpret_cast<const char*>(address);
}

constexpr uint32_t kGameHeap = 0x0063a210;  // ecx of the game's allocator (0x004e07f0) and free (0x004e08d0)

constexpr uint32_t kStrGenericError = 0x005895f0;     // "Generic application error 0x%X.\n\nEnable debug ..."
constexpr uint32_t kStrGfxDx7 = 0x005895e8;           // "GFX_DX7"
constexpr uint32_t kStrRestoreTextures = 0x0058952c;  // "RestoreTextures"
constexpr uint32_t kStrDot = 0x0058b1d8;              // "."
constexpr uint32_t kStrCouldNotReset = 0x0058950c;    // "Could not reset Direct3DDevice"
constexpr uint32_t kStrError = 0x0058e504;            // "Error"
constexpr uint32_t kStrCantRestore = 0x005894e4;      // "Fatal error: can't restore resources"
constexpr uint32_t kStrNewArray = 0x0058b5e0;         // "new[]"
constexpr uint32_t kStrNew = 0x0058b5e8;              // "new"
constexpr uint32_t kStrLoadedTexture = 0x005893a0;    // "Loaded texture \"%s\" from disk\n"
constexpr uint32_t kStrFailedTexture = 0x00589378;    // "Failed to load texture \"%s\" from disk \n"
constexpr uint32_t kStrD3DXLoadSurfaceFromSurface = 0x0058933c;
constexpr uint32_t kStrDXError = 0x0058b33c;          // "Error %s in DX function %s\n"

// Imports, called through the game's import table so the hook DLL's import hooks still apply.
LPSTR IatLstrcatA(LPSTR a, LPCSTR b)
{
    return (*reinterpret_cast<LPSTR(WINAPI**)(LPSTR, LPCSTR)>(0x0056b0e4))(a, b);
}
int IatMessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type)
{
    return (*reinterpret_cast<int(WINAPI**)(HWND, LPCSTR, LPCSTR, UINT)>(0x0056b220))(w, text, caption, type);
}
BOOL IatSetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    return (*reinterpret_cast<BOOL(WINAPI**)(HWND, HWND, int, int, int, int, UINT)>(0x0056b238))(w, after, x, y, cx,
                                                                                                cy, flags);
}
DWORD IatSetFilePointer(HANDLE h, LONG distance, PLONG high, DWORD method)
{
    return (*reinterpret_cast<DWORD(WINAPI**)(HANDLE, LONG, PLONG, DWORD)>(0x0056b0d0))(h, distance, high, method);
}
BOOL IatReadFile(HANDLE h, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
    return (*reinterpret_cast<BOOL(WINAPI**)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)>(0x0056b0c8))(
        h, buffer, size, read, overlapped);
}
BOOL IatCloseHandle(HANDLE h)
{
    return (*reinterpret_cast<BOOL(WINAPI**)(HANDLE)>(0x0056b084))(h);
}

uint32_t GameFree(uint32_t p)
{
    return orig::fn_004e08d0(kGameHeap, p);
}

// The used texture buckets (+0x494) as the original walks them: the next node is read before the current one is
// processed, and the head means the end.
ListNode* UsedBuckets(Renderer* r)
{
    return &Field<ListNode>(r, 0x494);
}
}  // namespace

// Recovered elsewhere.
uint32_t Renderer_SetupPresentParams(uint32_t width, uint32_t height, uint32_t bpp, Renderer* r,
                                     uint32_t windowed);                       // auto_00506210.cpp
uint32_t Renderer_CreateDevice(Renderer* r, uint32_t /*unused*/);              // auto_00506210.cpp
uint32_t Renderer_InitDeviceState(Renderer* r);                                // loader/auto_00502cb0.cpp
uint32_t Tga_Decode(uint32_t* out, const uint8_t* tga, uint32_t size);         // auto_0050ab40.cpp
void Bitmap_ComputeSteps(uint32_t* bmp, uint32_t rowBytes);                    // loader/auto_004ef270.cpp

// Defined below.
uint32_t TextureBucket_ReleaseD3D(uint8_t* bucket, Renderer* r, uint32_t notifyLost);
uint32_t TextureBucket_Release(uint32_t notifyLost, uint8_t* bucket, Renderer* r);
uint32_t Renderer_ResetDevice(Renderer* r);
uint32_t Renderer_ReleaseAllTextures(Renderer* r);
uint32_t Renderer_ReleaseTextureResources(Renderer* r);
uint32_t Renderer_ReleaseDevice(Renderer* r);
void Renderer_RestoreTextures(Renderer* r, uint32_t showProgress);

// 0x00508210 (esi = extra text or null; [renderer (unused), error code, unused], callee pops 12): format
// "Generic application error 0x%X..." (0x005895f0) with the code into a 512-byte buffer (sprintf at 0x0053814f),
// append the extra text (lstrcatA), log it (0x004e0b30, the buffer is the format) and show it in a MessageBoxA
// captioned "GFX_DX7" with MB_ICONHAND (0x10). Returns MessageBoxA's result. Caller: 0x0050b040 on a GetDesc
// failure. (not executed in the comparison scenarios)
int32_t Renderer_ShowErrorBox(const char* extra, Renderer* /*r*/, uint32_t code, uint32_t /*unused*/)
{
    char text[0x200];
    orig::fn_0053814f(u(text), kStrGenericError, code);
    if (extra)
        IatLstrcatA(text, extra);
    GameLog(text);
    return IatMessageBoxA(nullptr, text, Str(kStrGfxDx7), 0x10);
}

// 0x00509040 (eax = width, ecx = height, edx = bpp, edi = windowed request; [renderer, apply], callee pops 8):
// with apply 0 and no width or height, return 1 and change nothing. Otherwise default 800x600x32 for zero values,
// store them at +0x14d0/+0x14d4/+0x14d8 and +0x1634/+0x1638/+0x163c, the request in +0x14dc bit 0 (the bit
// Renderer_SetupPresentParams reads as its windowed request). With a request and apply, set the device window
// (+0x1484) from +0x9b0 and call 0x005080d0(1, 0, 0). Returns al = 0; the upper bits are what EAX held.
// Caller: Renderer_Init.
uint32_t Renderer_SetDisplayMode(uint32_t width, uint32_t height, uint32_t bpp, uint32_t windowed, Renderer* r,
                                 uint32_t apply)
{
    if (apply == 0)
    {
        if (width == 0)
        {
            if (height == 0)
                return 1;
            width = 800;
        }
    }
    else if (width == 0)
    {
        width = 800;
    }
    if (height == 0)
        height = 600;
    if (bpp == 0)
        bpp = 32;
    Field<uint32_t>(r, 0x14d0) = width;
    Field<uint32_t>(r, 0x1634) = width;
    Field<uint32_t>(r, 0x14d4) = height;
    Field<uint32_t>(r, 0x1638) = height;
    uint8_t& opts = Field<uint8_t>(r, 0x14dc);
    uint8_t request = windowed != 0 ? 1 : 0;
    Field<uint32_t>(r, 0x14d8) = bpp;
    Field<uint32_t>(r, 0x163c) = bpp;
    opts = static_cast<uint8_t>(opts ^ ((request ^ opts) & 1));
    uint32_t result = width;
    if (windowed != 0 && apply != 0)
    {
        r->deviceWindow = Field<HWND>(r, 0x9b0);
        result = orig::fn_005080d0(u(r), 1, 0, 0);
    }
    return result & 0xffffff00;
}

// 0x00509170 (eax = renderer): release everything Direct3D: all texture buckets (Renderer_ReleaseAllTextures),
// the device and its resources (Renderer_ReleaseDevice), IDirect3D9 (+0x14c8, Release), then free the block at
// +0x49c with the game's heap (0x004e08d0). Returns al = 1. Caller: 0x004ffd40. (not executed in the comparison
// scenarios)
uint32_t Renderer_ReleaseD3D(Renderer* r)
{
    Renderer_ReleaseAllTextures(r);
    Renderer_ReleaseDevice(r);
    if (r->d3d)
    {
        r->d3d->Release();
        r->d3d = nullptr;
    }
    uint32_t result = 0;
    if (uint32_t block = Field<uint32_t>(r, 0x49c))
    {
        result = GameFree(block);
        Field<uint32_t>(r, 0x49c) = 0;
    }
    return (result & 0xffffff00) | 1;
}

// 0x005093b0 ([renderer, show progress], stdcall): re-create every used bucket's texture (+0x494 list, texture
// record at bucket +8): clear the record's byte +0x21 and call the renderer's virtual 73 (+0x124,
// Renderer_RegisterTexture). With show progress set, before each texture the renderer's virtuals 61 (+0xf4, with
// 0), 58 (+0xe8), 59 (+0xec) and 66 (+0x108) run, and a local "RestoreTextures" string (0x0058952c) gets a "."
// (0x0058b1d8) per 1.0 (0x005bcb2c) of progress, 40.0 (0x005bcb30) spread over the textures. The string is never
// used (a compiled-out debug print); it is kept to mirror the original. Caller: Renderer_RestoreDevice.
// (not executed in the comparison scenarios)
void Renderer_RestoreTextures(Renderer* r, uint32_t showProgress)
{
    char progress[0xa0];
    std::memcpy(progress, Str(kStrRestoreTextures), 16);
    ListNode* head = UsedBuckets(r);
    int32_t count = 0;
    if (showProgress != 0)
    {
        ListNode* next = head->next->next;
        for (ListNode* n = head->next == head ? nullptr : head->next; n;)
        {
            if (Field<uint32_t>(n, 8) != 0)
                ++count;
            ListNode* cur = next;
            next = next->next;
            n = cur == head ? nullptr : cur;
        }
    }
    const float total = GameGlobal<float>(0x005bcb30);  // 40.0
    const float step = GameGlobal<float>(0x005bcb2c);   // 1.0
    float done = 0.0f;
    ListNode* next = head->next->next;
    for (ListNode* n = head->next == head ? nullptr : head->next; n;)
    {
        if (Field<uint8_t*>(n, 8) != nullptr)
        {
            if (showProgress != 0)
            {
                done = total / static_cast<float>(count) + done;
                while (done > step)
                {
                    done -= step;
                    std::memcpy(progress + std::strlen(progress), Str(kStrDot), 2);
                }
                VCall<uint32_t>(r, 0xf4 / 4, 0u);
                VCall<uint32_t>(r, 0xe8 / 4);
                VCall<uint32_t>(r, 0xec / 4);
                VCall<uint32_t>(r, 0x108 / 4);
            }
            Field<uint8_t*>(n, 8)[0x21] = 0;
            VCall<uint32_t>(r, 0x124 / 4, Field<uint8_t*>(n, 8));
        }
        ListNode* cur = next;
        next = next->next;
        n = cur == head ? nullptr : cur;
    }
}

// 0x00509560 (ecx = renderer; [width, height, bpp, windowed], callee pops 16): switch display mode: remember the
// current mode (+0x14d0..+0x14dc), set up the present parameters for the new one (Renderer_SetupPresentParams)
// and reset the device (Renderer_ResetDevice). If that fails, go back to the old mode (request = old +0x14dc &
// 0xffffff01) and reset; if that fails too, 640x480x16 with the old request; if that fails, MessageBoxA
// "Could not reset Direct3DDevice" / "Error" on the device window and exit(-1) (0x0053a117). Returns al = 1 when
// the first reset succeeded, else al = 0. No direct callers (virtual). (not executed in the comparison scenarios)
uint32_t Renderer_ChangeDisplayMode(Renderer* r, uint32_t width, uint32_t height, uint32_t bpp, uint32_t windowed)
{
    uint32_t oldWidth = Field<uint32_t>(r, 0x14d0);
    uint32_t oldHeight = Field<uint32_t>(r, 0x14d4);
    uint32_t oldBpp = Field<uint32_t>(r, 0x14d8);
    uint32_t oldRequest = Field<uint32_t>(r, 0x14dc);
    Renderer_SetupPresentParams(width, height, bpp, r, windowed);
    uint32_t result = Renderer_ResetDevice(r);
    if (result & 0xff)
        return (result & 0xffffff00) | 1;
    oldRequest &= 0xffffff01;
    Renderer_SetupPresentParams(oldWidth, oldHeight, oldBpp, r, oldRequest);
    result = Renderer_ResetDevice(r);
    if (!(result & 0xff))
    {
        Renderer_SetupPresentParams(0x280, 0x1e0, 0x10, r, oldRequest);
        result = Renderer_ResetDevice(r);
        if (!(result & 0xff))
        {
            IatMessageBoxA(r->deviceWindow, Str(kStrCouldNotReset), Str(kStrError), 0);
            GameFunction<void(__cdecl*)(int)>(0x0053a117)(-1);  // exit
            result = 0;
        }
    }
    return result & 0xffffff00;
}

// 0x00509620 (eax = renderer): reset the device after a loss. Runs the "lost" callback (+0x9b4, cdecl, with the
// user value +0x9bc), releases the COM objects at +0x3d0 and +0x3d4, releases every texture bucket
// (Renderer_ReleaseAllTextures), calls 0x004fe7d0, clears +0x34c/+0x350, frees the render pass blocks +0x358
// (also clearing +0x35c) and +0x354, calls virtual 10 (+0x28), then IDirect3DDevice9::Reset with the present
// parameters (+0x1490). Reset failed: returns 0. Reset succeeded: with no device or under 12 MB (0xc00000) of
// GetAvailableTextureMem returns al = 0; otherwise Renderer_InitDeviceState, 0x004fe870, 0x005080d0(1, 0, 0), the
// "reset" callback (+0x9b8, with +0x9bc), virtual 11 (+0x2c), and returns 1. Callers: Renderer_CheckDevice,
// Renderer_ChangeDisplayMode. (not executed in the comparison scenarios)
uint32_t Renderer_ResetDevice(Renderer* r)
{
    if (auto lost = Field<void(__cdecl*)(uint32_t)>(r, 0x9b4))
        lost(Field<uint32_t>(r, 0x9bc));
    if (auto p = Field<IUnknown*>(r, 0x3d0))
    {
        p->Release();
        Field<IUnknown*>(r, 0x3d0) = nullptr;
    }
    if (auto p = Field<IUnknown*>(r, 0x3d4))
    {
        p->Release();
        Field<IUnknown*>(r, 0x3d4) = nullptr;
    }
    Renderer_ReleaseAllTextures(r);
    orig::fn_004fe7d0(u(r));
    Field<uint32_t>(r, 0x350) = 0;
    Field<uint32_t>(r, 0x34c) = 0;
    if (uint32_t passes = Field<uint32_t>(r, 0x358))
    {
        GameFree(passes);
        Field<uint32_t>(r, 0x35c) = 0;
        Field<uint32_t>(r, 0x358) = 0;
    }
    if (uint32_t tree = Field<uint32_t>(r, 0x354))
    {
        GameFree(tree);
        Field<uint32_t>(r, 0x354) = 0;
    }
    VCall<uint32_t>(r, 0x28 / 4);
    HRESULT hr = r->device->Reset(&r->presentParams);
    if (hr != 0)
        return 0;
    if (!r->device)
        return 0;
    uint32_t memory = r->device->GetAvailableTextureMem();
    if (memory < 0xc00000)
        return memory & 0xffffff00;
    Renderer_InitDeviceState(r);
    orig::fn_004fe870(u(r));
    orig::fn_005080d0(u(r), 1, 0, 0);
    if (auto reset = Field<void(__cdecl*)(uint32_t)>(r, 0x9b8))
        reset(Field<uint32_t>(r, 0x9bc));
    VCall<uint32_t>(r, 0x2c / 4);
    return 1;
}

// 0x0050bc50 (esi = bucket, edi = renderer; [notify lost], callee pops 4): free a texture bucket's Direct3D
// objects. If it has a texture (+0x0c): renderer virtual 11 (+0x2c), virtual 10 (+0x28) too when the low byte of
// notify lost is set, Release +0x18 and +0x0c, clear +0x0c..+0x18. Then clear the record's created bit (+8 ->
// byte +0x23 bit 7), subtract the bucket's size (+0x20) from the renderer's texture memory total (+0x4a8) and
// clear it. Returns the size. Caller: TextureBucket_Release.
uint32_t TextureBucket_ReleaseD3D(uint8_t* bucket, Renderer* r, uint32_t notifyLost)
{
    if (Field<uint32_t>(bucket, 0xc) != 0)
    {
        VCall<uint32_t>(r, 0x2c / 4);
        if (static_cast<uint8_t>(notifyLost) != 0)
            VCall<uint32_t>(r, 0x28 / 4);
        if (auto p = Field<IUnknown*>(bucket, 0x18))
        {
            p->Release();
            Field<IUnknown*>(bucket, 0x18) = nullptr;
        }
        if (auto p = Field<IUnknown*>(bucket, 0xc))
        {
            p->Release();
            Field<IUnknown*>(bucket, 0xc) = nullptr;
        }
        Field<uint32_t>(bucket, 0xc) = 0;
        Field<uint32_t>(bucket, 0x10) = 0;
        Field<uint32_t>(bucket, 0x14) = 0;
        Field<uint32_t>(bucket, 0x18) = 0;
    }
    if (uint8_t* record = Field<uint8_t*>(bucket, 8))
        record[0x23] &= 0x7f;
    uint32_t size = Field<uint32_t>(bucket, 0x20);
    Field<uint32_t>(r, 0x4a8) -= size;
    Field<uint32_t>(bucket, 0x20) = 0;
    return size;
}

// 0x0050bcc0 (eax = notify lost, ecx = bucket or null, edx = renderer): give a bucket back: free its Direct3D
// objects (TextureBucket_ReleaseD3D), detach the texture record (record +0x0c and bucket +8 cleared), unlink it
// and insert it at the head of the free list (+0x48c). Returns the bucket's new next node, or EAX unchanged for a
// null bucket. Callers: Renderer_CreateTexture, Renderer_RegisterTexture.
uint32_t TextureBucket_Release(uint32_t notifyLost, uint8_t* bucket, Renderer* r)
{
    if (!bucket)
        return notifyLost;
    TextureBucket_ReleaseD3D(bucket, r, notifyLost);
    if (uint8_t* record = Field<uint8_t*>(bucket, 8))
    {
        Field<uint32_t>(record, 0xc) = 0;
        Field<uint32_t>(bucket, 8) = 0;
    }
    auto* node = reinterpret_cast<ListNode*>(bucket);
    node->prev->next = node->next;
    node->next->prev = node->prev;
    ListNode* freeList = &Field<ListNode>(r, 0x48c);
    node->prev = freeList;
    node->next = freeList->next;
    freeList->next = node;
    node->next->prev = node;
    return u(node->next);
}

// 0x005097c0 (edi = renderer): give back every used bucket (+0x494): the original inlines
// TextureBucket_Release with notify lost 0 (virtual 11, Release +0x18/+0x0c, clear the created bit, subtract the
// size from +0x4a8, detach the record, move the bucket to the free list +0x48c). Callers: Renderer_ReleaseD3D,
// Renderer_ResetDevice. (not executed in the comparison scenarios)
uint32_t Renderer_ReleaseAllTextures(Renderer* r)
{
    ListNode* head = UsedBuckets(r);
    ListNode* next = head->next->next;
    for (ListNode* n = head->next == head ? nullptr : head->next; n;)
    {
        TextureBucket_Release(0, reinterpret_cast<uint8_t*>(n), r);
        ListNode* cur = next;
        next = next->next;
        n = cur == head ? nullptr : cur;
    }
    return u(head);  // EAX ends on the list head
}

// 0x00509890 (edi = renderer): free the Direct3D objects of every used bucket (+0x494) but keep the buckets and
// their records: the original inlines TextureBucket_ReleaseD3D with notify lost 0 for each. Caller:
// Renderer_ReleaseDevice. (not executed in the comparison scenarios)
uint32_t Renderer_ReleaseTextureResources(Renderer* r)
{
    ListNode* head = UsedBuckets(r);
    ListNode* next = head->next->next;
    for (ListNode* n = head->next == head ? nullptr : head->next; n;)
    {
        TextureBucket_ReleaseD3D(reinterpret_cast<uint8_t*>(n), r, 0);
        ListNode* cur = next;
        next = next->next;
        n = cur == head ? nullptr : cur;
    }
    return u(head);  // EAX ends on the list head
}

// 0x00509920 (eax = renderer): release the device: 0x00506f30, the buckets' Direct3D objects
// (Renderer_ReleaseTextureResources), clear +0x34c/+0x350, free the render pass blocks +0x358 (clearing +0x35c)
// and +0x354, 0x0050c900, 0x004fe7d0, then Release the device (+0x14cc). Returns 0. Callers: Renderer_ReleaseD3D,
// 0x0050ccf0. (not executed in the comparison scenarios)
uint32_t Renderer_ReleaseDevice(Renderer* r)
{
    orig::fn_00506f30(u(r));
    Renderer_ReleaseTextureResources(r);
    Field<uint32_t>(r, 0x350) = 0;
    Field<uint32_t>(r, 0x34c) = 0;
    if (uint32_t passes = Field<uint32_t>(r, 0x358))
    {
        GameFree(passes);
        Field<uint32_t>(r, 0x35c) = 0;
        Field<uint32_t>(r, 0x358) = 0;
    }
    if (uint32_t tree = Field<uint32_t>(r, 0x354))
    {
        GameFree(tree);
        Field<uint32_t>(r, 0x354) = 0;
    }
    orig::fn_0050c900(u(r));
    orig::fn_004fe7d0(u(r));
    if (r->device)
    {
        r->device->Release();
        r->device = nullptr;
    }
    return 0;
}

// 0x005099b0 (esi = renderer): re-create the device after Renderer_ReleaseDevice. Only with IDirect3D9 (+0x14c8):
// when windowed (+0x1628 bit 5) put the device window topmost (SetWindowPos HWND_TOPMOST, SWP_SHOWWINDOW) at the
// rectangle +0x9c0..+0x9cc; Renderer_CreateDevice with the window +0x9b0; on failure log "Fatal error: can't
// restore resources" and return the error. Then 0x004fe870, Renderer_RestoreTextures(show progress),
// Renderer_InitDeviceState and virtual 11 (+0x2c). Returns the CreateDevice result (0 without IDirect3D9).
// Caller: 0x0050ccf0. (not executed in the comparison scenarios)
int32_t Renderer_RestoreDevice(Renderer* r)
{
    int32_t result = 0;
    if (!r->d3d)
        return result;
    if (Field<uint8_t>(r, 0x1628) & 0x20)
    {
        int32_t left = Field<int32_t>(r, 0x9c0), top = Field<int32_t>(r, 0x9c4);
        IatSetWindowPos(r->deviceWindow, HWND_TOPMOST, left, top, Field<int32_t>(r, 0x9c8) - left,
                        Field<int32_t>(r, 0x9cc) - top, 0x40);
    }
    result = static_cast<int32_t>(Renderer_CreateDevice(r, Field<uint32_t>(r, 0x9b0)));
    if (result < 0)
    {
        GameLog(Str(kStrCantRestore));
        return result;
    }
    orig::fn_004fe870(u(r));
    Renderer_RestoreTextures(r, 1);
    Renderer_InitDeviceState(r);
    VCall<uint32_t>(r, 0x2c / 4);
    return result;
}

// 0x0050b8a0 ([path], cdecl): load a TGA file into a new bitmap. Opens the file with 0x004f1c90 (a 0x108-byte
// file object, HANDLE first), measures it with SetFilePointer, reads it whole into a "new[]" block, allocates a
// zeroed 0x20-byte bitmap ("new"), decodes it (Tga_Decode) and computes its steps (Bitmap_ComputeSteps). Closes the
// file and frees the file data. Returns the bitmap (null if its allocation failed).
// Original bugs, see the note on the return values: when the file cannot be opened the original returns an
// uninitialised stack slot (reproduced as null), and on a short read it returns the byte count read as if it were
// the bitmap (reproduced). Caller: Texture_LoadFromDisk.
uint32_t* Bitmap_LoadTgaFile(const char* path)
{
    alignas(4) uint8_t file[0x108];
    if ((orig::fn_004f1c90(u(file), u(path)) & 0xff) == 0)
        return nullptr;  // original: uninitialised local (cannot be reproduced)
    HANDLE h = Field<HANDLE>(file, 0);
    DWORD position = IatSetFilePointer(h, 0, nullptr, FILE_CURRENT);
    IatSetFilePointer(h, 0, nullptr, FILE_END);
    DWORD size = IatSetFilePointer(h, 0, nullptr, FILE_CURRENT);
    IatSetFilePointer(h, static_cast<LONG>(position), nullptr, FILE_BEGIN);
    auto* data = reinterpret_cast<uint8_t*>(orig::fn_004e07f0(kGameHeap, 0x10, size, 2, kStrNewArray));
    DWORD read;
    IatReadFile(h, data, size, &read, nullptr);
    uint32_t* bitmap;
    if (read != size)
    {
        bitmap = reinterpret_cast<uint32_t*>(read);  // original bug: the byte count is returned as the bitmap
    }
    else
    {
        bitmap = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kGameHeap, 0x10, 0x20, 2, kStrNew));
        if (bitmap)
        {
            std::memset(bitmap, 0, 0x20);
            Tga_Decode(bitmap, data, size);
            Bitmap_ComputeSteps(bitmap, 0);
        }
    }
    IatCloseHandle(h);
    GameFree(u(data));
    return bitmap;
}

namespace
{
// log2 of a bitmap size against the texture size limit at 0x005caae0: the exponent when the size is a power of two
// within the limit, else minus the number of doublings tried.
int32_t SizeExponent(int32_t size)
{
    int32_t limit = GameGlobal<int32_t>(0x005caae0);
    int32_t power = 1;
    int32_t exponent = 0;
    if (limit >= power)
    {
        for (;;)
        {
            if (size <= power)
            {
                if (size >= power)
                    return exponent;
                break;
            }
            power += power;
            ++exponent;
            if (power > limit)
                break;
        }
    }
    return -exponent;
}
}  // namespace

// 0x0050b9e0 ([texture record], cdecl): the renderer's texture load callback (+0x4a0, see
// Renderer_RegisterTexture). For records loaded from disk (+0x23 bit 4): load the file by its base name (after the
// last '\' or '/') and then by the full name (+0) with Bitmap_LoadTgaFile. Found: data (+4) = bitmap, +0x18 and
// +0x1c = bitmap +0x14, +0x10/+0x14 = size exponents of the bitmap width (+0)/height (+4), set +0x23 bit 3 and log
// "Loaded texture \"%s\" from disk". Not found: log "Failed to load texture ..." and clear +0x23 bit 4. The
// original returns the log call's EAX (callers ignore it); this returns 0.
// abi.csv lists no stack arguments (no direct call sites), but the code reads one cdecl argument at [esp+4]; the
// entry stub below forwards it.
uint32_t Texture_LoadFromDisk(uint8_t* tex)
{
    if (!(tex[0x23] & 0x10))
        return 0;
    const char* name = Field<const char*>(tex, 0);
    int32_t length = 0;
    if (name[0] != 0)
    {
        while (name[length + 1] != 0)
            ++length;
        ++length;
        while (length > 0 && name[length - 1] != '\\' && name[length - 1] != '/')
            --length;
    }
    uint32_t* bitmap = Bitmap_LoadTgaFile(name + length);
    if (!bitmap)
        bitmap = Bitmap_LoadTgaFile(Field<const char*>(tex, 0));
    if (!bitmap)
    {
        GameLog(Str(kStrFailedTexture), Field<const char*>(tex, 0));
        tex[0x23] &= 0xef;
        return 0;
    }
    Field<uint32_t*>(tex, 4) = bitmap;
    uint32_t format = bitmap[5];
    Field<uint32_t>(tex, 0x1c) = format;
    Field<uint32_t>(tex, 0x18) = format;
    Field<int32_t>(tex, 0x10) = SizeExponent(static_cast<int32_t>(bitmap[0]));
    int32_t heightExponent = SizeExponent(static_cast<int32_t>(bitmap[1]));
    tex[0x23] |= 8;
    Field<int32_t>(tex, 0x14) = heightExponent;
    GameLog(Str(kStrLoadedTexture), Field<const char*>(tex, 0));
    return 0;
}

// 0x0050bae0 ([texture record], cdecl): the renderer's texture release callback (+0x4a4). For records loaded from
// disk (+0x23 bit 4): free the bitmap's pixels (+0x1c) and the bitmap (+4, then cleared) with the game's heap,
// clear +0x23 bit 3 and set +0x18/+0x1c to 15. Returns 15, or (not from disk) EAX unchanged, reproduced as 0.
// Same stack argument note as Texture_LoadFromDisk.
uint32_t Texture_UnloadFromDisk(uint8_t* tex)
{
    if (!(tex[0x23] & 0x10))
        return 0;
    if (uint8_t* bitmap = Field<uint8_t*>(tex, 4))
    {
        if (uint32_t pixels = Field<uint32_t>(bitmap, 0x1c))
            GameFree(pixels);
        GameFree(Field<uint32_t>(tex, 4));
        Field<uint32_t>(tex, 4) = 0;
    }
    tex[0x23] &= 0xf7;
    Field<uint32_t>(tex, 0x1c) = 0xf;
    Field<uint32_t>(tex, 0x18) = 0xf;
    return 0xf;
}

// 0x0050be40 (eax = source surface, ecx = destination surface; [unused], callee pops 4): copy a surface with
// D3DXLoadSurfaceFromSurface (statically linked at 0x00545cf8; no palettes or rectangles, D3DX_DEFAULT filter,
// no colour key) and log "Error %s in DX function %s" on failure. Returns true on success. Caller: 0x0050c040.
// (not executed in the comparison scenarios)
bool Surface_LoadFromSurface(IDirect3DSurface9* source, IDirect3DSurface9* destination, uint32_t /*unused*/)
{
    uint32_t hr = orig::fn_00545cf8(u(destination), 0, 0, u(source), 0, 0, 0xffffffff, 0);
    if (hr != 0)
    {
        auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(hr));
        GameLog(Str(kStrDXError), name, Str(kStrD3DXLoadSurfaceFromSurface));
    }
    return hr == 0;
}

// 0x0050bfe0 ([texture record], callee pops 4): for a created (word +0x22 bit 15) non-cube (bit 5 clear) texture,
// GetSurfaceLevel and Release every level after the first of its IDirect3DTexture9 (bucket +0x0c, then +0x10);
// nothing else is done with the surfaces. Returns the last COM result (level count with one level); for other
// textures the original returns the flag word in AX (upper EAX bits not reproduced). No direct callers (virtual).
// (not executed in the comparison scenarios)
uint32_t Texture_TouchMipLevels(uint8_t* tex)
{
    uint16_t flags = Field<uint16_t>(tex, 0x22);
    if ((flags & 0x20) || !(flags & 0x8000))
        return flags;
    IDirect3DTexture9* texture = Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10);
    DWORD levels = texture->GetLevelCount();
    uint32_t result = levels;
    for (DWORD level = 1; level < levels; ++level)
    {
        IDirect3DSurface9* surface;
        result = static_cast<uint32_t>(
            Field<IDirect3DTexture9*>(Field<uint8_t*>(tex, 0xc), 0x10)->GetSurfaceLevel(level, &surface));
        if (result == 0)
            result = surface->Release();
    }
    return result;
}

// ---- ABI adapters and hooks

static uint32_t Renderer_ShowErrorBox_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return static_cast<uint32_t>(
        Renderer_ShowErrorBox(reinterpret_cast<const char*>(esi), reinterpret_cast<Renderer*>(a0), a1, a2));
}
static uint32_t Renderer_SetDisplayMode_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0,
                                            uint32_t a1)
{
    return Renderer_SetDisplayMode(eax, ecx, edx, edi, reinterpret_cast<Renderer*>(a0), a1);
}
static uint32_t Renderer_ReleaseD3D_Abi(uint32_t eax)
{
    return Renderer_ReleaseD3D(reinterpret_cast<Renderer*>(eax));
}
static uint32_t Renderer_RestoreTextures_Abi(uint32_t a0, uint32_t a1)
{
    Renderer_RestoreTextures(reinterpret_cast<Renderer*>(a0), a1);
    return 0;
}
static uint32_t Renderer_ChangeDisplayMode_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Renderer_ChangeDisplayMode(reinterpret_cast<Renderer*>(ecx), a0, a1, a2, a3);
}
static uint32_t Renderer_ResetDevice_Abi(uint32_t eax)
{
    return Renderer_ResetDevice(reinterpret_cast<Renderer*>(eax));
}
static uint32_t Renderer_ReleaseAllTextures_Abi(uint32_t edi)
{
    return Renderer_ReleaseAllTextures(reinterpret_cast<Renderer*>(edi));
}
static uint32_t Renderer_ReleaseTextureResources_Abi(uint32_t edi)
{
    return Renderer_ReleaseTextureResources(reinterpret_cast<Renderer*>(edi));
}
static uint32_t Renderer_ReleaseDevice_Abi(uint32_t eax)
{
    return Renderer_ReleaseDevice(reinterpret_cast<Renderer*>(eax));
}
static uint32_t Renderer_RestoreDevice_Abi(uint32_t esi)
{
    return static_cast<uint32_t>(Renderer_RestoreDevice(reinterpret_cast<Renderer*>(esi)));
}
static uint32_t Bitmap_LoadTgaFile_Abi(uint32_t /*ebx*/, uint32_t /*edi*/, uint32_t /*ebp*/, uint32_t a0)
{
    return u(Bitmap_LoadTgaFile(reinterpret_cast<const char*>(a0)));
}
static uint32_t Texture_LoadFromDisk_Abi(uint32_t a0)
{
    return Texture_LoadFromDisk(reinterpret_cast<uint8_t*>(a0));
}
static uint32_t Texture_UnloadFromDisk_Abi(uint32_t a0)
{
    return Texture_UnloadFromDisk(reinterpret_cast<uint8_t*>(a0));
}
static uint32_t TextureBucket_ReleaseD3D_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return TextureBucket_ReleaseD3D(reinterpret_cast<uint8_t*>(esi), reinterpret_cast<Renderer*>(edi), a0);
}
static uint32_t TextureBucket_Release_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return TextureBucket_Release(eax, reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<Renderer*>(edx));
}
static uint32_t Surface_LoadFromSurface_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return Surface_LoadFromSurface(reinterpret_cast<IDirect3DSurface9*>(eax), reinterpret_cast<IDirect3DSurface9*>(ecx),
                                   a0);
}
static uint32_t Texture_TouchMipLevels_Abi(uint32_t a0)
{
    return Texture_TouchMipLevels(reinterpret_cast<uint8_t*>(a0));
}

// 0x0050b9e0 and 0x0050bae0 are cdecl with one stack argument, but abi.csv has no call sites to infer it from
// (they are reached through the renderer's callback pointers +0x4a0/+0x4a4), so their generated stubs pass
// nothing. These stubs are the generated shape with the one caller-popped argument forwarded.
// clang-format off
#define CDECL1_ENTRY(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 28] __asm call impl __asm add esp, 4 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret }
// clang-format on

ABI_ENTRY_00508210(Renderer_ShowErrorBox_Entry, Renderer_ShowErrorBox_Abi)
ABI_ENTRY_00509040(Renderer_SetDisplayMode_Entry, Renderer_SetDisplayMode_Abi)
ABI_ENTRY_00509170(Renderer_ReleaseD3D_Entry, Renderer_ReleaseD3D_Abi)
ABI_ENTRY_005093b0(Renderer_RestoreTextures_Entry, Renderer_RestoreTextures_Abi)
ABI_ENTRY_00509560(Renderer_ChangeDisplayMode_Entry, Renderer_ChangeDisplayMode_Abi)
ABI_ENTRY_00509620(Renderer_ResetDevice_Entry, Renderer_ResetDevice_Abi)
ABI_ENTRY_005097c0(Renderer_ReleaseAllTextures_Entry, Renderer_ReleaseAllTextures_Abi)
ABI_ENTRY_00509890(Renderer_ReleaseTextureResources_Entry, Renderer_ReleaseTextureResources_Abi)
ABI_ENTRY_00509920(Renderer_ReleaseDevice_Entry, Renderer_ReleaseDevice_Abi)
ABI_ENTRY_005099b0(Renderer_RestoreDevice_Entry, Renderer_RestoreDevice_Abi)
ABI_ENTRY_0050b8a0(Bitmap_LoadTgaFile_Entry, Bitmap_LoadTgaFile_Abi)
CDECL1_ENTRY(Texture_LoadFromDisk_Entry, Texture_LoadFromDisk_Abi)
CDECL1_ENTRY(Texture_UnloadFromDisk_Entry, Texture_UnloadFromDisk_Abi)
ABI_ENTRY_0050bc50(TextureBucket_ReleaseD3D_Entry, TextureBucket_ReleaseD3D_Abi)
ABI_ENTRY_0050bcc0(TextureBucket_Release_Entry, TextureBucket_Release_Abi)
ABI_ENTRY_0050be40(Surface_LoadFromSurface_Entry, Surface_LoadFromSurface_Abi)
ABI_ENTRY_0050bfe0(Texture_TouchMipLevels_Entry, Texture_TouchMipLevels_Abi)

HOOK(0x00508210, "Renderer_ShowErrorBox", Renderer_ShowErrorBox_Entry, nullptr);
HOOK(0x00509040, "Renderer_SetDisplayMode", Renderer_SetDisplayMode_Entry, nullptr);
HOOK(0x00509170, "Renderer_ReleaseD3D", Renderer_ReleaseD3D_Entry, nullptr);
HOOK(0x005093b0, "Renderer_RestoreTextures", Renderer_RestoreTextures_Entry, nullptr);
HOOK(0x00509560, "Renderer_ChangeDisplayMode", Renderer_ChangeDisplayMode_Entry, nullptr);
HOOK(0x00509620, "Renderer_ResetDevice", Renderer_ResetDevice_Entry, nullptr);
HOOK(0x005097c0, "Renderer_ReleaseAllTextures", Renderer_ReleaseAllTextures_Entry, nullptr);
HOOK(0x00509890, "Renderer_ReleaseTextureResources", Renderer_ReleaseTextureResources_Entry, nullptr);
HOOK(0x00509920, "Renderer_ReleaseDevice", Renderer_ReleaseDevice_Entry, nullptr);
HOOK(0x005099b0, "Renderer_RestoreDevice", Renderer_RestoreDevice_Entry, nullptr);
HOOK(0x0050b8a0, "Bitmap_LoadTgaFile", Bitmap_LoadTgaFile_Entry, nullptr);
HOOK(0x0050b9e0, "Texture_LoadFromDisk", Texture_LoadFromDisk_Entry, nullptr);
HOOK(0x0050bae0, "Texture_UnloadFromDisk", Texture_UnloadFromDisk_Entry, nullptr);
HOOK(0x0050bc50, "TextureBucket_ReleaseD3D", TextureBucket_ReleaseD3D_Entry, nullptr);
HOOK(0x0050bcc0, "TextureBucket_Release", TextureBucket_Release_Entry, nullptr);
HOOK(0x0050be40, "Surface_LoadFromSurface", Surface_LoadFromSurface_Entry, nullptr);
HOOK(0x0050bfe0, "Texture_TouchMipLevels", Texture_TouchMipLevels_Entry, nullptr);
