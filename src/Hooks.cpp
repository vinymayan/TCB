#include "Hooks.h"
#include "Settings.h"
#include "PrecisionAPI.h"

void Hook_OnMeleeHit::processHit(RE::Actor* victim, RE::HitData& hitData)
{
	if (!victim) {
		_ProcessHit(victim, hitData);
		return;
	}

	auto aggressor = hitData.aggressor.get().get();

	if (!aggressor) {
		_ProcessHit(victim, hitData);
		return;
	}
	bool hasIframe = false;
	victim->GetGraphVariableBool("hasIframeCMF", hasIframe);

	if (hasIframe) {
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
		auto aggressor = a_hitData.attacker;

		if (!victim || !aggressor || !victim->IsBlocking()) {
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

