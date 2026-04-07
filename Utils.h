#pragma once
#include "structs.h"
#include "memory.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>

// Source 2 row-major W2S.  matrix[row][col].
// clip_w < 0.001f = behind camera or at focal point.
inline bool WorldToScreen(const Vector3& world, Vector2& screen, const view_matrix_t& vm, int width, int height) {
    const float clip_x = vm.matrix[0][0] * world.x + vm.matrix[0][1] * world.y + vm.matrix[0][2] * world.z + vm.matrix[0][3];
    const float clip_y = vm.matrix[1][0] * world.x + vm.matrix[1][1] * world.y + vm.matrix[1][2] * world.z + vm.matrix[1][3];
    const float clip_z = vm.matrix[2][0] * world.x + vm.matrix[2][1] * world.y + vm.matrix[2][2] * world.z + vm.matrix[2][3];
    const float clip_w = vm.matrix[3][0] * world.x + vm.matrix[3][1] * world.y + vm.matrix[3][2] * world.z + vm.matrix[3][3];

    if (clip_w < 0.001f) return false;

    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    screen.x = (width / 2.0f) + ndc_x * (width / 2.0f);
    screen.y = (height / 2.0f) - ndc_y * (height / 2.0f);
    return true;
}

inline float NormalizeYaw(float yaw) {
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

inline float ClampPitch(float pitch) {
    return std::clamp(pitch, -89.0f, 89.0f);
}

// ── Bone reading helpers ──────────────────────────────────────────────────────

struct BoneMatrix3x4 { float m[3][4]; };

// Position-only layout used by some CS2 builds (0x20 stride)
struct BonePos { float x, y, z, w; };

inline Vector3 MatrixTranslation(const BoneMatrix3x4& m) {
    return { m.m[0][3], m.m[1][3], m.m[2][3] };
}

inline bool IsFiniteVector3(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Validates that a 3×4 matrix has plausible rotation column magnitudes.
// Each basis vector should have length roughly 1.0 (0.15–3.5 to allow small
// scale factors without rejecting valid bones).
inline bool IsValidMatrix3x4(const BoneMatrix3x4& m) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(m.m[r][c])) return false;

    auto col_len = [&](int c) {
        return std::sqrt(m.m[0][c] * m.m[0][c] + m.m[1][c] * m.m[1][c] + m.m[2][c] * m.m[2][c]);
        };
    const float lx = col_len(0), ly = col_len(1), lz = col_len(2);
    return lx > 0.15f && ly > 0.15f && lz > 0.15f &&
        lx < 3.5f && ly < 3.5f && lz < 3.5f;
}

// Rejects garbage pointer reads by checking that a bone is plausibly close to
// the pawn's known world position:
//   • Horizontal (XY) displacement ≤ 95 units  (player capsule radius ≈ 16)
//   • Vertical (Z)   displacement in [-40, +115] (feet below origin to head)
inline bool IsReasonableBoneWorldPoint(
    const Vector3& pt, const Vector3& pawn_origin, bool has_origin)
{
    if (!IsFiniteVector3(pt)) return false;
    if (std::fabs(pt.x) > 100000.f || std::fabs(pt.y) > 100000.f ||
        std::fabs(pt.z) > 100000.f) return false;
    if (!has_origin) return true;
    const float dx = pt.x - pawn_origin.x;
    const float dy = pt.y - pawn_origin.y;
    const float dz = pt.z - pawn_origin.z;
    return (dx * dx + dy * dy) <= 95.f * 95.f && dz >= -40.f && dz <= 115.f;
}

// ── ReadBone ─────────────────────────────────────────────────────────────────
// CS2 bone access: pawn → m_pGameSceneNode → bone-array pointer → matrix/vec
//
// The bone-array pointer sits at one of several offsets inside the node
// depending on CS2 patch version.  Known candidates (struct layout shifts):
//
//   node + 0x1F0   — pre-March 2026, m_modelState @ 0x170, boneArray @ 0x80
//   node + 0x1E0   — March 2026+,    m_modelState @ 0x160, boneArray @ 0x80
//   node + 0x100   — some interim patches (0x80+0x80)
//
// Each bone can be laid out as:
//   0x30-stride BoneMatrix3x4 — preferred (full rotation + translation)
//   0x20-stride BonePos (xyz + pad) — older/simpler layout
//
// Both layouts are tried for every candidate pointer; the first that passes
// IsValidMatrix3x4 / IsReasonableBoneWorldPoint is returned.
// ─────────────────────────────────────────────────────────────────────────────
inline bool ReadBone(Memory& mem, uintptr_t pawn, int idx, Vector3& out) {
    if (idx < 0 || idx >= 256) return false;

    const uintptr_t node = mem.Read<uintptr_t>(pawn + schemas::m_pGameSceneNode);
    if (!node) return false;

    const Vector3 pawn_origin = mem.Read<Vector3>(pawn + schemas::m_vOldOrigin);
    const bool has_origin = IsFiniteVector3(pawn_origin);

    // All known-good candidate pointer offsets inside the node
    const uintptr_t candidates[] = {
        mem.Read<uintptr_t>(node + 0x1F0),   // pre-March 2026
        mem.Read<uintptr_t>(node + 0x1E0),   // March 2026+
        mem.Read<uintptr_t>(node + 0x100),   // interim patches
    };

    for (const uintptr_t base : candidates) {
        if (!base) continue;

        // ── Try 0x30-stride full 3×4 matrix (preferred) ──────────────────────
        const BoneMatrix3x4 mat = mem.Read<BoneMatrix3x4>(base + (uintptr_t)idx * 0x30);
        if (IsValidMatrix3x4(mat)) {
            const Vector3 pt = MatrixTranslation(mat);
            if (IsReasonableBoneWorldPoint(pt, pawn_origin, has_origin)) {
                out = pt;
                return true;
            }
        }

        // ── Try 0x20-stride position-only vector ──────────────────────────────
        const BonePos bp = mem.Read<BonePos>(base + (uintptr_t)idx * 0x20);
        if (std::isfinite(bp.x) && std::isfinite(bp.y) && std::isfinite(bp.z) &&
            !(bp.x == 0.f && bp.y == 0.f && bp.z == 0.f)) {
            const Vector3 pt{ bp.x, bp.y, bp.z };
            if (IsReasonableBoneWorldPoint(pt, pawn_origin, has_origin)) {
                out = pt;
                return true;
            }
        }

        // ── Legacy: 0x30-stride but read as position-only ─────────────────────
        const BonePos bp30 = mem.Read<BonePos>(base + (uintptr_t)idx * 0x30);
        if (std::isfinite(bp30.x) && std::isfinite(bp30.y) && std::isfinite(bp30.z) &&
            !(bp30.x == 0.f && bp30.y == 0.f && bp30.z == 0.f)) {
            const Vector3 pt{ bp30.x, bp30.y, bp30.z };
            if (IsReasonableBoneWorldPoint(pt, pawn_origin, has_origin)) {
                out = pt;
                return true;
            }
        }
    }
    return false;
}