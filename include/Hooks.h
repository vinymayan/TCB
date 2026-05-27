#pragma once
#include "Events.h"

namespace RE
{
    namespace Offset
    {
        typedef void(_fastcall* _destroyProjectile)(RE::Projectile* a_projectile);
        inline static REL::Relocation<_destroyProjectile> destroyProjectile{ RELOCATION_ID(42930, 44110) };

    }
}

class Hook_OnMeleeHit
{
public:
    static void install()
    {
        auto& trampoline = SKSE::GetTrampoline();

        constexpr size_t size_per_hook = 14;
        constexpr size_t NUM_TRAMPOLINE_HOOKS = 3;
        SKSE::AllocTrampoline(size_per_hook * NUM_TRAMPOLINE_HOOKS);
        REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37673, 38627) };  //140628C20       14064E760
        _ProcessHit = trampoline.write_call<5>(hook.address() + REL::Relocate(0x3C0, 0x4A8), processHit);
        logger::info("hook:OnMeleeHit");
    }

private:
    static void processHit(RE::Actor* victim, RE::HitData& hitData);
    static inline REL::Relocation<decltype(processHit)> _ProcessHit;  //140626400       14064BAB0

};

class Hook_OnProjectileCollision
{
public:
    static void install()
    {
        REL::Relocation<std::uintptr_t> arrowProjectileVtbl{ RE::VTABLE_ArrowProjectile[0] };
        REL::Relocation<std::uintptr_t> missileProjectileVtbl{ RE::VTABLE_MissileProjectile[0] };

        _arrowCollission = arrowProjectileVtbl.write_vfunc(190, OnArrowCollision);
        _missileCollission = missileProjectileVtbl.write_vfunc(190, OnMissileCollision);
        logger::info("hook:OnProjectileCollision");
    };

private:
    static void OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);

    static void OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);
    static inline REL::Relocation<decltype(OnArrowCollision)> _arrowCollission;
    static inline REL::Relocation<decltype(OnMissileCollision)> _missileCollission;
};
