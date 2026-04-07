#pragma once
#include <cstddef>
#include <cstdint>

// ── Wicked.Services · Offsets.h ─────────────────────────────────────────────
// Last updated: April 3, 2026 (CS2 Build)
// Final Fix for: Grenade Spawn Time and Molotov Type Identification
// ─────────────────────────────────────────────────────────────────────────────

namespace offsets {
    // ── client.dll ──────────────────────────────────────────────────────────
    inline std::ptrdiff_t dwCSGOInput = 0x231E330;
    inline std::ptrdiff_t dwEntityList = 0x24B3268;
    inline std::ptrdiff_t dwGameEntitySystem = 0x24B3268;
    inline std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x20A0;
    inline std::ptrdiff_t dwGameRules = 0x2311ED0;
    inline std::ptrdiff_t dwGlobalVars = 0x2062540;
    inline std::ptrdiff_t dwGlowManager = 0x230ECD8;
    inline std::ptrdiff_t dwLocalPlayerController = 0x22F8028;
    inline std::ptrdiff_t dwLocalPlayerPawn = 0x206D9E0;
    inline std::ptrdiff_t dwPlantedC4 = 0x231BAB0;
    inline std::ptrdiff_t dwPrediction = 0x206D8F0;
    inline std::ptrdiff_t dwSensitivity = 0x230F7E8;
    inline std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
    inline std::ptrdiff_t dwViewAngles = 0x231E9B8;
    inline std::ptrdiff_t dwViewMatrix = 0x2313F10;
    inline std::ptrdiff_t dwViewRender = 0x2314328;
    inline std::ptrdiff_t dwWeaponC4 = 0x229D2B0;

    // ── Layout Constants ────────────────────────────────────────────────────
    inline std::ptrdiff_t entity_list_entry = 0x10;
    inline std::ptrdiff_t entity_list_controller_stride = 0x78;
    inline std::ptrdiff_t dwGameSceneNode = 0x1F0;
}

namespace schemas {
    // ── C_BaseEntity ────────────────────────────────────────────────────────
    inline std::ptrdiff_t m_iHealth = 0x354;
    inline std::ptrdiff_t m_iTeamNum = 0x3F3;
    inline std::ptrdiff_t m_fFlags = 0x400;
    inline std::ptrdiff_t m_vecVelocity = 0x438;
    inline std::ptrdiff_t m_pGameSceneNode = 0x338;
    inline std::ptrdiff_t m_vOldOrigin = 0x1588;

    // ── C_BaseModelEntity ───────────────────────────────────────────────────
    inline std::ptrdiff_t m_vecViewOffset = 0xD58;

    // ── CCSPlayerController ─────────────────────────────────────────────────
    inline std::ptrdiff_t m_hPlayerPawn = 0x80C;
    inline std::ptrdiff_t m_iszPlayerName = 0x6F8;
    inline std::ptrdiff_t m_sSanitizedPlayerName = 0x860;
    inline std::ptrdiff_t m_iPing = 0x828;
    inline std::ptrdiff_t m_pInGameMoneyServices = 0x808;
    inline std::ptrdiff_t m_bPawnHasDefuser = 0x920;
    inline std::ptrdiff_t m_bPawnHasHelmet = 0x921;

    // ── C_CSPlayerPawn ──────────────────────────────────────────────────────
    inline std::ptrdiff_t m_iIDEntIndex = 0x3EAC;
    inline std::ptrdiff_t m_entitySpottedState = 0x26E0;
    inline std::ptrdiff_t m_bIsScoped = 0x26F8;
    inline std::ptrdiff_t m_bIsDefusing = 0x26FA;
    inline std::ptrdiff_t m_ArmorValue = 0x272C;
    inline std::ptrdiff_t m_aimPunchAngle = 0x16CC;
    inline std::ptrdiff_t m_iShotsFired = 0x270C;

    // ── EntitySpottedState_t ────────────────────────────────────────────────
    inline std::ptrdiff_t m_bSpotted = 0x8;
    inline std::ptrdiff_t m_bSpottedByMask = 0xC;

    // ── C_CSPlayerPawnBase ──────────────────────────────────────────────────
    inline std::ptrdiff_t m_pItemServices = 0x13E0;
    inline std::ptrdiff_t m_pObserverServices = 0x13F0;
    inline std::ptrdiff_t m_pClippingWeapon = 0x3DC0;
    inline std::ptrdiff_t m_flFlashBangTime = 0x15E4;
    inline std::ptrdiff_t m_flFlashDuration = 0x15F8;
    inline std::ptrdiff_t m_flFlashOverlayAlpha = 0x15EC;
    inline std::ptrdiff_t m_flFlashMaxAlpha = 0x15F4;

    // ── CPlayer_ObserverServices ────────────────────────────────────────────
    inline std::ptrdiff_t m_iObserverMode = 0x40;
    inline std::ptrdiff_t m_hObserverTarget = 0x44;

    // ── Weapon / Items ──────────────────────────────────────────────────────
    inline std::ptrdiff_t m_AttributeManager = 0x1378;
    inline std::ptrdiff_t m_Item = 0x50;
    inline std::ptrdiff_t m_iItemDefinitionIndex = 0x1BA;
    inline std::ptrdiff_t m_iClip1 = 0x18D0;
    inline std::ptrdiff_t m_pReserveAmmo = 0x18D8;

    // ── C_PlantedC4 ─────────────────────────────────────────────────────────
    inline std::ptrdiff_t m_bBombTicking = 0x1170;
    inline std::ptrdiff_t m_nBombSite = 0x1174;
    inline std::ptrdiff_t m_flC4Blow = 0x11A0;
    inline std::ptrdiff_t m_bBeingDefused = 0x11AC;
    inline std::ptrdiff_t m_bHasExploded = 0x11A5;
    inline std::ptrdiff_t m_bBombDefused = 0x11C4;
    inline std::ptrdiff_t m_flDefuseCountDown = 0x11C0;

    // ── Grenade Specifics (Fixes Misc.cpp Errors) ───────────────────────────
    inline std::ptrdiff_t m_flSpawnTime = 0x13D8;      // From C_BaseCSGrenadeProjectile
    inline std::ptrdiff_t m_bIsIncGrenade = 0x1438;   // From C_MolotovProjectile
    inline std::ptrdiff_t m_bDidSmokeEffect = 0x1454;
    inline std::ptrdiff_t m_vSmokeDetonationPos = 0x1468;
}