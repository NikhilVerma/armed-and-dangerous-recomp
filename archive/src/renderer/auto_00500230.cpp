// Recovered: 0x00500230-0x00500d20. S2, Approach B.
// Despite the address range (between the D3D renderer files), these are audio helpers:
//   0x00500230-0x00500520  a list of positional sound emitters owned by the audio system (list at
//                          audio+0x74, see callers 0x004f20d0/0x004f22e0/0x004f2a80). Element 0x94 bytes,
//                          random re-trigger interval 20..45 (floats 0x41a00000/0x42340000), distance cut-off.
//                          The "AmbientSounds" name is a guess from that behaviour.
//   0x00500840-0x00500d20  sound resolution and stream refill for a playing sound ("Audio : %s not found").
//
// Emitter element layout (offsets from the code below):
//   +0x00/+0x04 list links, +0x08 id, +0x0c handle A (loop), +0x10 handle B (one-shot), +0x14 byte no position,
//   +0x15 byte play with position, +0x18 interval min, +0x1c interval max, +0x20 position[3], +0x2c timer,
//   +0x30 volume, +0x34 loop sounds[10], +0x5c one-shot sounds[10], +0x84 one-shot count, +0x88 loop count,
//   +0x8c/+0x90 attenuation distances (+0x90 is the cut-off radius).
// List object: +0x00/+0x04 head, +0x94 element pool {elemSize, perChunk, freeList, chunkList}, +0x98 audio system.
#include <cmath>
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
constexpr uint32_t kMainHeap = 0x0063a210;                                       // ecx of 0x004e07f0/0x004e08d0
const char* const kStrNew = reinterpret_cast<const char*>(0x0058b5e8);           // "new"
const char* const kStrNotFound = reinterpret_cast<const char*>(0x0058ad28);      // "Audio : %s not found"
const char* const kStrNone = reinterpret_cast<const char*>(0x005bb9e0);          // "none"
const float& kRandScale = GameGlobal<float>(0x005bcbc0);                         // 1/32768
const float& kZero = GameGlobal<float>(0x0056c05c);                              // 0.0

// The game's statically linked rand() (0x005391cd).
int32_t GameRand()
{
    return static_cast<int32_t>(orig::fn_005391cd());
}

void* AudioOf(void* list)
{
    return Field<void*>(list, 0x98);
}

// One random entry of a sound table: (rand() * count) >> 15, as imul/sar.
uint32_t PickRandom(uint8_t* e, uint32_t tableOffset, uint32_t count)
{
    int32_t index = (GameRand() * static_cast<int32_t>(count)) >> 15;
    return Field<uint32_t>(e, tableOffset + index * 4);
}

// New timer: (max - min) * rand() * (1/32768) + min.
void ResetTimer(uint8_t* e)
{
    float hi = Field<float>(e, 0x1c);
    float lo = Field<float>(e, 0x18);
    float r = static_cast<float>(GameRand());
    Field<float>(e, 0x2c) = r * (hi - lo) * kRandScale + lo;
}
}  // namespace

// 0x00500230 (esi = list; [audio system], callee pops): initialise an empty emitter list: head links to
// itself, an element pool for 0x94-byte elements, 10 per chunk (0x10-byte header allocated from the main heap
// 0x0063a210 by 0x004e07f0 with tag "new"), and the owning audio system at +0x98. Returns the list.
uint8_t* AmbientSounds_Init(uint8_t* list, uint32_t audio)
{
    Field<uint8_t*>(list, 0) = list;
    Field<uint8_t*>(list, 4) = list;
    auto* pool = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kMainHeap, 0x10, 0x10, 2, u(kStrNew)));
    if (pool)
    {
        pool[0] = 0x94;
        pool[1] = 10;
        pool[2] = 0;
        pool[3] = 0;
    }
    Field<uint32_t*>(list, 0x94) = pool;
    Field<uint32_t>(list, 0x98) = audio;
    return list;
}

