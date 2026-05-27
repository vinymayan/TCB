#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Settings.h"

const std::string dawn = "Dawnguard.esm";
namespace Hook_Precision { void Initialize(); }

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        anim = GetIdleByFormID(0x0E6A8, dawn);
        Hook_Precision::Initialize();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
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
                }
            }
        }
        Sink::NpcCombatTracker::RegisterSink(player);
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
