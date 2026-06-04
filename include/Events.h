#pragma once
#include <shared_mutex>
#include <chrono> 
#include <unordered_map> 
#include <unordered_set>
#include "TrueDirectionalMovementAPI.h"

extern const std::string dawn;
extern const std::string sky;
extern RE::TESIdleForm* anim;
extern TDM_API::IVTDM3* tdmAPI;
RE::TESIdleForm* GetIdleByFormID(RE::FormID a_formID, const std::string& a_pluginName);

namespace Sink {


    void ApplySlowTime(float a_multiplier);
    void ResetTimeTask();

    class HitEventHandler : public RE::BSTEventSink<RE::TESHitEvent> {
    public:
        static HitEventHandler* GetSingleton() {
            static HitEventHandler singleton;
            return &singleton;
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event,
                                              RE::BSTEventSource<RE::TESHitEvent>* a_source) override;
    };

    class NpcCycleSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static NpcCycleSink* GetSingleton() {
            static NpcCycleSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
    };

    class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
    public:
        static NpcCombatTracker* GetSingleton() {
            static NpcCombatTracker singleton;
            return &singleton;
        }

        // Função chamada quando um evento de combate ocorre
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
            RE::BSTEventSource<RE::TESCombatEvent>*) override;

        static void RegisterSink(RE::Actor* a_actor);
        static void UnregisterSink(RE::Actor* a_actor);

        static void RegisterSinksForExistingCombatants();

    private:
        // Instância compartilhada do nosso processador de lógica
        inline static NpcCycleSink g_npcSink;

        // Guarda os FormIDs dos NPCs que já estamos ouvindo
        inline static std::set<RE::FormID> g_trackedNPCs;
        inline static std::shared_mutex g_mutex;
    };

    class PC3DLoadEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static PC3DLoadEventHandler* GetSingleton() {
            static PC3DLoadEventHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override;
    };
}