// 0x005003c0 (eax = list): return every element to the pool's free list (+8), then free all pool chunks
// (singly linked from +0xc, 0x004e08d0 on the main heap) and clear both pool lists. Returns 0 (EAX ends 0).
uint32_t AmbientSounds_Clear(uint8_t* list)
{
    auto* head = reinterpret_cast<ListNode*>(list);
    for (ListNode* n = head->next; n != head;)
    {
        ListNode* next = n->next;
        next->prev = n->prev;
        n->prev->next = n->next;
        uint8_t* pool = Field<uint8_t*>(list, 0x94);
        n->next = Field<ListNode*>(pool, 8);
        Field<ListNode*>(pool, 8) = n;
        n = next;
    }
    uint8_t* pool = Field<uint8_t*>(list, 0x94);
    for (auto* chunk = Field<uint32_t*>(pool, 0xc); chunk;)
    {
        auto* next = reinterpret_cast<uint32_t*>(*chunk);
        orig::fn_004e08d0(kMainHeap, u(chunk));
        chunk = next;
    }
    Field<uint32_t>(pool, 0xc) = 0;
    Field<uint32_t>(pool, 8) = 0;
    return 0;
}

// 0x00500280 ([list], callee pops): destroy: AmbientSounds_Clear, then free any remaining pool chunks and the
// pool header, and clear +0x94. Returns the last free's result (unused by the caller 0x004f22e0).
// (not executed in the comparison scenarios)
uint32_t AmbientSounds_Destroy(uint8_t* list)
{
    uint32_t result = AmbientSounds_Clear(list);
    uint8_t* pool = Field<uint8_t*>(list, 0x94);
    if (pool)
    {
        for (auto* chunk = Field<uint32_t*>(pool, 0xc); chunk;)
        {
            auto* next = reinterpret_cast<uint32_t*>(*chunk);
            orig::fn_004e08d0(kMainHeap, u(chunk));
            chunk = next;
        }
        Field<uint32_t>(pool, 0xc) = 0;
        Field<uint32_t>(pool, 8) = 0;
        result = orig::fn_004e08d0(kMainHeap, u(pool));
    }
    Field<uint32_t>(list, 0x94) = 0;
    return result;
}

// 0x005002e0 (bl = no position; [list, position, play with position], callee pops): allocate an element from
// the pool (0x004f6020), zero it, link it at the head, give it the next id from the audio system's counter
// (+0x34, skips 0), set the flags (+0x14 = bl, +0x15 = byte of the third argument), copy the position when bl is
// 0, and the re-trigger interval 20..45. Returns the id.
uint32_t AmbientSounds_Add(uint8_t noPosition, uint8_t* list, const uint32_t* position, uint32_t withPosition)
{
    auto* e = reinterpret_cast<uint32_t*>(orig::fn_004f6020(Field<uint32_t>(list, 0x94)));
    for (int i = 0; i < 0x25; ++i)
        e[i] = 0;
    auto* head = reinterpret_cast<ListNode*>(list);
    auto* node = reinterpret_cast<ListNode*>(e);
    head->next->prev = node;
    node->next = head->next;
    node->prev = head;
    head->next = node;
    uint8_t* audio = Field<uint8_t*>(list, 0x98);
    uint32_t id = Field<uint32_t>(audio, 0x34);
    Field<uint32_t>(audio, 0x34) = id + 1;
    if (id + 1 == 0)
        Field<uint32_t>(audio, 0x34) = 1;
    e[2] = id;
    reinterpret_cast<uint8_t*>(e)[0x14] = noPosition;
    reinterpret_cast<uint8_t*>(e)[0x15] = static_cast<uint8_t>(withPosition);
    if (noPosition == 0)
    {
        e[8] = position[0];
        e[9] = position[1];
        e[10] = position[2];
    }
    e[6] = 0x41a00000;  // 20.0f
    e[7] = 0x42340000;  // 45.0f
    return id;
}

// 0x00500370 (ecx = list, esi = pointer to id): if *id is non-zero, find the element with that id (+8), unlink
// it and push it on the pool's free list; *id becomes 0 either way. Does not stop its sounds. Returns the element,
// or the list head when not found/empty (EAX at those exits); 0 when *id was 0 (the original returns the caller's
// EAX there). (not executed in the comparison scenarios)
uint32_t AmbientSounds_Remove(uint8_t* list, uint32_t* id)
{
    uint32_t wanted = *id;
    if (wanted == 0)
        return 0;
    auto* head = reinterpret_cast<ListNode*>(list);
    ListNode* n = head->next;
    if (n != head)
    {
        while (Field<uint32_t>(n, 8) != wanted)
        {
            n = n->next;
            if (n == head)
            {
                *id = 0;
                return u(n);
            }
        }
        n->next->prev = n->prev;
        n->prev->next = n->next;
        uint8_t* pool = Field<uint8_t*>(list, 0x94);
        n->next = Field<ListNode*>(pool, 8);
        Field<ListNode*>(pool, 8) = n;
    }
    *id = 0;
    return u(n);
}

