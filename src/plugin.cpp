#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Settings.h"

const std::string dawn = "Dawnguard.esm";

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        anim = GetIdleByFormID(0x0E6A8, dawn);
       
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(Sink::HitEventHandler::GetSingleton());
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Sink::NpcCombatTracker::GetSingleton());
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->AddAnimationGraphEventSink(Sink::NpcCycleSink::GetSingleton());
        }
		Sink::ProcessHitHook::Install();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    
    return true;
}
