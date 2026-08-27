#include "Hooks.h"
#include "AttackTracking.h"
#include "Settings.h"
#include "PrecisionAPI.h"

void Hook_OnMeleeHit::processHit(RE::Actor* victim, RE::HitData& hitData)
{
	if (!victim) {
		_ProcessHit(victim, hitData);
		return;
	}

	bool hasIframe = false;
	victim->GetGraphVariableBool("hasIframeCMF", hasIframe);

	if (hasIframe) {
		hitData.totalDamage = 0.0f;
		hitData.physicalDamage = 0.0f;
		hitData.stagger = 0;
		return;
	}

	_ProcessHit(victim, hitData);
}

namespace Hook_Precision
{
	static PRECISION_API::PreHitCallbackReturn PreHitCallback(const PRECISION_API::PrecisionHitData& a_hitData)
	{
		PRECISION_API::PreHitCallbackReturn ret;

		if (!a_hitData.target || a_hitData.target->formType != RE::FormType::ActorCharacter) {
			return ret;
		}

		auto victim = const_cast<RE::TESObjectREFR*>(a_hitData.target)->As<RE::Actor>();
		if (!victim) {
			return ret;
		}

		bool hasIframe = false;
		victim->GetGraphVariableBool("hasIframeCMF", hasIframe);

		if (hasIframe) {
			ret.bIgnoreHit = true;
			return ret;
		}
		else {
			return ret;
		}
		
	}

	void Initialize()
	{
		auto precisionAPI = static_cast<PRECISION_API::IVPrecision4*>(PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4));
		if (precisionAPI) {
			// Unificação das chamadas da API sob o mesmo escopo lógico
			precisionAPI->AddPreHitCallback(SKSE::GetPluginHandle(), PreHitCallback);
			SKSE::log::info("[Precision API Unificada]: Registros de PreHit concluídos com sucesso!");
		}
		else {
			SKSE::log::warn("[Precision API]: Plugin não encontrado. Rodando puramente por hooks nativos do jogo.");
		}
	}
}

bool processProjectileBlock(RE::Actor* a_blocker, RE::Projectile* a_projectile, RE::hkpCollidable* a_projectile_collidable)
{
	bool hasIframe = false;
	a_blocker->GetGraphVariableBool("hasIframeCMF", hasIframe);
	if(!hasIframe) {
		return false;
	}
	auto shooterHandle = a_projectile->GetProjectileRuntimeData().shooter;
	auto shooter = shooterHandle.get().get() ? shooterHandle.get().get()->As<RE::Actor>() : nullptr;
	bool isUndodgeable = false;
	bool isArrow = !a_projectile->GetProjectileRuntimeData().spell;
	RE::Offset::destroyProjectile(a_projectile);
	return true;
}

inline bool shouldIgnoreHit(RE::Projectile* a_projectile, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{
	if (a_AllCdPointCollector) {
		for (auto& hit : a_AllCdPointCollector->hits) {
			auto refrA = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA);
			auto refrB = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB);
			RE::Actor* target = nullptr;
			RE::hkpCollidable* projectileCollidable = nullptr;

			if (refrA && refrA->formType == RE::FormType::ActorCharacter) {
				target = refrA->As<RE::Actor>();
				projectileCollidable = const_cast<RE::hkpCollidable*>(hit.rootCollidableB);
			}
			else if (refrB && refrB->formType == RE::FormType::ActorCharacter) {
				target = refrB->As<RE::Actor>();
				projectileCollidable = const_cast<RE::hkpCollidable*>(hit.rootCollidableA);
			}

			if (target) {
				if (processProjectileBlock(target, a_projectile, projectileCollidable)) {
					return true;
				}
			}
			else {
			}
		}
	}
	return false;
}

void Hook_OnProjectileCollision::OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{
	if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
		return;
	}
	_arrowCollission(a_this, a_AllCdPointCollector);
}

void Hook_OnProjectileCollision::OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{

	if (a_this && (a_this->GetProjectileRuntimeData().spell || a_this->GetProjectileBase())) {
		if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
			return;
		}
	}

	_missileCollission(a_this, a_AllCdPointCollector);
}

void Hook_AttackTracking::install()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[0] };
    _UpdateCharacter = characterVtable.write_vfunc(0xAD, UpdateCharacter);

    REL::Relocation<std::uintptr_t> playerCharacterVtable{ RE::VTABLE_PlayerCharacter[0] };
    _UpdatePlayerCharacter = playerCharacterVtable.write_vfunc(0xAD, UpdatePlayerCharacter);

    // ActorState is a secondary base of Character. This is the same vtable slot
    // used by TDM to adjust the movement rotation returned by the engine.
    REL::Relocation<std::uintptr_t> characterActorStateVtable{ RE::VTABLE_Character[6] };
    _GetMovementRotation = characterActorStateVtable.write_vfunc(0x4, GetMovementRotation);

    REL::Relocation<std::uintptr_t> playerActorStateVtable{ RE::VTABLE_PlayerCharacter[6] };
    _GetPlayerMovementRotation = playerActorStateVtable.write_vfunc(0x4, GetPlayerMovementRotation);

    SKSE::log::info("[AttackTracking] Hooks de Character e PlayerCharacter instalados.");
}

void Hook_AttackTracking::UpdateCharacter(RE::Actor* a_actor, float a_deltaTime)
{
    _UpdateCharacter(a_actor, a_deltaTime);
    AttackTracking::Manager::GetSingleton()->Update(a_actor, a_deltaTime);
}

void Hook_AttackTracking::UpdatePlayerCharacter(RE::Actor* a_actor, float a_deltaTime)
{
    _UpdatePlayerCharacter(a_actor, a_deltaTime);
    AttackTracking::Manager::GetSingleton()->Update(a_actor, a_deltaTime);
}

void Hook_AttackTracking::GetMovementRotation(RE::ActorState* a_actorState, RE::NiPoint3& a_rotation)
{
    _GetMovementRotation(a_actorState, a_rotation);
    AttackTracking::Manager::GetSingleton()->ApplyRootMotionYawCorrection(a_actorState, a_rotation);
}

void Hook_AttackTracking::GetPlayerMovementRotation(RE::ActorState* a_actorState, RE::NiPoint3& a_rotation)
{
    _GetPlayerMovementRotation(a_actorState, a_rotation);
    AttackTracking::Manager::GetSingleton()->ApplyRootMotionYawCorrection(a_actorState, a_rotation);
}