// Shared body of 0x00500420/0x005004a0: append the sound named `name` (looked up in the audio system's name
// dictionary +0xa0 with 0x004f1160, -1 when missing) to one of the element's 10-entry tables. The value is
// always written, the count only grows when it is a valid index (< audio +0x78). Returns the dictionary when it
// is null or the element is missing/full, else the count (new when grown).
static uint32_t AmbientSounds_AddSoundTo(uint8_t* list, uint32_t id, uint32_t name, uint32_t countOffset,
                                         uint32_t tableOffset)
{
    uint32_t dict = Field<uint32_t>(AudioOf(list), 0xa0);
    if (dict == 0)
        return dict;
    auto* head = reinterpret_cast<ListNode*>(list);
    ListNode* n = head->next;
    if (n == head)
        return dict;
    while (Field<uint32_t>(n, 8) != id)
    {
        n = n->next;
        if (n == head)
            return dict;
    }
    if (Field<uint32_t>(n, countOffset) >= 10)
        return dict;
    auto* entry = reinterpret_cast<uint32_t*>(orig::fn_004f1160(dict, name));
    uint32_t value = entry ? *entry : 0xffffffff;
    Field<uint32_t>(n, tableOffset + Field<uint32_t>(n, countOffset) * 4) = value;
    uint32_t count = Field<uint32_t>(n, countOffset);
    if (Field<uint32_t>(n, tableOffset + count * 4) < Field<uint32_t>(AudioOf(list), 0x78))
    {
        ++count;
        Field<uint32_t>(n, countOffset) = count;
    }
    return count;
}

// 0x00500420 (eax = list, ecx = id; [sound name], callee pops): add a one-shot sound (table +0x5c, count +0x84),
// played at random intervals by AmbientSounds_Update.
uint32_t AmbientSounds_AddOneShot(uint8_t* list, uint32_t id, uint32_t name)
{
    return AmbientSounds_AddSoundTo(list, id, name, 0x84, 0x5c);
}

// 0x005004a0 (eax = list, ecx = id; [sound name], callee pops): add a loop sound (table +0x34, count +0x88),
// started by AmbientSounds_Update while the element has no handle A.
uint32_t AmbientSounds_AddLoop(uint8_t* list, uint32_t id, uint32_t name)
{
    return AmbientSounds_AddSoundTo(list, id, name, 0x88, 0x34);
}

