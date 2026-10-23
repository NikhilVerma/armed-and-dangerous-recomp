// vk_shader_translate.cpp: D3D9 shader bytecode (shader models 1.1 to 2.0) to GLSL 450 for the Vulkan backend.
// Conventions (bindings, attribute and varying locations, clip space): vk_abi.h and vk_shaders.h.
//
// Supported
//   vs_1_1, vs_2_0: dcl (inputs -> VertexLocation), def, mov, add, sub, mad, mul, rcp, rsq, dp3, dp4, min, max,
//     slt, sge, exp, log, lit, dst, frc, expp, logp (both the vs_1_1 vector forms and the 2.0 scalar forms),
//     m4x4, m4x3, m3x4, m3x3, m3x2, mova, abs, nrm, pow, crs, sincos, sgn, lrp, dp2add; registers r, v, c (with
//     a0-relative addressing), a0, oPos, oD0/oD1, oT0..7, oFog, oPts; source swizzles and negate; _sat and
//     shifted results.
//   ps_1_1 .. ps_1_3: tex, texcoord, texkill, texbem, texbeml, texreg2ar, texreg2gb, texreg2rgb, texm3x2pad,
//     texm3x2tex, texm3x2depth, texm3x3pad, texm3x3tex, texm3x3spec, texm3x3vspec, texm3x3, texdp3, texdp3tex;
//     arithmetic mov add sub mul mad lrp dp3 dp4 cnd cmp nop; co-issue (+); every source modifier (bias,
//     invert, negate, bx2) and result modifier (_x2 _x4 _d2 _sat); register range clamp [-1, 1].
//   ps_1_4: texld, texcrd (with _dz/_dw), texkill, texdepth, bem, phase, the arithmetic above; range [-8, 8].
//   ps_2_0: dcl (2D and cube samplers, t/v inputs), texld, texldp, texldb, texkill, and the arithmetic of
//     vs_2_0 that ps_2_0 allows; oC0 and oDepth.
// Not supported (translation fails with an error naming the instruction)
//   Flow control (rep, loop, if, call, label, break, ...; vs_2_0 static flow control and the 2.x/3.0
//   models), predication, integer and boolean constants other than defi/defb (no binding for them in vk_abi.h),
//   volume textures (no sampler3D binding in vk_abi.h), multiple render targets (oC1..3), dsx/dsy/texldd/texldl.
// Known approximations, documented where they apply
//   * ps_1_1..1_3 `tex` always samples a 2D texture and ignores D3DTTFF_PROJECTED (neither the bound texture's
//     type nor the texture stage flags are part of the translation input). Cube maps are used only where the
//     instruction implies one (texm3x3tex/spec/vspec).
//   * Unwritten vertex outputs: oD0 = (1,1,1,1), oD1 = (0,0,0,0), oT = (0,0,0,0), oFog = 1 (no fog), oPts = 1.
//     D3D9 leaves them undefined; these are the values the reference rasterizer and wined3d produce, and they
//     keep a fixed-function pixel half that reads diffuse/specular harmless.
//
// The D3D9 bytecode format relied on (public D3D9 shader bytecode reference / DDK d3d9types.h):
//   token 0           version: 0xFFFEmmnn (vertex) or 0xFFFFmmnn (pixel), mm major, nn minor.
//   instruction token bits 0-15 opcode (D3DSIO_*), 16-23 opcode-specific control (texld project/bias,
//                     comparison), 24-27 operand token count (shader model 2+ only; 0 in SM1, where the count
//                     is fixed per opcode: Sm1OperandTokens below), 28 predicated, 30 co-issue (ps_1_x).
//   comment           opcode 0xFFFE, bits 16-30 = number of following DWORDs to skip.
//   end               0x0000FFFF.
//   register token    bits 0-10 register number, bits 28-30 | 11-12 (<<3) register type (D3DSPR_*), bit 13
//                     relative addressing (SM1 vertex shaders: implicitly a0.x, no extra token; SM2+: one
//                     extra token follows that names the address register and component), bit 31 always 1.
//     destination     bits 16-19 write mask (x y z w), 20-23 result modifier (D3DSPDM_SATURATE, partial
//                     precision, centroid), 24-27 signed shift (1 = x2, 2 = x4, 3 = x8, 15 = d2, 14 = d4, 13 = d8).
//     source          bits 16-23 swizzle (2 bits per output component), 24-27 modifier (D3DSPSM_*).
//   dcl               dcl token, usage token (bits 0-4 D3DDECLUSAGE, 16-19 usage index, 27-30 sampler texture
//                     type for samplers), destination token.
//   def               def token, destination token (a c register), four IEEE floats.
#include "vk_shaders.h"
#include "vk_fog.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace rhi::vulkan
{
float g_terrainDetail = 0.0f;          // [render] terrain_detail (see the terrain detail note below)
float g_terrainDetailStrength = 0.6f;
namespace
{
// Upper bound for a shader's length in tokens: the bytecode has no length field, only the end token, so a corrupt
// stream must not walk off into memory forever. (D3D9's largest shaders are a few thousand tokens.)
constexpr size_t kMaxTokens = 1u << 18;

uint32_t RegType(uint32_t t)
{
    return ((t & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT) | ((t & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
}
uint32_t RegNum(uint32_t t) { return t & D3DSP_REGNUM_MASK; }
uint32_t WriteMask(uint32_t t) { return (t & D3DSP_WRITEMASK_ALL) >> 16; }

// Operand tokens per instruction in shader model 1 (no length field there). -1: not a shader model 1 opcode.
int Sm1OperandTokens(uint32_t op, bool pixel, uint32_t minor)
{
    switch (op)
    {
    case D3DSIO_NOP:
    case D3DSIO_PHASE: return 0;
    case D3DSIO_TEXKILL:
    case D3DSIO_TEXDEPTH: return 1;
    case D3DSIO_TEX:
    case D3DSIO_TEXCOORD: return pixel && minor >= 4 ? 2 : 1;  // ps_1_4 texld/texcrd take a source register
    case D3DSIO_MOV:
    case D3DSIO_RCP:
    case D3DSIO_RSQ:
    case D3DSIO_EXP:
    case D3DSIO_LOG:
    case D3DSIO_LIT:
    case D3DSIO_FRC:
    case D3DSIO_EXPP:
    case D3DSIO_LOGP:
    case D3DSIO_DCL:
    case D3DSIO_TEXBEM:
    case D3DSIO_TEXBEML:
    case D3DSIO_TEXREG2AR:
    case D3DSIO_TEXREG2GB:
    case D3DSIO_TEXREG2RGB:
    case D3DSIO_TEXM3x2PAD:
    case D3DSIO_TEXM3x2TEX:
    case D3DSIO_TEXM3x2DEPTH:
    case D3DSIO_TEXM3x3PAD:
    case D3DSIO_TEXM3x3TEX:
    case D3DSIO_TEXM3x3VSPEC:
    case D3DSIO_TEXM3x3:
    case D3DSIO_TEXDP3:
    case D3DSIO_TEXDP3TEX: return 2;
    case D3DSIO_ADD:
    case D3DSIO_SUB:
    case D3DSIO_MUL:
    case D3DSIO_DP3:
    case D3DSIO_DP4:
    case D3DSIO_MIN:
    case D3DSIO_MAX:
    case D3DSIO_SLT:
    case D3DSIO_SGE:
    case D3DSIO_DST:
    case D3DSIO_M4x4:
    case D3DSIO_M4x3:
    case D3DSIO_M3x4:
    case D3DSIO_M3x3:
    case D3DSIO_M3x2:
    case D3DSIO_BEM:
    case D3DSIO_TEXM3x3SPEC: return 3;
    case D3DSIO_MAD:
    case D3DSIO_LRP:
    case D3DSIO_CND:
    case D3DSIO_CMP: return 4;
    case D3DSIO_DEF: return 5;
    default: return -1;
    }
}

const char* OpName(uint32_t op)
{
    switch (op)
    {
    case D3DSIO_CALL: return "call";
    case D3DSIO_CALLNZ: return "callnz";
    case D3DSIO_LOOP: return "loop";
    case D3DSIO_RET: return "ret";
    case D3DSIO_ENDLOOP: return "endloop";
    case D3DSIO_LABEL: return "label";
    case D3DSIO_REP: return "rep";
    case D3DSIO_ENDREP: return "endrep";
    case D3DSIO_IF: return "if";
    case D3DSIO_IFC: return "ifc";
    case D3DSIO_ELSE: return "else";
    case D3DSIO_ENDIF: return "endif";
    case D3DSIO_BREAK: return "break";
    case D3DSIO_BREAKC: return "breakc";
    case D3DSIO_BREAKP: return "breakp";
    case D3DSIO_SETP: return "setp";
    case D3DSIO_DSX: return "dsx";
    case D3DSIO_DSY: return "dsy";
    case D3DSIO_TEXLDD: return "texldd";
    case D3DSIO_TEXLDL: return "texldl";
    default: return "?";
    }
}

// A float as a GLSL literal that reads back to the same bits.
std::string FloatLit(float f)
{
    if (!std::isfinite(f))
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return std::format("uintBitsToFloat(0x{:08x}u)", bits);
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(f));
    std::string s = buf;
    if (s.find_first_of(".e") == std::string::npos)
        s += ".0";
    return s;
}

// The FixedFunction block (binding 2), mirrored member by member from FFUniforms with explicit offsets taken from
// the C++ struct, so the two cannot drift apart. Instance name `ff`. (`texture` is spelled textureMatrix: it is a
// GLSL built-in function name.)
std::string FixedFunctionBlock()
{
    struct Member
    {
        const char* decl;
        size_t offset;
    };
#define FF_MEMBER(decl, field) Member{decl, offsetof(FFUniforms, field)}
    const Member members[] = {
        FF_MEMBER("mat4 world[4]", world),
        FF_MEMBER("mat4 view", view),
        FF_MEMBER("mat4 projection", projection),
        FF_MEMBER("mat4 textureMatrix[8]", texture),
        FF_MEMBER("vec4 halfPixel", halfPixel),
        FF_MEMBER("vec4 viewportSize", viewportSize),
        FF_MEMBER("vec4 textureFactor", textureFactor),
        FF_MEMBER("vec4 fogColor", fogColor),
        FF_MEMBER("vec4 fogParams", fogParams),
        FF_MEMBER("vec4 alphaRef", alphaRef),
        FF_MEMBER("vec4 ambient", ambient),
        FF_MEMBER("vec4 materialDiffuse", materialDiffuse),
        FF_MEMBER("vec4 materialAmbient", materialAmbient),
        FF_MEMBER("vec4 materialSpecular", materialSpecular),
        FF_MEMBER("vec4 materialEmissive", materialEmissive),
        FF_MEMBER("vec4 lightDiffuse[8]", lightDiffuse),
        FF_MEMBER("vec4 lightSpecular[8]", lightSpecular),
        FF_MEMBER("vec4 lightAmbient[8]", lightAmbient),
        FF_MEMBER("vec4 lightPosition[8]", lightPosition),
        FF_MEMBER("vec4 lightDirection[8]", lightDirection),
        FF_MEMBER("vec4 lightAttenuation[8]", lightAttenuation),
        FF_MEMBER("vec4 lightSpot[8]", lightSpot),
        FF_MEMBER("vec4 textureStageConstant[8]", textureStageConstant),
        FF_MEMBER("vec4 bumpEnvMat[8]", bumpEnvMat),
        FF_MEMBER("vec4 bumpEnvLum[8]", bumpEnvLum),
        FF_MEMBER("vec4 tessParams", tessParams),
        FF_MEMBER("ModernFog mf", mf),
        FF_MEMBER("vec4 soft", soft),
    };
#undef FF_MEMBER
    std::string s = std::string("#ifndef MODERN_FOG_STRUCT\n#define MODERN_FOG_STRUCT\nstruct ModernFog\n{\n") +
                    ModernFogMembers() + "};\n#endif\n";
    s += std::format("layout(std140, set = 0, binding = {}) uniform FixedFunction\n{{\n", kBindingFixedFunction);
    for (const Member& m : members)
        s += std::format("    layout(offset = {}) {};\n", m.offset, m.decl);
    return s + "} ff;\n";
}

// The PrevFrame block (binding 3, vk_abi.h "Motion vectors"), mirrored from PrevUniforms with offsets taken from the
// C++ struct like FixedFunctionBlock. Instance name `pf`. Translated shaders read only vsConstants.
std::string PrevFrameBlock()
{
    return std::format("layout(std140, set = 0, binding = {}) uniform PrevFrame\n{{\n"
                       "    layout(offset = {}) mat4 world[4];\n"
                       "    layout(offset = {}) mat4 view;\n"
                       "    layout(offset = {}) mat4 projection;\n"
                       "    layout(offset = {}) vec4 vsConstants[{}];\n"
                       "}} pf;\n",
                       kBindingPrevFrame, offsetof(PrevUniforms, world), offsetof(PrevUniforms, view),
                       offsetof(PrevUniforms, projection), offsetof(PrevUniforms, vsConstants), kVsConstantCount);
}

// D3D9 arithmetic where GLSL differs (D3D9 reference pseudo-code for rcp, rsq, log, lit, dst, expp, logp).
// Vulkan does not promise IEEE infinities from 1/0, so the infinities are spelled out.
constexpr const char* kHelpers = R"(#define D3D_INF uintBitsToFloat(0x7f800000u)
float d3d_rcp(float x) { return x == 1.0 ? 1.0 : (x == 0.0 ? D3D_INF : 1.0 / x); }
float d3d_rsq(float x) { float a = abs(x); return a == 1.0 ? 1.0 : (a == 0.0 ? D3D_INF : inversesqrt(a)); }
float d3d_log(float x) { float a = abs(x); return a == 0.0 ? -D3D_INF : log2(a); }
vec4 d3d_lit(vec4 s)
{
    vec4 d = vec4(1.0, 0.0, 0.0, 1.0);
    float power = clamp(s.w, -127.9961, 127.9961);
    if (s.x > 0.0)
    {
        d.y = s.x;
        if (s.y > 0.0)
            d.z = exp2(power * log2(s.y));
    }
    return d;
}
vec4 d3d_dst(vec4 a, vec4 b) { return vec4(1.0, a.y * b.y, a.z, b.w); }
vec4 d3d_expp(float x) { float f = floor(x); return vec4(exp2(f), x - f, exp2(x), 1.0); }
vec4 d3d_logp(float x)
{
    float a = abs(x);
    if (a == 0.0)
        return vec4(-D3D_INF, 1.0, -D3D_INF, 1.0);
    int e;
    float m = frexp(a, e);  // a = m * 2^e, m in [0.5, 1)
    return vec4(float(e - 1), m * 2.0, log2(a), 1.0);
}
vec4 d3d_dz(vec4 v) { return vec4(v.xy / v.z, v.z, v.w); }
vec4 d3d_dw(vec4 v) { return vec4(v.xy / v.w, v.z, v.w); }
)";

struct Operand
{
    uint32_t token = 0;
    uint32_t relToken = 0;  // SM2+: the address register of a relative operand
    bool hasRelToken = false;
};

struct Instruction
{
    uint32_t opcode = 0;
    uint32_t control = 0;  // bits 16-23 of the instruction token
    bool coissue = false;
    size_t offset = 0;  // token index, for error messages
    std::vector<Operand> ops;        // in token order: the destination (if any) first
    std::vector<uint32_t> literal;   // dcl usage token; def/defi/defb values
};

// Code for one instruction: what it computes and how it stores the result, kept apart so that a co-issued pair
// (ps_1_x `+`) reads both instructions' sources before either writes.
struct Emitted
{
    std::string compute;
    std::string write;
    bool coissue = false;
};

// Ambient-occlusion material outputs (vk_abi.h): mat_aoWeight = the share of the pixel's colour that comes from the
// ambient term (occlusion applies to that share only), mat_albedo = the surface albedo's luminance (-1: unknown).
// Armed and Dangerous creates three pixel shaders (work/shaders, [test] dump_shaders=1; hashes as DumpShader in
// src/rhi/d3d9_facade.cpp). Each lights as  albedo * (N.L * c1 + v0) [+ v1 + specular]: v0 (vertex colour) is the
// ambient; the N.L term (sun), v1 (an added glow term) and the specular highlight are not occluded.
// Remaster terrain detail ([render] terrain_detail, set by the device at start; 0 = the original shader). The terrain
// pixel shader's normal map (t0) is stretched over several metres per tile, so its relief reads as broad soft blobs
// up close. With a scale K > 1 the same normal map is sampled again at K times the tiling and its slope added to the
// base slope (a detail layer); mip filtering fades it out with distance. Values: g_terrainDetail above.

std::string MaterialOutputs(uint64_t hash)
{
    const char* albedo = nullptr;
    switch (hash)
    {
    case 0x731b0a29671b0f18ull: albedo = "t0.rgb"; break;  // bump + specular + v1 glow: t0 base, t1 normals
    case 0x98a72838035bad82ull:                              // terrain: t1 / t2 blended by t3, t0 normals
        albedo = "clamp(t2.rgb * t3.rgb + t1.rgb * (1.0 - t3.rgb), 0.0, 1.0)";
        break;
    case 0xeac78aec9a00d1cfull: albedo = "t0.rgb"; break;  // bump diffuse: t0 base, t1 normals
    default: break;
    }
    if (!albedo)
        return "    float mat_aoWeight = 1.0, mat_albedo = -1.0;\n";
    return std::string("    vec3 mat_a = ") + albedo + ";\n" +
           "    const vec3 mat_lum = vec3(0.2126, 0.7152, 0.0722);\n"
           "    float mat_albedo = dot(mat_a, mat_lum);\n"
           "    float mat_aoWeight = clamp(dot(mat_a * v0.rgb, mat_lum) / max(dot(result.rgb, mat_lum), 1e-3), 0.0, "
           "1.0);\n";
}

class Translator
{
public:
    explicit Translator(const uint32_t* code) : code_(code)
    {
        // FNV-1a over the bytecode up to and including the end token, as DumpShader names the corpus files.
        size_t n = 0;
        while (code && code[n] != 0x0000ffffu && n < 65536)
            ++n;
        const unsigned char* b = reinterpret_cast<const unsigned char*>(code);
        for (size_t i = 0; code && i < (n + 1) * 4; ++i)
            hash_ = (hash_ ^ b[i]) * 1099511628211ull;
    }

    TranslatedShader Run()
    {
        if (!code_)
            return Failed("null bytecode");
        uint32_t version = code_[0];
        uint32_t kind = version >> 16;
        if (kind != 0xFFFE && kind != 0xFFFF)
            return Failed(std::format("bad version token 0x{:08x}", version));
        pixel_ = kind == 0xFFFF;
        major_ = (version >> 8) & 0xFF;
        minor_ = version & 0xFF;
        bool supported = pixel_ ? (major_ == 1 && minor_ >= 1 && minor_ <= 4) || (major_ == 2 && minor_ == 0)
                                : (major_ == 1 && minor_ == 1) || (major_ == 2 && minor_ == 0);
        if (!supported)
            return Failed(std::format("unsupported shader model {}_{}_{}", pixel_ ? "ps" : "vs", major_, minor_));
        ps1x_ = pixel_ && major_ == 1;
        psRange_ = minor_ >= 4 ? "8.0" : "1.0";
        result_.isPixel = pixel_;

        if (!Parse())
            return Failed(error_);
        // Pass 1: declarations (dcl, def) so that every later read sees them.
        for (const Instruction& in : instrs_)
            if (!Declare(in))
                return Failed(error_);
        // Pass 2: code.
        for (const Instruction& in : instrs_)
            if (!Generate(in))
                return Failed(error_);
        // Vertex shaders, pass 3 (motion vectors, vk_abi.h): the same program again with the previous frame's
        // constants (PrevFrame vsConstants), evaluated in PrevPosition() for the previous clip position. The current
        // evaluation's code (pass 2) is left exactly as it was.
        if (!pixel_)
        {
            std::vector<Emitted> current = std::move(out_);
            out_.clear();
            prev_ = true;
            for (const Instruction& in : instrs_)
                if (!Generate(in))
                    return Failed(error_);
            prev_ = false;
            prevOut_ = std::move(out_);
            // Pass 4 (tessellation, vk_abi.h): current constants, position input moved by `posOffset`, evaluated in
            // OffsetPosition() to turn the vertex normal into a camera-space direction.
            out_.clear();
            offset_ = true;
            for (const Instruction& in : instrs_)
                if (!Generate(in))
                    return Failed(error_);
            offset_ = false;
            offsetOut_ = std::move(out_);
            out_ = std::move(current);
            result_.relativeConstants = needConstFunc_ || needPrevConstFunc_;
            for (uint32_t n = 0; n < 16; ++n)
                if (vsInputLocation_[n] == 3)
                    result_.hasNormal = true;
        }
        result_.glsl = Assemble();
        result_.ok = true;
        return result_;
    }

private:
    // ---- parsing ----------------------------------------------------------------------------------------------

    bool Parse()
    {
        size_t i = 1;
        while (true)
        {
            if (i >= kMaxTokens)
                return Error("no end token");
            uint32_t tok = code_[i];
            uint32_t op = tok & D3DSI_OPCODE_MASK;
            if (op == D3DSIO_COMMENT)
            {
                i += 1 + ((tok & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
                continue;
            }
            if (op == D3DSIO_END)
                return true;
            if (tok & D3DSHADER_INSTRUCTION_PREDICATED)
                return Error(std::format("predicated instruction at token {}", i));
            int count;
            if (major_ >= 2)
                count = static_cast<int>((tok & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT);
            else
            {
                count = Sm1OperandTokens(op, pixel_, minor_);
                if (count < 0)
                    return Error(std::format("unknown shader model 1 opcode {} at token {}", op, i));
            }
            Instruction in;
            in.opcode = op;
            in.control = (tok >> 16) & 0xFF;
            in.coissue = (tok & D3DSI_COISSUE) != 0 && ps1x_;
            in.offset = i;
            const uint32_t* p = code_ + i + 1;
            const uint32_t* end = p + count;
            if (op == D3DSIO_DCL)
            {
                if (count < 2)
                    return Error("short dcl");
                in.literal.push_back(p[0]);
                in.ops.push_back({p[1]});
            }
            else if (op == D3DSIO_DEF || op == D3DSIO_DEFI || op == D3DSIO_DEFB)
            {
                if (count < 2)
                    return Error("short def");
                in.ops.push_back({p[0]});
                in.literal.assign(p + 1, end);
            }
            else
            {
                while (p < end)
                {
                    Operand o{*p++};
                    if (major_ >= 2 && (o.token & D3DSHADER_ADDRMODE_RELATIVE) && p < end)
                        o.relToken = *p++, o.hasRelToken = true;
                    in.ops.push_back(o);
                }
            }
            instrs_.push_back(std::move(in));
            i += 1 + static_cast<size_t>(count);
        }
    }

    // ---- declarations -----------------------------------------------------------------------------------------

    bool Declare(const Instruction& in)
    {
        if (in.opcode == D3DSIO_DEF)
        {
            if (in.literal.size() < 4 || RegType(in.ops[0].token) != D3DSPR_CONST)
                return Error("malformed def");
            float v[4];
            std::memcpy(v, in.literal.data(), sizeof v);
            defs_[RegNum(in.ops[0].token)] = std::format("vec4({}, {}, {}, {})", FloatLit(v[0]), FloatLit(v[1]),
                                                         FloatLit(v[2]), FloatLit(v[3]));
            return true;
        }
        if (in.opcode != D3DSIO_DCL)
            return true;
        uint32_t usageTok = in.literal[0];
        uint32_t reg = in.ops[0].token, type = RegType(reg), num = RegNum(reg);
        if (!pixel_)
        {
            if (type != D3DSPR_INPUT)
                return true;  // vs_3_0 output declarations are not reached (vs_3_0 is rejected above)
            BYTE usage = static_cast<BYTE>(usageTok & D3DSP_DCL_USAGE_MASK);
            BYTE index = static_cast<BYTE>((usageTok & D3DSP_DCL_USAGEINDEX_MASK) >> D3DSP_DCL_USAGEINDEX_SHIFT);
            uint32_t loc = VertexLocation(usage, index);
            if (num >= 16)
                return Error("dcl of input register out of range");
            if (loc != 0xff)
            {
                vsInputLocation_[num] = loc;
                result_.inputMask |= 1u << loc;
            }
            return true;
        }
        if (type == D3DSPR_SAMPLER)
        {
            if (num >= kMaxSamplers)
                return Error("sampler out of range");
            uint32_t tt = usageTok & D3DSP_TEXTURETYPE_MASK;
            if (tt == D3DSTT_CUBE)
                samplerCube_ |= 1u << num;
            else if (tt != D3DSTT_2D)
                return Error(std::format("unsupported sampler type {} (volume textures have no binding)", tt >> 27));
            samplers_ |= 1u << num;
        }
        return true;  // ps_2_0 dcl of t# / v#: the inputs are declared on first use
    }

    // ---- registers --------------------------------------------------------------------------------------------

    std::string Temp(uint32_t n)
    {
        if (!anyTemp_ || n > maxTemp_)
            maxTemp_ = n;
        anyTemp_ = true;
        return std::format("r{}", n);
    }

    std::string TexCoordIn(uint32_t n)
    {
        texCoordsUsed_ |= 1u << n;
        return std::format("vT{}", n);
    }

    std::string ConstRead(uint32_t n)
    {
        auto it = defs_.find(n);
        if (it != defs_.end())
            return it->second;  // def overrides the constant register for this shader
        if (pixel_)
        {
            if (n >= kPsConstantCount)
                return "vec4(0.0)";
            // ps_1_x constant registers hold [-1, 1] (D3D9 clamps the values passed to SetPixelShaderConstantF).
            return ps1x_ ? std::format("clamp(psc.c[{}], -1.0, 1.0)", n) : std::format("psc.c[{}]", n);
        }
        if (n >= kVsConstantCount)
            return "vec4(0.0)";
        return prev_ ? std::format("pf.vsConstants[{}]", n) : std::format("vsc.c[{}]", n);
    }

    // Value of a source register (no swizzle or modifier yet). regOffset: added to the register number (matrix
    // instructions read consecutive constant registers).
    bool SourceBase(const Operand& o, uint32_t regOffset, std::string& out)
    {
        uint32_t t = o.token, type = RegType(t), n = RegNum(t) + regOffset;
        bool relative = (t & D3DSHADER_ADDRMODE_RELATIVE) != 0;
        if (relative && type != D3DSPR_CONST)
            return Error("relative addressing of a non-constant register");
        switch (type)
        {
        case D3DSPR_TEMP:
            out = Temp(n);
            return true;
        case D3DSPR_INPUT:
            if (pixel_)
            {
                if (n > 1)
                    return Error("pixel shader input register out of range");
                colorsUsed_ |= 1u << n;
                out = std::format("v{}", n);
            }
            else
            {
                out = n < 16 && vsInputLocation_[n] != 0xff ? std::format("v{}", n) : std::string("vec4(0.0)");
                if (offset_ && n < 16 && vsInputLocation_[n] == 0)
                    out = std::format("(v{} + posOffset)", n);
            }
            return true;
        case D3DSPR_CONST:
            if (!relative)
            {
                out = ConstRead(n);
                return true;
            }
            if (pixel_)
                return Error("relative constant addressing in a pixel shader");
            {
                std::string index = "a0.x";  // vs_1_1: always a0.x, no extra token
                if (o.hasRelToken)
                {
                    uint32_t rt = o.relToken;
                    if (RegType(rt) != D3DSPR_ADDR)
                        return Error("relative addressing through aL (loops are not supported)");
                    index = std::string("a0.") + "xyzw"[(rt >> D3DVS_SWIZZLE_SHIFT) & 3];
                }
                if (prev_)
                    needPrevConstFunc_ = true;
                else
                    needConstFunc_ = true;
                out = std::format("{}({} + {})", prev_ ? "CP" : "C", index, n);
            }
            return true;
        case D3DSPR_ADDR:  // == D3DSPR_TEXTURE
            if (!pixel_)
            {
                out = "vec4(a0)";
                return true;
            }
            if (n >= 8)
                return Error("texture register out of range");
            if (ps1x_ && minor_ < 4)
            {
                texRegsUsed_ |= 1u << n;
                out = std::format("t{}", n);
            }
            else
                out = TexCoordIn(n);  // ps_1_4, ps_2_0: t# are the interpolated texture coordinates
            return true;
        case D3DSPR_CONSTINT:
        case D3DSPR_CONSTBOOL:
            return Error("integer/boolean constant used outside flow control");
        default:
            return Error(std::format("unsupported source register type {}", type));
        }
    }

    // Full source operand: register, swizzle, modifier.
    bool Src(const Operand& o, std::string& out, uint32_t regOffset = 0)
    {
        std::string base;
        if (!SourceBase(o, regOffset, base))
            return false;
        uint32_t sw = (o.token >> D3DVS_SWIZZLE_SHIFT) & 0xFF;
        if (sw != 0xE4)  // .xyzw
        {
            base += '.';
            for (int c = 0; c < 4; ++c)
                base += "xyzw"[(sw >> (2 * c)) & 3];
        }
        switch (o.token & D3DSP_SRCMOD_MASK)
        {
        case D3DSPSM_NONE: out = base; break;
        case D3DSPSM_NEG: out = "(-" + base + ")"; break;
        case D3DSPSM_BIAS: out = "(" + base + " - 0.5)"; break;
        case D3DSPSM_BIASNEG: out = "(0.5 - " + base + ")"; break;
        case D3DSPSM_SIGN: out = "(2.0 * " + base + " - 1.0)"; break;  // _bx2
        case D3DSPSM_SIGNNEG: out = "(1.0 - 2.0 * " + base + ")"; break;
        case D3DSPSM_COMP: out = "(1.0 - " + base + ")"; break;  // invert
        case D3DSPSM_X2: out = "(2.0 * " + base + ")"; break;
        case D3DSPSM_X2NEG: out = "(-2.0 * " + base + ")"; break;
        case D3DSPSM_DZ: out = "d3d_dz(" + base + ")"; break;
        case D3DSPSM_DW: out = "d3d_dw(" + base + ")"; break;
        case D3DSPSM_ABS: out = "abs(" + base + ")"; break;
        case D3DSPSM_ABSNEG: out = "(-abs(" + base + "))"; break;
        default: return Error("unsupported source modifier (not)");
        }
        return true;
    }

    // Store `value` (a vec4 expression) to the destination with its write mask and result modifiers.
    // maskLimit: components the instruction can produce (m3x3 writes xyz, ...).
    bool Write(const Operand& d, const std::string& value, std::string& out, uint32_t maskLimit = 0xF)
    {
        uint32_t t = d.token, type = RegType(t), n = RegNum(t);
        uint32_t mask = WriteMask(t) & maskLimit;
        std::string v = value;
        int shift = static_cast<int>((t & D3DSP_DSTSHIFT_MASK) >> D3DSP_DSTSHIFT_SHIFT);
        if (shift >= 8)
            shift -= 16;
        if (shift)
            v = std::format("({} * {})", v, FloatLit(std::ldexp(1.0f, shift)));
        if (t & D3DSPDM_SATURATE)
            v = std::format("clamp({}, 0.0, 1.0)", v);
        else if (ps1x_ && (type == D3DSPR_TEMP || type == D3DSPR_TEXTURE))
            v = std::format("clamp({}, -{}, {})", v, psRange_, psRange_);  // ps_1_x register range
        std::string name;
        switch (type)
        {
        case D3DSPR_TEMP: name = Temp(n); break;
        case D3DSPR_ADDR:  // == D3DSPR_TEXTURE
            if (!pixel_)
            {
                // vs_1_1 `mov a0` floors; vs_2_0 `mova` rounds to nearest.
                std::string conv = major_ >= 2 ? "floor(" + v + " + 0.5)" : "floor(" + v + ")";
                for (int c = 0; c < 4; ++c)
                    if (mask & (1u << c))
                        out += std::format("    a0.{} = int({}.{});\n", "xyzw"[c], conv, "xyzw"[c]);
                return true;
            }
            if (n >= 8)
                return Error("texture register out of range");
            texRegsUsed_ |= 1u << n;
            name = std::format("t{}", n);
            break;
        case D3DSPR_RASTOUT:
            if (pixel_ || n > 2)
                return Error("bad rasterizer output");
            name = n == D3DSRO_POSITION ? "o_Pos" : n == D3DSRO_FOG ? "o_Fog" : "o_Pts";
            break;
        case D3DSPR_ATTROUT:
            if (n > 1)
                return Error("bad color output");
            name = std::format("o_D{}", n);
            break;
        case D3DSPR_TEXCRDOUT:
            if (pixel_ || n > 7)
                return Error("bad texture coordinate output");
            name = std::format("o_T{}", n);
            break;
        case D3DSPR_COLOROUT:
            if (n != 0)
                return Error("multiple render targets (oC1..3) are not supported");
            name = "oC0";
            break;
        case D3DSPR_DEPTHOUT:
            writesDepth_ = true;
            out += std::format("    gl_FragDepth = clamp(({}).x, 0.0, 1.0);\n", v);
            return true;
        default: return Error(std::format("unsupported destination register type {}", type));
        }
        if (mask == 0xF || mask == 0)
            out += std::format("    {} = {};\n", name, v);
        else
        {
            std::string sw;
            for (int c = 0; c < 4; ++c)
                if (mask & (1u << c))
                    sw += "xyzw"[c];
            out += std::format("    {}.{} = ({}).{};\n", name, sw, v, sw);
        }
        return true;
    }

    // ---- instructions -----------------------------------------------------------------------------------------

    // An arithmetic instruction: the destination gets `expr`.
    bool Arith(const Instruction& in, const std::string& expr, uint32_t maskLimit = 0xF)
    {
        std::string tmp = std::format("x{}", tmpCounter_++);
        Emitted e;
        e.compute = std::format("    vec4 {} = {};\n", tmp, expr);
        if (!Write(in.ops[0], tmp, e.write, maskLimit))
            return false;
        e.coissue = in.coissue;
        out_.push_back(std::move(e));
        return true;
    }

    void Statement(std::string code) { out_.push_back({std::move(code), {}, false}); }

    std::string Sampler(uint32_t n, bool cube)
    {
        samplers_ |= 1u << n;
        if (cube)
            samplerCube_ |= 1u << n;
        return std::format("s{}", n);
    }

    bool NeedOps(const Instruction& in, size_t n)
    {
        if (in.ops.size() < n)
            return Error(std::format("instruction {} at token {} has {} operands, expected {}", in.opcode, in.offset,
                                     in.ops.size(), n));
        return true;
    }

    // ps_1_1..1_3 texture-stage instructions: destination t<m> (m is also the sampler and texture coordinate set),
    // source t<n> where the instruction has one.
    bool TextureOp(const Instruction& in)
    {
        uint32_t m = RegNum(in.ops[0].token);
        if (m >= 8 || (in.ops.size() > 1 && RegNum(in.ops[1].token) >= 8))
            return Error("texture register out of range");
        std::string dst = std::format("t{}", m);
        texRegsUsed_ |= 1u << m;
        std::string tn;
        if (in.ops.size() > 1 && in.opcode != D3DSIO_TEXKILL)
        {
            uint32_t n = RegNum(in.ops[1].token);
            texRegsUsed_ |= 1u << n;
            tn = std::format("t{}", n);
        }
        std::string tc = TexCoordIn(m);
        auto dotRow = [&] { return std::format("dot({}.xyz, {}.xyz)", tc, tn); };
        switch (in.opcode)
        {
        case D3DSIO_TEX:
            Statement(std::format("    {} = texture({}, {}.xy);\n", dst, Sampler(m, false), tc));
            if (hash_ == 0x98a72838035bad82ull && m == 0 && g_terrainDetail > 1.0f)  // terrain normals: detail layer
                Statement(std::format(
                    "    {{ vec3 tdn = {0}.xyz * 2.0 - 1.0; vec3 tdd = texture({1}, {2}.xy * {3}).xyz * 2.0 - 1.0;\n"
                    "      tdn = normalize(vec3(tdn.xy + tdd.xy * {4}, tdn.z)); {0}.xyz = tdn * 0.5 + 0.5; }}\n",
                    dst, Sampler(m, false), tc, FloatLit(g_terrainDetail), FloatLit(g_terrainDetailStrength)));
            return true;
        case D3DSIO_TEXCOORD:  // the coordinates as a color: uvw clamped to [0, 1], alpha 1
            Statement(std::format("    {} = vec4(clamp({}.xyz, 0.0, 1.0), 1.0);\n", dst, tc));
            return true;
        case D3DSIO_TEXKILL:  // ps_1_1..1_3: tests the texture coordinates, not a sampled value
            Statement(std::format("    if (any(lessThan({}.xyz, vec3(0.0)))) discard;\n", tc));
            return true;
        case D3DSIO_TEXBEM:
        case D3DSIO_TEXBEML:
        {
            // u' = u + M00 r + M10 g, v' = v + M01 r + M11 g (bumpEnvMat[m] = 00, 01, 10, 11)
            std::string s = std::format(
                "    {} = texture({}, {}.xy + vec2(dot(ff.bumpEnvMat[{}].xz, {}.xy), dot(ff.bumpEnvMat[{}].yw, {}.xy)));\n",
                dst, Sampler(m, false), tc, m, tn, m, tn);
            if (in.opcode == D3DSIO_TEXBEML)  // luminance: b * LSCALE + LOFFSET, saturated
                s += std::format("    {}.rgb *= clamp({}.z * ff.bumpEnvLum[{}].x + ff.bumpEnvLum[{}].y, 0.0, 1.0);\n",
                                 dst, tn, m, m);
            Statement(s);
            return true;
        }
        case D3DSIO_TEXREG2AR:
            Statement(std::format("    {} = texture({}, {}.wx);\n", dst, Sampler(m, false), tn));
            return true;
        case D3DSIO_TEXREG2GB:
            Statement(std::format("    {} = texture({}, {}.yz);\n", dst, Sampler(m, false), tn));
            return true;
        case D3DSIO_TEXREG2RGB:  // 2D binding: the r and g components address it
            Statement(std::format("    {} = texture({}, {}.xy);\n", dst, Sampler(m, false), tn));
            return true;
        case D3DSIO_TEXDP3:
            Statement(std::format("    {} = vec4({});\n", dst, dotRow()));
            return true;
        case D3DSIO_TEXDP3TEX:
            Statement(std::format("    {} = texture({}, vec2({}, 0.0));\n", dst, Sampler(m, false), dotRow()));
            return true;
        case D3DSIO_TEXM3x2PAD:
        case D3DSIO_TEXM3x3PAD:
            if (padIndex_ > 1)
                return Error("texm3x*pad chain too long");
            Statement(std::format("    tm.{} = {};\n", "xy"[padIndex_], dotRow()));
            needMatrixVec_ = true;
            ++padIndex_;
            return true;
        case D3DSIO_TEXM3x2TEX:
        case D3DSIO_TEXM3x2DEPTH:
            if (padIndex_ != 1)
                return Error("texm3x2 without its pad");
            padIndex_ = 0;
            if (in.opcode == D3DSIO_TEXM3x2TEX)
                Statement(std::format("    tm.y = {};\n    {} = texture({}, tm.xy);\n", dotRow(), dst, Sampler(m, false)));
            else
            {
                writesDepth_ = true;
                Statement(std::format("    tm.y = {};\n    gl_FragDepth = clamp(tm.y == 0.0 ? 1.0 : tm.x / tm.y, 0.0, 1.0);\n"
                                      "    {} = vec4(0.0);\n",
                                      dotRow(), dst));
            }
            return true;
        case D3DSIO_TEXM3x3TEX:
        case D3DSIO_TEXM3x3:
        case D3DSIO_TEXM3x3SPEC:
        case D3DSIO_TEXM3x3VSPEC:
        {
            if (padIndex_ != 2)
                return Error("texm3x3 without its two pads");
            padIndex_ = 0;
            std::string s = std::format("    tm.z = {};\n", dotRow());
            if (in.opcode == D3DSIO_TEXM3x3)
                s += std::format("    {} = vec4(tm, 1.0);\n", dst);
            else if (in.opcode == D3DSIO_TEXM3x3TEX)
                s += std::format("    {} = texture({}, tm);\n", dst, Sampler(m, true));
            else
            {
                // Reflection of the eye vector E about the normal N = tm: 2 N (N.E) / (N.N) - E.
                std::string eye;
                if (in.opcode == D3DSIO_TEXM3x3SPEC)
                {
                    if (!NeedOps(in, 3) || !Src(in.ops[2], eye))
                        return false;
                    eye = "(" + eye + ").xyz";
                }
                else
                {
                    if (m < 2)
                        return Error("texm3x3vspec register out of range");
                    eye = std::format("vec3({}.w, {}.w, {}.w)", TexCoordIn(m - 2), TexCoordIn(m - 1), tc);
                }
                s += std::format("    {{ vec3 e = {}; vec3 rv = 2.0 * tm * dot(tm, e) / dot(tm, tm) - e;\n"
                                 "      {} = texture({}, rv); }}\n",
                                 eye, dst, Sampler(m, true));
            }
            Statement(s);
            return true;
        }
        default: return Error("unexpected texture instruction");
        }
    }

    // ps_1_4 and ps_2_0 texture instructions (destination is an r register).
    bool TextureOpModern(const Instruction& in)
    {
        if (in.opcode == D3DSIO_TEXKILL)
        {
            // ps_1_4: r# (a value) or t# (coordinates); ps_2_0: r# or t# (both values). Operand is a dest token.
            uint32_t t = in.ops[0].token, n = RegNum(t);
            std::string v = RegType(t) == D3DSPR_TEXTURE ? TexCoordIn(n) : Temp(n);
            Statement(std::format("    if (any(lessThan({}.xyz, vec3(0.0)))) discard;\n", v));
            return true;
        }
        if (in.opcode == D3DSIO_TEXDEPTH)  // ps_1_4: depth = r5.r / r5.g
        {
            writesDepth_ = true;
            std::string r = Temp(RegNum(in.ops[0].token));
            Statement(std::format("    gl_FragDepth = clamp({}.y == 0.0 ? 1.0 : {}.x / {}.y, 0.0, 1.0);\n", r, r, r));
            return true;
        }
        if (!NeedOps(in, 2))
            return false;
        std::string coord;
        if (in.opcode == D3DSIO_TEXCOORD)  // ps_1_4 texcrd: copies coordinates (no clamp)
        {
            if (!Src(in.ops[1], coord))
                return false;
            return Arith(in, std::format("vec4(({}).xyz, 1.0)", coord));
        }
        // texld
        if (!Src(in.ops[1], coord))
            return false;
        uint32_t s;
        bool project = false, bias = false;
        if (major_ == 1)
            s = RegNum(in.ops[0].token);  // ps_1_4: the sampler is the destination register's number
        else
        {
            if (!NeedOps(in, 3) || RegType(in.ops[2].token) != D3DSPR_SAMPLER)
                return Error("texld without a sampler");
            s = RegNum(in.ops[2].token);
            project = (in.control << 16) == D3DSI_TEXLD_PROJECT;
            bias = (in.control << 16) == D3DSI_TEXLD_BIAS;
        }
        if (s >= kMaxSamplers)
            return Error("sampler out of range");
        bool cube = (samplerCube_ >> s) & 1;
        std::string name = Sampler(s, cube);
        std::string c = "(" + coord + ")";
        std::string expr;
        if (cube)
            expr = project ? std::format("texture({}, {}.xyz / {}.w)", name, c, c)
                 : bias    ? std::format("texture({}, {}.xyz, {}.w)", name, c, c)
                           : std::format("texture({}, {}.xyz)", name, c);
        else
            expr = project ? std::format("textureProj({}, {}.xyw)", name, c)
                 : bias    ? std::format("texture({}, {}.xy, {}.w)", name, c, c)
                           : std::format("texture({}, {}.xy)", name, c);
        return Arith(in, expr);
    }

    bool Generate(const Instruction& in)
    {
        uint32_t op = in.opcode;
        if (op == D3DSIO_DCL || op == D3DSIO_DEF || op == D3DSIO_DEFI || op == D3DSIO_DEFB || op == D3DSIO_NOP)
            return true;
        if (op == D3DSIO_PHASE)  // ps_1_4: registers carry across the phase marker in this translation
            return true;
        if (pixel_)
        {
            switch (op)
            {
            case D3DSIO_TEX:
            case D3DSIO_TEXCOORD:
            case D3DSIO_TEXKILL:
            case D3DSIO_TEXDEPTH:
            case D3DSIO_TEXBEM:
            case D3DSIO_TEXBEML:
            case D3DSIO_TEXREG2AR:
            case D3DSIO_TEXREG2GB:
            case D3DSIO_TEXREG2RGB:
            case D3DSIO_TEXM3x2PAD:
            case D3DSIO_TEXM3x2TEX:
            case D3DSIO_TEXM3x2DEPTH:
            case D3DSIO_TEXM3x3PAD:
            case D3DSIO_TEXM3x3TEX:
            case D3DSIO_TEXM3x3:
            case D3DSIO_TEXM3x3SPEC:
            case D3DSIO_TEXM3x3VSPEC:
            case D3DSIO_TEXDP3:
            case D3DSIO_TEXDP3TEX:
                if (!NeedOps(in, 1))
                    return false;
                if (ps1x_ && minor_ < 4)
                    return TextureOp(in);
                if (op == D3DSIO_TEX || op == D3DSIO_TEXCOORD || op == D3DSIO_TEXKILL || op == D3DSIO_TEXDEPTH)
                    return TextureOpModern(in);
                return Error("ps_1_1..1_3 texture instruction in a later shader model");
            default: break;
            }
        }

        // Arithmetic: destination + sources.
        if (in.ops.empty())
            return Error(std::format("unsupported instruction {} ({}) at token {}", OpName(op), op, in.offset));
        std::vector<std::string> s(in.ops.size() - 1);
        auto src = [&](size_t i) -> bool {
            if (i + 1 >= in.ops.size())
                return Error(std::format("missing operand at token {}", in.offset));
            return Src(in.ops[i + 1], s[i]);
        };
        auto srcs = [&](size_t n) {
            for (size_t i = 0; i < n; ++i)
                if (!src(i))
                    return false;
            return true;
        };
        // Scalar sources: the replicate swizzle selects the component; without one SM1 reads .w, which is what
        // `.w` of the swizzled value gives in both cases.
        auto scalar = [&](size_t i) { return std::format("({}).w", s[i]); };
        switch (op)
        {
        case D3DSIO_MOV:
        case D3DSIO_MOVA: return srcs(1) && Arith(in, s[0]);
        case D3DSIO_ADD: return srcs(2) && Arith(in, std::format("{} + {}", s[0], s[1]));
        case D3DSIO_SUB: return srcs(2) && Arith(in, std::format("{} - {}", s[0], s[1]));
        case D3DSIO_MUL: return srcs(2) && Arith(in, std::format("{} * {}", s[0], s[1]));
        case D3DSIO_MAD: return srcs(3) && Arith(in, std::format("{} * {} + {}", s[0], s[1], s[2]));
        case D3DSIO_RCP: return srcs(1) && Arith(in, std::format("vec4(d3d_rcp({}))", scalar(0)));
        case D3DSIO_RSQ: return srcs(1) && Arith(in, std::format("vec4(d3d_rsq({}))", scalar(0)));
        case D3DSIO_DP3:
            return srcs(2) && Arith(in, std::format("vec4(dot(({}).xyz, ({}).xyz))", s[0], s[1]));
        case D3DSIO_DP4: return srcs(2) && Arith(in, std::format("vec4(dot({}, {}))", s[0], s[1]));
        case D3DSIO_MIN: return srcs(2) && Arith(in, std::format("min({}, {})", s[0], s[1]));
        case D3DSIO_MAX: return srcs(2) && Arith(in, std::format("max({}, {})", s[0], s[1]));
        case D3DSIO_SLT: return srcs(2) && Arith(in, std::format("vec4(lessThan({}, {}))", s[0], s[1]));
        case D3DSIO_SGE: return srcs(2) && Arith(in, std::format("vec4(greaterThanEqual({}, {}))", s[0], s[1]));
        case D3DSIO_EXP: return srcs(1) && Arith(in, std::format("vec4(exp2({}))", scalar(0)));
        case D3DSIO_LOG: return srcs(1) && Arith(in, std::format("vec4(d3d_log({}))", scalar(0)));
        case D3DSIO_EXPP:  // vs_1_1: the four-component partial-precision form; 2.0: scalar
            return srcs(1) && Arith(in, major_ == 1 ? std::format("d3d_expp({})", scalar(0))
                                                    : std::format("vec4(exp2({}))", scalar(0)));
        case D3DSIO_LOGP:
            return srcs(1) && Arith(in, major_ == 1 ? std::format("d3d_logp({})", scalar(0))
                                                    : std::format("vec4(d3d_log({}))", scalar(0)));
        case D3DSIO_LIT: return srcs(1) && Arith(in, std::format("d3d_lit({})", s[0]));
        case D3DSIO_DST: return srcs(2) && Arith(in, std::format("d3d_dst({}, {})", s[0], s[1]));
        case D3DSIO_FRC: return srcs(1) && Arith(in, std::format("fract({})", s[0]));
        case D3DSIO_ABS: return srcs(1) && Arith(in, std::format("abs({})", s[0]));
        case D3DSIO_SGN: return srcs(1) && Arith(in, std::format("sign({})", s[0]));
        case D3DSIO_NRM:
            return srcs(1) && Arith(in, std::format("{} * d3d_rsq(dot(({}).xyz, ({}).xyz))", s[0], s[0], s[0]));
        case D3DSIO_POW:
            return srcs(2) && Arith(in, std::format("vec4(pow(abs({}), {}))", scalar(0), scalar(1)));
        case D3DSIO_CRS:
            return srcs(2) && Arith(in, std::format("vec4(cross(({}).xyz, ({}).xyz), 0.0)", s[0], s[1]), 0x7);
        case D3DSIO_SINCOS:  // x = cos, y = sin (the 2.0 form's extra Taylor-constant sources are not needed)
            return src(0) && Arith(in, std::format("vec4(cos({}), sin({}), 0.0, 0.0)", scalar(0), scalar(0)), 0x3);
        case D3DSIO_LRP: return srcs(3) && Arith(in, std::format("mix({}, {}, {})", s[2], s[1], s[0]));
        case D3DSIO_DP2ADD:
            return srcs(3) && Arith(in, std::format("vec4(dot(({}).xy, ({}).xy) + ({}).x)", s[0], s[1], s[2]));
        case D3DSIO_CMP:
            return srcs(3) &&
                   Arith(in, std::format("mix({}, {}, greaterThanEqual({}, vec4(0.0)))", s[2], s[1], s[0]));
        case D3DSIO_CND:
            if (!srcs(3))
                return false;
            if (ps1x_ && minor_ < 4)  // 1.1..1.3: one condition, r0.a > 0.5, for all components
                return Arith(in, std::format("(({}).w > 0.5 ? {} : {})", s[0], s[1], s[2]));
            return Arith(in, std::format("mix({}, {}, greaterThan({}, vec4(0.5)))", s[2], s[1], s[0]));
        case D3DSIO_BEM:  // ps_1_4: dst.rg = src0.rg + bump matrix(dst#) * src1.rg
        {
            if (!srcs(2))
                return false;
            uint32_t m = RegNum(in.ops[0].token);
            if (m >= 8)
                return Error("bem register out of range");
            return Arith(in,
                         std::format("vec4(({}).xy + vec2(dot(ff.bumpEnvMat[{}].xz, ({}).xy), dot(ff.bumpEnvMat[{}].yw, ({}).xy)), 0.0, 0.0)",
                                     s[0], m, s[1], m, s[1]),
                         0x3);
        }
        case D3DSIO_M4x4:
        case D3DSIO_M4x3:
        case D3DSIO_M3x4:
        case D3DSIO_M3x3:
        case D3DSIO_M3x2:
        {
            int cols = op == D3DSIO_M4x4 || op == D3DSIO_M4x3 ? 4 : 3;
            int rows = op == D3DSIO_M4x4 || op == D3DSIO_M3x4 ? 4 : op == D3DSIO_M3x2 ? 2 : 3;
            if (!src(0) || !NeedOps(in, 3))
                return false;
            std::string v = cols == 4 ? s[0] : "(" + s[0] + ").xyz";
            std::string e = "vec4(";
            for (int r = 0; r < 4; ++r)
            {
                if (r)
                    e += ", ";
                if (r >= rows)
                {
                    e += "0.0";
                    continue;
                }
                std::string row;
                if (!Src(in.ops[2], row, static_cast<uint32_t>(r)))
                    return false;
                e += cols == 4 ? std::format("dot({}, {})", v, row) : std::format("dot({}, ({}).xyz)", v, row);
            }
            return Arith(in, e + ")", (1u << rows) - 1);
        }
        default:
            return Error(std::format("unsupported instruction {} ({}) at token {}", OpName(op), op, in.offset));
        }
    }

    // ---- output -----------------------------------------------------------------------------------------------

    std::string Assemble()
    {
        std::string g = "#version 450\n";
        g += std::format("// Translated from D3D9 {}_{}_{} bytecode by vk_shader_translate.cpp.\n", pixel_ ? "ps" : "vs",
                         major_, minor_);
        g += std::format("layout(std140, set = 0, binding = {}) uniform VsConstants {{ vec4 c[{}]; }} vsc;\n",
                         kBindingVsConstants, kVsConstantCount);
        g += std::format("layout(std140, set = 0, binding = {}) uniform PsConstants {{ vec4 c[{}]; }} psc;\n",
                         kBindingPsConstants, kPsConstantCount);
        g += FixedFunctionBlock();
        g += kHelpers;

        std::string decl, epilogue;
        if (!pixel_)
        {
            for (uint32_t n = 0; n < 16; ++n)
                if (vsInputLocation_[n] != 0xff)
                    g += std::format("layout(location = {}) in vec4 v{};\n", vsInputLocation_[n], n);
            for (int i = 0; i < 8; ++i)
                g += std::format("layout(location = {}) out vec4 oT{};\n", i, i);
            g += "layout(location = 8) out vec4 oD0;\nlayout(location = 9) out vec4 oD1;\n"
                 "layout(location = 10) out float oFog;\n";
            // Motion vectors (vk_abi.h): current clip position before the half-pixel offset, previous clip position.
            g += "layout(location = 11) out vec4 vCurClip;\nlayout(location = 12) out vec4 vPrevClip;\n";
            g += PrevFrameBlock();
            g += kCaptureGlsl;
            g += kTessVertexGlsl;
            // Relative constant reads (c[a0.x + n]): def'd registers first, out of range reads as zero.
            // C: current constants (VsConstants); CP: previous frame's (PrevFrame vsConstants).
            auto constFunc = [&](const char* name, const char* array) {
                g += std::format("vec4 {}(int i)\n{{\n", name);
                if (!defs_.empty())
                {
                    g += "    switch (i)\n    {\n";
                    for (auto& [n, v] : defs_)
                        g += std::format("    case {}: return {};\n", n, v);
                    g += "    default: break;\n    }\n";
                }
                g += std::format("    return i >= 0 && i < {} ? {}[i] : vec4(0.0);\n}}\n", kVsConstantCount, array);
            };
            if (needConstFunc_)
                constFunc("C", "vsc.c");
            if (needPrevConstFunc_)
                constFunc("CP", "pf.vsConstants");
            decl = "    ivec4 a0 = ivec4(0);\n"
                   "    vec4 o_Pos = vec4(0.0), o_Fog = vec4(1.0), o_Pts = vec4(1.0);\n"
                   "    vec4 o_D0 = vec4(1.0), o_D1 = vec4(0.0);\n"
                   "    vec4 o_T0 = vec4(0.0), o_T1 = vec4(0.0), o_T2 = vec4(0.0), o_T3 = vec4(0.0);\n"
                   "    vec4 o_T4 = vec4(0.0), o_T5 = vec4(0.0), o_T6 = vec4(0.0), o_T7 = vec4(0.0);\n";
            epilogue = "    D3D9_CAPTURE_WRITE(o_Pos);  // scene vertex capture (vk_abi.h)\n"
                       "    gl_Position = o_Pos;\n"
                       "    gl_Position.xy += ff.halfPixel.xy * gl_Position.w;  // D3D9 half-pixel offset (vk_abi.h)\n"
                       "    gl_PointSize = o_Pts.x;\n"
                       "    oT0 = o_T0; oT1 = o_T1; oT2 = o_T2; oT3 = o_T3; oT4 = o_T4; oT5 = o_T5; oT6 = o_T6; oT7 = o_T7;\n"
                       "    oD0 = clamp(o_D0, 0.0, 1.0);  // D3D9 clamps the color outputs\n"
                       "    oD1 = clamp(o_D1, 0.0, 1.0);\n"
                       "    oFog = clamp(o_Fog.x, 0.0, 1.0);\n"
                       "    vCurClip = o_Pos;  // motion vectors (vk_abi.h): before the half-pixel offset\n"
                       "    vPrevClip = PrevPosition();\n";
            // Tessellation outputs (vk_abi.h): camera-space position and normal.
            int posReg = -1, normalReg = -1;
            for (int n = 0; n < 16; ++n)
            {
                if (vsInputLocation_[n] == 0)
                    posReg = n;
                if (vsInputLocation_[n] == 3)
                    normalReg = n;
            }
            epilogue += "#ifdef D3D9_TESS\n"
                        "    mat4 tessInv = inverse(TessCamera());\n"
                        "    vec4 tessP = tessInv * o_Pos;\n"
                        "    tess_World = tessP.xyz / tessP.w;\n"
                        "    tess_Normal = vec3(0.0);\n";
            if (posReg >= 0 && normalReg >= 0)
                epilogue += std::format(
                    "    vec3 tessN = TessDecodeNormal(v{1});\n"
                    "    if (dot(tessN, tessN) > 1e-12)\n"
                    "    {{\n"
                    "        float tessS = max(length(v{0}.xyz), 1.0) * 0.5;  // any size: positions are affine\n"
                    "        vec4 tessQ = tessInv * OffsetPosition(vec4(normalize(tessN) * tessS, 0.0));\n"
                    "        vec3 tessD = tessQ.xyz / tessQ.w - tess_World;\n"
                    "        if (dot(tessD, tessD) > 0.0)\n"
                    "            tess_Normal = normalize(tessD);\n"
                    "    }}\n",
                    posReg, normalReg);
            epilogue += "#endif\n";
        }
        else
        {
            for (uint32_t n = 0; n < 8; ++n)
                if (texCoordsUsed_ & (1u << n))
                    g += std::format("layout(location = {}) in vec4 vT{};\n", n, n);
            for (uint32_t n = 0; n < 2; ++n)
                if (colorsUsed_ & (1u << n))
                    g += std::format("layout(location = {}) in vec4 vColor{};\n", 8 + n, n);
            // Fog factor for D3D9_FINAL (FinalPixelDefine): always declared.
            g += "layout(location = 10) in float vFog;\n";
            g += "layout(location = 0) out vec4 fragColor;\n";
            // Motion vectors (vk_abi.h): clip positions from the vertex half, motion = prevUv - curUv.
            g += "layout(location = 11) in vec4 vCurClip;\nlayout(location = 12) in vec4 vPrevClip;\n"
                 "layout(location = 1) out vec4 motion;\n";
            for (uint32_t n = 0; n < kMaxSamplers; ++n)
                if (samplers_ & (1u << n))
                    g += std::format("layout(set = 0, binding = {}) uniform {} s{};\n", kBindingSampler0 + n,
                                     (samplerCube_ >> n) & 1 ? "samplerCube" : "sampler2D", n);
            g += "#ifndef D3D9_FINAL\n#define D3D9_FINAL(c)\n#endif\n";
            for (uint32_t n = 0; n < 2; ++n)
                if (colorsUsed_ & (1u << n))  // color iterators hold [0, 1]
                    decl += std::format("    vec4 v{} = clamp(vColor{}, 0.0, 1.0);\n", n, n);
            if (ps1x_)
                for (uint32_t n = 0; n < 8; ++n)
                    if (texRegsUsed_ & (1u << n))
                        decl += std::format("    vec4 t{} = vec4(0.0);\n", n);
            if (needMatrixVec_)
                decl += "    vec3 tm = vec3(0.0);  // texm3x2 / texm3x3 row results\n";
            if (writesDepth_)
                decl += "    gl_FragDepth = gl_FragCoord.z;\n";
            if (!ps1x_)
                decl += "    vec4 oC0 = vec4(0.0);\n";
            // ps_1_x: the result is r0, converted to a [0, 1] color; ps_2_0: oC0.
            epilogue = "    vec2 mv_curNdc = vCurClip.xy / vCurClip.w, mv_prevNdc = vPrevClip.xy / vPrevClip.w;\n"
                       "    vec2 mv = vec2(mv_prevNdc.x * 0.5 + 0.5, 0.5 - mv_prevNdc.y * 0.5) -\n"
                       "              vec2(mv_curNdc.x * 0.5 + 0.5, 0.5 - mv_curNdc.y * 0.5);\n";
            epilogue += ps1x_ ? "    vec4 result = clamp(r0, 0.0, 1.0);\n" : "    vec4 result = oC0;\n";
            // Material outputs for ambient occlusion (vk_abi.h "Motion vectors", z and w), before fog.
            epilogue += MaterialOutputs(hash_);
            epilogue += "    motion = vec4(mv, mat_aoWeight, mat_albedo);\n";
            epilogue += "    D3D9_FINAL(result);\n    fragColor = result;\n";
            if (ps1x_)
                Temp(0);
        }
        if (anyTemp_)
            for (uint32_t n = 0; n <= maxTemp_; ++n)
                decl += std::format("    vec4 r{} = vec4(0.0);\n", n);

        auto body = [&](const std::vector<Emitted>& code) {
            for (size_t i = 0; i < code.size(); ++i)
            {
                if (i + 1 < code.size() && code[i + 1].coissue)
                {
                    // Co-issued pair: both read their sources before either writes.
                    g += code[i].compute + code[i + 1].compute + code[i].write + code[i + 1].write;
                    ++i;
                }
                else
                    g += code[i].compute + code[i].write;
            }
        };
        if (!pixel_)
        {
            // The program evaluated with the previous frame's constants (pass 3 in Run): its own registers, a0 and
            // outputs; only the position is returned (the other outputs are dead code for the compiler).
            g += "vec4 PrevPosition()\n{\n" + decl;
            body(prevOut_);
            g += "    return o_Pos;\n}\n";
            // Pass 4: the position input moved by posOffset (tessellation normals).
            g += "#ifdef D3D9_TESS\nvec4 OffsetPosition(vec4 posOffset)\n{\n" + decl;
            body(offsetOut_);
            g += "    return o_Pos;\n}\n#endif\n";
        }
        g += "void main()\n{\n" + decl;
        body(out_);
        g += epilogue + "}\n";
        result_.samplerMask = samplers_;
        result_.samplerCubeMask = samplerCube_ & samplers_;
        return g;
    }

    bool Error(std::string msg)
    {
        if (error_.empty())
            error_ = std::move(msg);
        return false;
    }

    TranslatedShader Failed(std::string msg)
    {
        TranslatedShader t;
        t.isPixel = pixel_;
        t.error = std::move(msg);
        return t;
    }

    const uint32_t* code_;
    uint64_t hash_ = 1469598103934665603ull;
    bool pixel_ = false, ps1x_ = false;
    uint32_t major_ = 0, minor_ = 0;
    const char* psRange_ = "1.0";
    std::vector<Instruction> instrs_;
    std::vector<Emitted> out_;
    std::map<uint32_t, std::string> defs_;  // def'd constant register -> literal
    uint32_t vsInputLocation_[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t texCoordsUsed_ = 0, colorsUsed_ = 0, texRegsUsed_ = 0;
    uint32_t samplers_ = 0, samplerCube_ = 0;
    uint32_t maxTemp_ = 0;
    bool anyTemp_ = false;
    bool needConstFunc_ = false, needMatrixVec_ = false, writesDepth_ = false;
    bool prev_ = false;                // generating the previous-frame evaluation (constants from PrevFrame)
    bool needPrevConstFunc_ = false;   // CP(): relative constant reads of that evaluation
    std::vector<Emitted> prevOut_;     // that evaluation's code (vertex shaders)
    bool offset_ = false;              // generating the moved-position evaluation (tessellation normals)
    std::vector<Emitted> offsetOut_;   // that evaluation's code
    int padIndex_ = 0;
    int tmpCounter_ = 0;
    std::string error_;
    TranslatedShader result_;
};
}  // namespace

TranslatedShader TranslateShader(const uint32_t* code) { return Translator(code).Run(); }
std::string VertexUniformBlocks()
{
    return std::format("layout(std140, set = 0, binding = {}) uniform VsConstants {{ vec4 c[{}]; }} vsc;\n",
                       kBindingVsConstants, kVsConstantCount) +
           FixedFunctionBlock() + PrevFrameBlock();
}
}  // namespace rhi::vulkan
