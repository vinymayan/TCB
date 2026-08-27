#include "logger.h"
#include "AttackTracking.h"
#include "Events.h"
#include "Hooks.h"
#include "Settings.h"
#include "TrueDirectionalMovementAPI.h"

const std::string dawn = "Dawnguard.esm";
namespace Hook_Precision { void Initialize(); }
TDM_API::IVTDM3* tdmAPI = nullptr;

void PayloadInterpreterMessageListener(SKSE::MessagingInterface::Message* message) {
    if (!message || !message->sender || std::string_view(message->sender) != "PayloadInterpreter") {
        return;
    }

    AttackTracking::RegisterPayloadHandlers(static_cast<payloadinterpreter::API::Message*>(message->data));
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        if (SKSE::GetMessagingInterface()->RegisterListener("PayloadInterpreter", PayloadInterpreterMessageListener)) {
            logger::info("Listener do Payload Interpreter registrado com sucesso.");
        }
        else {
            logger::warn("Payload Interpreter nao encontrado; payloads TrackTCB/MagnetismTCB ficarao indisponiveis.");
        }

        tdmAPI = static_cast<TDM_API::IVTDM3*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V3));
        if (tdmAPI) {
            logger::info("True Directional Movement API (V3) carregada com sucesso!");
        }
        else {
            logger::warn("True Directional Movement nao foi encontrado ou falhou ao carregar.");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        anim = GetIdleByFormID(0x0E6A8, dawn);
        Hook_Precision::Initialize();
		Hook_OnMeleeHit::install();
		Hook_OnProjectileCollision::install();
		Hook_AttackTracking::install();
    }
    if (message->type == SKSE::MessagingInterface::kPreLoadGame) {
        AttackTracking::Manager::GetSingleton()->Clear();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        AttackTracking::Manager::GetSingleton()->Clear();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(Sink::HitEventHandler::GetSingleton());
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Sink::NpcCombatTracker::GetSingleton());
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->AddAnimationGraphEventSink(Sink::NpcCycleSink::GetSingleton());
            player->SetGraphVariableBool("Paired_AnimationCMF", false);
            player->SetGraphVariableBool("Stagger_AnimationCMF", false);
            player->SetGraphVariableBool("Parry_AnimationCMF", false);
            player->SetGraphVariableFloat("StaggerAmount_AnimationCMF", 0.0f);
            player->SetGraphVariableFloat("StaggerDirection_AnimationCMF", 0.0f);
            player->SetGraphVariableInt("nStagger_AnimationCMF", 0);
            player->SetGraphVariableBool("isUnblockableHit", false);
            player->SetGraphVariableBool("isParryingCMF", false);
            player->SetGraphVariableBool("isUndodgeableHit", false);
            player->SetGraphVariableBool("PairedAllCMF", false);
        }
        if (auto processLists = RE::ProcessLists::GetSingleton()) {
            for (auto& actorHandle : processLists->highActorHandles) {
                if (auto actor = actorHandle.get().get()) {
                    actor->SetGraphVariableBool("Paired_AnimationCMF", false);
                    actor->SetGraphVariableBool("Stagger_AnimationCMF", false);
                    actor->SetGraphVariableBool("Parry_AnimationCMF", false);
                    actor->SetGraphVariableFloat("StaggerAmount_AnimationCMF", 0.0f);
                    actor->SetGraphVariableFloat("StaggerDirection_AnimationCMF", 0.0f);
                    actor->SetGraphVariableInt("nStagger_AnimationCMF", 0);
                    actor->SetGraphVariableBool("isUnblockableHit", false);
                    actor->SetGraphVariableBool("isParryingCMF", false);
                    actor->SetGraphVariableBool("isUndodgeableHit", false);
                    actor->SetGraphVariableBool("PairedAllCMF", false);
                }
            }
        }
        Sink::NpcCombatTracker::RegisterSinksForExistingCombatants();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    
    return true;
}