// 0x00500520 (edi = list; [elapsed time], callee pops): per-frame update. Reads the listener position (audio
// virtual +0x118). For each element with a position (+0x14 == 0) farther than its radius (+0x90), both handles
// are stopped and cleared (virtual +0xd8 with the handle's address, 0). Otherwise:
// - no handle A and loop sounds: start a random loop sound (virtual +0xcc with the position when +0x15, else
//   +0xd0, the "1" argument presumably loop) into handle A and reset the timer;
// - positional elements without +0x15: volume +0x30 = virtual +0xac(distance, +0x8c, +0x90) (float in st0);
//   handle A gets it via virtual +0xf8;
// - handle B playing (virtual +0xdc) gets the volume, else it is cleared; with no handle B and one-shot sounds,
//   unless audio +0xa8 is set, the timer counts down by the elapsed time and on expiry a random one-shot starts
//   and the timer resets. Original quirk: with +0x15 set the one-shot handle goes to +0xc (handle A), not +0x10.
// Returns 0 (EAX is left over in the original; the caller 0x004f2a80 ignores it).
uint32_t AmbientSounds_Update(uint8_t* list, float elapsed)
{
    float listener[3];
    VCall<uint32_t>(AudioOf(list), 0x118 / 4, listener);
    auto* head = reinterpret_cast<ListNode*>(list);
    float distance = 0.0f;  // only read for positional elements, after being set
    for (ListNode* n = head->next; n != head; n = n->next)
    {
        auto* e = reinterpret_cast<uint8_t*>(n);
        if (e[0x14] == 0)
        {
            float dz = Field<float>(e, 0x28) - listener[2];
            float dy = Field<float>(e, 0x24) - listener[1];
            float dx = Field<float>(e, 0x20) - listener[0];
            distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance > Field<float>(e, 0x90))
            {
                if (Field<uint32_t>(e, 0xc) != 0)
                    VCall<uint32_t>(AudioOf(list), 0xd8 / 4, &Field<uint32_t>(e, 0xc), 0u);
                if (Field<uint32_t>(e, 0x10) != 0)
                    VCall<uint32_t>(AudioOf(list), 0xd8 / 4, &Field<uint32_t>(e, 0x10), 0u);
                continue;
            }
        }

        if (Field<uint32_t>(e, 0xc) == 0)
        {
            uint32_t loops = Field<uint32_t>(e, 0x88);
            if (loops != 0)
            {
                uint32_t handle;
                if (e[0x15] != 0)
                {
                    uint32_t sound = PickRandom(e, 0x34, loops);
                    handle = VCall<uint32_t>(AudioOf(list), 0xcc / 4, 0u, &Field<uint32_t>(e, 0x20), 0u, 1u, 0u,
                                             sound, 0u);
                }
                else
                {
                    uint32_t sound = PickRandom(e, 0x34, loops);
                    handle = VCall<uint32_t>(AudioOf(list), 0xd0 / 4, 0u, 0u, 1u, 0u, sound, 0u);
                }
                Field<uint32_t>(e, 0xc) = handle;
                ResetTimer(e);
            }
        }

        if (e[0x14] == 0 && e[0x15] == 0)
            Field<float>(e, 0x30) = VCall<float>(AudioOf(list), 0xac / 4, distance, Field<float>(e, 0x8c),
                                                 Field<float>(e, 0x90));

        if (Field<uint32_t>(e, 0xc) != 0 && e[0x14] == 0 && e[0x15] == 0)
            VCall<uint32_t>(AudioOf(list), 0xf8 / 4, Field<uint32_t>(e, 0xc), Field<float>(e, 0x30));

        if (Field<uint32_t>(e, 0x10) != 0)
        {
            if (!VCall<bool>(AudioOf(list), 0xdc / 4, Field<uint32_t>(e, 0x10)))
                Field<uint32_t>(e, 0x10) = 0;
            else if (e[0x14] == 0 && e[0x15] == 0)
                VCall<uint32_t>(AudioOf(list), 0xf8 / 4, Field<uint32_t>(e, 0x10), Field<float>(e, 0x30));
            continue;
        }

        uint32_t oneShots = Field<uint32_t>(e, 0x84);
        if (oneShots == 0 || Field<uint32_t>(AudioOf(list), 0xa8) != 0)
            continue;
        float timer = Field<float>(e, 0x2c) - elapsed;
        Field<float>(e, 0x2c) = timer;
        if (!(timer < kZero))
            continue;
        if (e[0x15] != 0)
        {
            uint32_t sound = PickRandom(e, 0x5c, oneShots);
            Field<uint32_t>(e, 0xc) = VCall<uint32_t>(AudioOf(list), 0xcc / 4, 0u, &Field<uint32_t>(e, 0x20), 0u,
                                                      0u, 0u, sound, 0u);
        }
        else
        {
            uint32_t sound = PickRandom(e, 0x5c, oneShots);
            Field<uint32_t>(e, 0x10) = VCall<uint32_t>(AudioOf(list), 0xd0 / 4, 0u, 0u, 0u, 0u, sound, 0u);
        }
        if (e[0x14] == 0 && e[0x15] == 0 && Field<uint32_t>(e, 0x10) != 0)
            VCall<uint32_t>(AudioOf(list), 0xf8 / 4, Field<uint32_t>(e, 0x10), Field<float>(e, 0x30));
        ResetTimer(e);
    }
    return 0;
}

// 0x005009b0 (eax = destination, ecx = passed through to strrchr, esi = source; [unused], callee pops): copy
// the source name into the destination, cut at its last '_' when it has one (strrchr 0x00539ee0; the '_' is
// restored in the source). Returns the '_' in the source, or one past the source's terminator when there is none.
char* String_CopyBeforeLastUnderscore(char* dest, uint32_t ecx, char* source)
{
    auto* underscore = reinterpret_cast<char*>(orig::fn_00539ee0(ecx, u(source), '_'));
    if (underscore)
    {
        *underscore = 0;
        char* s = source;
        char* d = dest;
        char c;
        do
        {
            c = *s++;
            *d++ = c;
        } while (c != 0);
        *underscore = '_';
        return underscore;
    }
    char* s = source;
    char* d = dest;
    char c;
    do
    {
        c = *s++;
        *d++ = c;
    } while (c != 0);
    return s;
}

// 0x00500a00 (edi unused; [audio system, sound object, sound params, unused 1], callee pops): resolve which
// sound record the object plays. With no name in the params (+8) the object's own record (+0xc) is used;
// otherwise the name is looked up in the audio system's dictionary (+0xa0, 0x004f1160) and table (+0x80, count
// +0x78), logging "Audio : %s not found" (0x00407ff0, channel +8, level 5) when missing. The record goes to the
// object's +0x70. When it has variants (+0x20 bit 0x2000000, count +0x17c, 0x20-byte names from +0x180), a
// variant record is chosen with 0x004f2470 into +0x6c: one matching the audio system's current variant
// name (+0x94, compared up to the last '_'), else the first variant ("none" means no variant; params bit 0x800
// with record bit 0x8000000 takes the second). Matching clears the current name. Sets object +0x64 bit 0x80 when
// a variant was found. Returns 0 (the only caller, 0x00500890, ignores EAX).
uint32_t Audio_ResolveSound(uint32_t /*edi*/, uint8_t* audio, uint8_t* object, uint32_t* params, uint32_t /*one*/)
{
    char nameBase[32];
    char variantBase[32];
    uint32_t index = 0xffffffff;
    uint8_t* sound;
    if (params[2] == 0)
    {
        String_CopyBeforeLastUnderscore(nameBase, 0, Field<char*>(object, 0xc));
        sound = Field<uint8_t*>(object, 0xc);
    }
    else
    {
        String_CopyBeforeLastUnderscore(nameBase, 0, reinterpret_cast<char*>(params[2]));
        uint32_t name = params[2];
        if (Field<uint32_t>(audio, 0x80) == 0)
        {
            sound = nullptr;
        }
        else
        {
            if (name != 0)
            {
                uint32_t dict = Field<uint32_t>(audio, 0xa0);
                if (dict != 0)
                {
                    auto* entry = reinterpret_cast<uint32_t*>(orig::fn_004f1160(dict, name));
                    index = entry ? *entry : 0xffffffff;
                }
                else
                {
                    index = 0xffffffff;
                }
            }
            if (index < Field<uint32_t>(audio, 0x78))
            {
                sound = Field<uint8_t**>(audio, 0x80)[index];
            }
            else
            {
                orig::fn_00407ff0(Field<uint32_t>(audio, 8), 5, u(kStrNotFound), name);
                sound = nullptr;
            }
        }
    }
    Field<uint8_t*>(object, 0x70) = sound;
    if (!(Field<uint32_t>(sound, 0x20) & 0x2000000))  // the original reads through a null record too
        return 0;
    if (Field<uint32_t>(sound, 0x17c) == 0)
    {
        Field<uint32_t>(object, 0x6c) = 0;
        return 0;
    }

    uint8_t* chosen = sound;  // the original reuses the record's slot for the result
    bool wasCurrent = false;
    if (Field<uint8_t*>(audio, 0x94) == sound)
    {
        chosen = nullptr;
        Field<uint32_t>(audio, 0x94) = 0;
        wasCurrent = true;
    }

    if (Field<uint32_t>(audio, 0x94) != 0)
    {
        uint8_t* found = nullptr;
        uint32_t count = Field<uint32_t>(Field<uint8_t*>(object, 0x70), 0x17c);
        uint32_t i = 0;
        uint32_t offset = 0x180;
        bool matched = false;
        for (; i < count; ++i, offset += 0x20)
        {
            String_CopyBeforeLastUnderscore(variantBase, 0, Field<char*>(object, 0x70) + offset);
            if (orig::fn_005448ee(u(variantBase), Field<uint32_t>(audio, 0x94), u(variantBase)) == 0)
            {
                chosen = reinterpret_cast<uint8_t*>(
                    orig::fn_004f2470(u(&index), u(audio), u(Field<uint8_t*>(object, 0x70) + offset), 0));
                if (chosen)
                {
                    matched = true;
                    break;
                }
            }
            else if (found == nullptr)
            {
                auto* other = reinterpret_cast<uint8_t*>(
                    orig::fn_004f2470(u(&index), u(audio), u(Field<uint8_t*>(object, 0x70) + offset), 1));
                if (other)
                {
                    uint32_t j = 0;
                    char* variant = reinterpret_cast<char*>(other + 0x180);
                    while (j < Field<uint32_t>(other, 0x17c))
                    {
                        String_CopyBeforeLastUnderscore(variantBase, 0, variant);
                        if (orig::fn_005448ee(u(variantBase), Field<uint32_t>(audio, 0x94), u(variantBase)) == 0)
                        {
                            found = other;
                            break;
                        }
                        ++j;
                        variant += 0x20;
                    }
                }
            }
        }
        if (!matched)
        {
            if (found)
                chosen = found;
            else
                chosen = reinterpret_cast<uint8_t*>(
                    orig::fn_004f2470(u(&index), u(audio), u(Field<uint8_t*>(object, 0x70) + 0x180), 1));
        }
    }
    else if (!wasCurrent)
    {
        char* variant = reinterpret_cast<char*>(sound + 0x180);
        if (orig::fn_00544957(u(sound), u(variant), u(kStrNone), 4) == 0)
        {
            Field<uint32_t>(object, 0x6c) = 0;
            return 0;
        }
        if ((params[0] & 0x800) && (Field<uint32_t>(sound, 0x20) & 0x8000000))
        {
            if (Field<uint32_t>(sound, 0x17c) <= 1)
            {
                Field<uint32_t>(object, 0x6c) = 0;
                return 0;
            }
            variant = reinterpret_cast<char*>(sound + 0x1a0);
        }
        chosen = reinterpret_cast<uint8_t*>(orig::fn_004f2470(u(&index), u(audio), u(variant), 0));
    }

    if (chosen)
    {
        Field<uint32_t>(object, 0x64) |= 0x80;
        char* current = Field<char*>(audio, 0x94);
        if (current)
        {
            String_CopyBeforeLastUnderscore(variantBase, 0, current);
            if (orig::fn_005448ee(u(variantBase), u(nameBase), u(variantBase)) == 0)
            {
                Field<uint32_t>(audio, 0x94) = 0;
                chosen = reinterpret_cast<uint8_t*>(orig::fn_004f2470(u(&index), u(audio), params[2], 0));
            }
        }
    }
    Field<uint8_t*>(object, 0x6c) = chosen;
    return 0;
}

// 0x00500890 (ecx = sound object, edi = its state (object +0x64 at both callers); [audio system], callee
// pops): when the state's sign bit (0x80) is set, feed the voice (+0x14). Records with flag 0x40 (+0x20): once
// voice virtual +0x34 says yes, re-resolve with Audio_ResolveSound; with no record left clear object +0x64 bit
// 0x80, else build the record's file name (0x004f19b0, record +0xfc, 260-byte buffer) and pass it to voice
// virtual +0x10 (second argument 0 when the object's record +0xc has flag 0x2000000, else 1). Other records:
// while voice virtual +0xc is below state +0x10, queue the record's data (+0xf8, size +0xf0) into the voice's
// current slot (byte +0xd, 12-byte slots from +0x20, only when its +4 == +8), raise the voice's +8 to at least
// the size and call voice virtual +0x2c(size). Returns 0 (both callers ignore EAX).
uint32_t fn_00500890(uint8_t* object, uint8_t* state, uint8_t* audio)
{
    if (static_cast<int8_t>(state[0]) >= 0)
        return 0;
    uint8_t* record = Field<uint8_t*>(state, 8);
    void* voice = Field<void*>(state, 0x14);
    if (record[0x20] & 0x40)
    {
        if (!VCall<bool>(voice, 0x34 / 4))
            return 0;
        Audio_ResolveSound(u(state), audio, object, reinterpret_cast<uint32_t*>(state), 1);
        uint8_t* resolved = Field<uint8_t*>(state, 8);
        if (!resolved)
        {
            Field<uint32_t>(object, 0x64) &= 0xffffff7f;
            return 0;
        }
        char path[260];
        uint32_t second = (Field<uint32_t>(Field<uint8_t*>(object, 0xc), 0x20) & 0x2000000) ? 0u : 1u;
        void* target = Field<void*>(state, 0x14);
        uint32_t name = orig::fn_004f19b0(u(resolved + 0xfc), u(path));
        VCall<uint32_t>(target, 0x10 / 4, name, second, 0u, 0u);
        return 0;
    }
    auto* stream = static_cast<uint8_t*>(voice);
    uint32_t queued = VCall<uint32_t>(stream, 0xc / 4);
    while (queued < Field<uint32_t>(state, 0x10))
    {
        uint8_t* rec = Field<uint8_t*>(state, 8);
        uint32_t size = Field<uint32_t>(rec, 0xf0);
        uint32_t data = Field<uint32_t>(rec, 0xf8);
        uint8_t* slot = stream + 0x20 + stream[0xd] * 12;
        if (data == 0 || size == 0)
            return 0;
        if (Field<uint32_t>(slot, 4) != Field<uint32_t>(slot, 8))
            return 0;
        Field<uint32_t>(slot, 0) = data;
        uint32_t largest = Field<uint32_t>(stream, 8);
        if (largest <= size)
            largest = size;
        Field<uint32_t>(stream, 8) = largest;
        VCall<uint32_t>(stream, 0x2c / 4, size);
        queued = VCall<uint32_t>(Field<void*>(state, 0x14), 0xc / 4);
    }
    return 0;
}

// 0x00500840 (ebx = handler, esi = sound object): fn_00500890(object, object +0x64, handler), then handler
// virtual 0 (object, object +0x5c). When +0x7c == +0x74 or flag 0x10000 (+0): unless voice (+0x78) virtual +0x34
// is false and virtual +8 is true with 0x10000 clear, set +0x64 bit 4. Returns the last virtual call's EAX.
uint32_t fn_00500840(void* handler, uint8_t* object)
{
    fn_00500890(object, object + 0x64, static_cast<uint8_t*>(handler));
    uint32_t result = VCall<uint32_t>(handler, 0, object, object + 0x5c);
    if (Field<uint32_t>(object, 0x7c) != Field<uint32_t>(object, 0x74) && !(Field<uint32_t>(object, 0) & 0x10000))
        return result;
    result = VCall<uint32_t>(Field<void*>(object, 0x78), 0x34 / 4);
    if ((result & 0xff) == 0)
    {
        result = VCall<uint32_t>(Field<void*>(object, 0x78), 8 / 4);
        if ((result & 0xff) != 0 && !(Field<uint32_t>(object, 0) & 0x10000))
            return result;
    }
    Field<uint32_t>(object, 0x64) |= 4;
    return result;
}

// 0x00500d20 (ebx = sound object, esi = handler): without +0x64 bit 4, handler virtual +0x2c (object, 0) and
// true. With it: handler virtual +0x28 (object); false returns that result, else fn_00500890(object, object
// +0x64, handler), handler virtual 0 (object, object +0x5c) and true. AL is the result (caller 0x004f29d0).
uint32_t fn_00500d20(uint8_t* object, void* handler)
{
    if (!(object[0x64] & 4))
    {
        uint32_t r = VCall<uint32_t>(handler, 0x2c / 4, object, 0u);
        return (r & 0xffffff00) | 1;
    }
    uint32_t r = VCall<uint32_t>(handler, 0x28 / 4, object);
    if ((r & 0xff) == 0)
        return r;
    fn_00500890(object, object + 0x64, static_cast<uint8_t*>(handler));
    r = VCall<uint32_t>(handler, 0, object, object + 0x5c);
    return (r & 0xffffff00) | 1;
}

static uint32_t AmbientSounds_Init_Abi(uint32_t esi, uint32_t audio)
{
    return u(AmbientSounds_Init(reinterpret_cast<uint8_t*>(esi), audio));
}
static uint32_t AmbientSounds_Destroy_Abi(uint32_t list)
{
    return AmbientSounds_Destroy(reinterpret_cast<uint8_t*>(list));
}
static uint32_t AmbientSounds_Add_Abi(uint32_t ebx, uint32_t list, uint32_t position, uint32_t withPosition)
{
    return AmbientSounds_Add(static_cast<uint8_t>(ebx), reinterpret_cast<uint8_t*>(list),
                             reinterpret_cast<const uint32_t*>(position), withPosition);
}
static uint32_t AmbientSounds_Remove_Abi(uint32_t ecx, uint32_t esi)
{
    return AmbientSounds_Remove(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint32_t*>(esi));
}
static uint32_t AmbientSounds_Clear_Abi(uint32_t eax)
{
    return AmbientSounds_Clear(reinterpret_cast<uint8_t*>(eax));
}
static uint32_t AmbientSounds_AddOneShot_Abi(uint32_t eax, uint32_t ecx, uint32_t name)
{
    return AmbientSounds_AddOneShot(reinterpret_cast<uint8_t*>(eax), ecx, name);
}
static uint32_t AmbientSounds_AddLoop_Abi(uint32_t eax, uint32_t ecx, uint32_t name)
{
    return AmbientSounds_AddLoop(reinterpret_cast<uint8_t*>(eax), ecx, name);
}
static uint32_t AmbientSounds_Update_Abi(uint32_t edi, uint32_t elapsed)
{
    float t;
    static_assert(sizeof(t) == sizeof(elapsed), "float size");
    memcpy(&t, &elapsed, sizeof(t));
    return AmbientSounds_Update(reinterpret_cast<uint8_t*>(edi), t);
}
static uint32_t fn_00500840_Abi(uint32_t ebx, uint32_t esi)
{
    return fn_00500840(reinterpret_cast<void*>(ebx), reinterpret_cast<uint8_t*>(esi));
}
static uint32_t fn_00500890_Abi(uint32_t ecx, uint32_t edi, uint32_t audio)
{
    return fn_00500890(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(edi),
                       reinterpret_cast<uint8_t*>(audio));
}
static uint32_t String_CopyBeforeLastUnderscore_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t /*unused*/)
{
    return u(String_CopyBeforeLastUnderscore(reinterpret_cast<char*>(eax), ecx, reinterpret_cast<char*>(esi)));
}
static uint32_t Audio_ResolveSound_Abi(uint32_t edi, uint32_t audio, uint32_t object, uint32_t params, uint32_t one)
{
    return Audio_ResolveSound(edi, reinterpret_cast<uint8_t*>(audio), reinterpret_cast<uint8_t*>(object),
                              reinterpret_cast<uint32_t*>(params), one);
}
static uint32_t fn_00500d20_Abi(uint32_t ebx, uint32_t esi)
{
    return fn_00500d20(reinterpret_cast<uint8_t*>(ebx), reinterpret_cast<void*>(esi));
}

ABI_ENTRY_00500230(AmbientSounds_Init_Entry, AmbientSounds_Init_Abi)
ABI_ENTRY_00500280(AmbientSounds_Destroy_Entry, AmbientSounds_Destroy_Abi)
ABI_ENTRY_005002e0(AmbientSounds_Add_Entry, AmbientSounds_Add_Abi)
ABI_ENTRY_00500370(AmbientSounds_Remove_Entry, AmbientSounds_Remove_Abi)
ABI_ENTRY_005003c0(AmbientSounds_Clear_Entry, AmbientSounds_Clear_Abi)
ABI_ENTRY_00500420(AmbientSounds_AddOneShot_Entry, AmbientSounds_AddOneShot_Abi)
ABI_ENTRY_005004a0(AmbientSounds_AddLoop_Entry, AmbientSounds_AddLoop_Abi)
ABI_ENTRY_00500520(AmbientSounds_Update_Entry, AmbientSounds_Update_Abi)
ABI_ENTRY_00500840(fn_00500840_Entry, fn_00500840_Abi)
ABI_ENTRY_00500890(fn_00500890_Entry, fn_00500890_Abi)
ABI_ENTRY_005009b0(String_CopyBeforeLastUnderscore_Entry, String_CopyBeforeLastUnderscore_Abi)
ABI_ENTRY_00500a00(Audio_ResolveSound_Entry, Audio_ResolveSound_Abi)
ABI_ENTRY_00500d20(fn_00500d20_Entry, fn_00500d20_Abi)
HOOK(0x00500230, "AmbientSounds_Init", AmbientSounds_Init_Entry, nullptr);
HOOK(0x00500280, "AmbientSounds_Destroy", AmbientSounds_Destroy_Entry, nullptr);
HOOK(0x005002e0, "AmbientSounds_Add", AmbientSounds_Add_Entry, nullptr);
HOOK(0x00500370, "AmbientSounds_Remove", AmbientSounds_Remove_Entry, nullptr);
HOOK(0x005003c0, "AmbientSounds_Clear", AmbientSounds_Clear_Entry, nullptr);
HOOK(0x00500420, "AmbientSounds_AddOneShot", AmbientSounds_AddOneShot_Entry, nullptr);
HOOK(0x005004a0, "AmbientSounds_AddLoop", AmbientSounds_AddLoop_Entry, nullptr);
HOOK(0x00500520, "AmbientSounds_Update", AmbientSounds_Update_Entry, nullptr);
HOOK(0x00500840, "fn_00500840", fn_00500840_Entry, nullptr);
HOOK(0x00500890, "fn_00500890", fn_00500890_Entry, nullptr);
HOOK(0x005009b0, "String_CopyBeforeLastUnderscore", String_CopyBeforeLastUnderscore_Entry, nullptr);
HOOK(0x00500a00, "Audio_ResolveSound", Audio_ResolveSound_Entry, nullptr);
HOOK(0x00500d20, "fn_00500d20", fn_00500d20_Entry, nullptr);
