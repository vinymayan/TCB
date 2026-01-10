#include "Events.h"

RE::TESIdleForm* anim = nullptr;

float CalculateStaggerDirection(RE::Actor* attacker, RE::Actor* target) {
    if (!attacker || !target) {
        return 0.0f;
    }

    // 1. Pegamos as posições X e Y
    const auto attackerPos = attacker->GetPosition();
    const auto targetPos = target->GetPosition();

    // 2. Calculamos o vetor (delta) do Alvo -> Atacante
    float dx = attackerPos.x - targetPos.x;
    float dy = attackerPos.y - targetPos.y;

    // 3. Calculamos o ângulo absoluto no mundo (em radianos, de -PI a +PI)
    float angleToAttacker = std::atan2(dy, dx);

    // 4. Pegamos o ângulo que o Alvo está olhando (Z rotation)
    float targetAngle = target->data.angle.z;

    // 5. Subtraímos para achar o ângulo RELATIVO (onde o atacante está na visão do alvo)
    float relativeAngle = angleToAttacker - targetAngle;

    // 6. Normalização trigonométrica
    // O resultado pode estar fora de 0..2PI (ex: negativo), então corrigimos.
    const float PI = 3.14159265358979323846f;
    const float TWO_PI = 2.0f * PI;

    // Garante que o ângulo esteja entre 0 e 2PI
    while (relativeAngle < 0) {
        relativeAngle += TWO_PI;
    }
    while (relativeAngle >= TWO_PI) {
        relativeAngle -= TWO_PI;
    }

    // 7. Converte radianos (0..6.28) para a escala do Behavior Graph (0.0..1.0)
    return relativeAngle / TWO_PI;
}

RE::TESIdleForm* GetIdleByFormID(RE::FormID a_formID, const std::string& a_pluginName) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    RE::TESForm* lookupForm = dataHandler ? dataHandler->LookupForm(a_formID, a_pluginName) : nullptr;
    if (!lookupForm) {
        SKSE::log::warn("Não foi possível encontrar o FormID 0x{:X} no plugin {}", a_formID, a_pluginName);
        return nullptr;
    }
    // Verificamos se o tipo do formulário é IdleForm e fazemos o cast.
    if (lookupForm->GetFormType() == RE::FormType::Idle) {
        return static_cast<RE::TESIdleForm*>(lookupForm);
    }
    SKSE::log::warn("O FormID 0x{:X} não é um TESIdleForm.", a_formID);
    return nullptr;
}


void PlayIdleAnimationTarget(RE::Actor* a_actor, RE::TESIdleForm* a_idle, RE::Actor* a_target) {
    if (a_actor && a_idle) {
        if (auto* processManager = a_actor->GetActorRuntimeData().currentProcess) {
            processManager->PlayIdle(a_actor, a_idle, a_target);

            //SKSE::log::info("Tocando animação idle FormID 0x{:X}", a_idle->GetFormID());
        } else {
            SKSE::log::error("Não foi possível obter o AIProcess (currentProcess) do ator.");
        }
    }
}


RE::BSEventNotifyControl Sink::HitEventHandler::ProcessEvent(const RE::TESHitEvent* a_event,
                                                             RE::BSTEventSource<RE::TESHitEvent>* a_source) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!a_event || !a_event->cause || !a_event->target) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* target = a_event->target.get()->As<RE::Actor>();
    auto* attacker = a_event->cause.get()->As<RE::Actor>();

    if (!attacker) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!target || target->IsDead()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    bool PlayPaired = false;
    bool PlayStagger = false;
    bool PlayParry = false;
    float StaggerAmount = 1.0f;
    float StaggerDirection = 0.0f;
    int nStagger = 0;
	int nPaired = 0;
    ParryType type = ParryTimerManager::GetParryType(target->GetFormID());
    
    if (type != ParryType::None) {
        if (attacker == player || target == player) {
            ApplySlowTime(g_SlowTimeMultiplier);
            ResetTimeTask();
        }
        // Aplica os efeitos e seta as variáveis no alvo
        PlayParryEffects(target, type);

        // Faz o ATACANTE entrar em stagger (quem bateu no escudo/arma)
        if (attacker) {
            attacker->SetGraphVariableFloat("staggerMagnitude", 1.0f); //
            attacker->NotifyAnimationGraph("staggerStart"); //

            // Opcional: Se for perfeito, o atacante fica mais tempo em stagger
            if (type == ParryType::Perfect) {
                attacker->SetGraphVariableFloat("staggerMagnitude", 2.0f); //
            }
        }

        // Limpa a janela para evitar múltiplos acionamentos no mesmo hit
        ParryTimerManager::RemoveWindow(target->GetFormID());
        return RE::BSEventNotifyControl::kContinue;
    }
    float damageTaken = target->GetTrackedDamage();
	logger::info("Damage Taken: {}", damageTaken);
    attacker->GetGraphVariableBool("Paired_AnimationCMF", PlayPaired);
    attacker->GetGraphVariableBool("Stagger_AnimationCMF", PlayStagger);
    target->GetGraphVariableBool("Parry_AnimationCMF", PlayParry);

    attacker->GetGraphVariableFloat("StaggerAmount_AnimationCMF", StaggerAmount);
    attacker->GetGraphVariableFloat("StaggerDirection_AnimationCMF", StaggerDirection);
    attacker->GetGraphVariableInt("nStagger_AnimationCMF", nStagger);
    target->SetGraphVariableInt("nStagger_AnimationCMF", nStagger);
    attacker->SetGraphVariableInt("nStagger_AnimationCMF", 0);

    if (PlayPaired == true) {
        if (target && !target->IsDead() && target->IsHumanoid()) {
            attacker->NotifyAnimationGraph("MCO_EndAnimation");
            target->NotifyAnimationGraph("MCO_EndAnimation");
            PlayIdleAnimationTarget(attacker, anim, target);
            //logger::info("Playing paired animation and closed the window");
            attacker->SetGraphVariableBool("Paired_AnimationCMF", false);
            //attacker->SetGraphVariableInt("nPaired_AnimationCMF", 0);
        }
    }
    else if (PlayStagger == true) {
        if (target && !target->IsDead()) {
            float direction = CalculateStaggerDirection(attacker, target);
            target->SetGraphVariableFloat("staggerMagnitude", StaggerAmount);
            target->SetGraphVariableFloat("staggerDirection", direction);
            //PlayIdleAnimationTarget(target, stagger, target);
            target->NotifyAnimationGraph("staggerStart");

            attacker->SetGraphVariableBool("Stagger_AnimationCMF", false);
            //target->SetGraphVariableInt("nStagger_AnimationCMF", 0);
        }
    }else if (PlayParry == true) {
        if (target && !target->IsDead()) {
            attacker->NotifyAnimationGraph("staggerStart");
            if (attacker == player || target == player) {
                ApplySlowTime(g_SlowTimeMultiplier);
                ResetTimeTask();
            }
            float damageTaken = target->GetTrackedDamage();
            // Aplica os efeitos e seta as variáveis no alvo
            PlayParryEffects(attacker, type);
            target->SetGraphVariableBool("Parry_AnimationCMF", false);
            //attacker->SetGraphVariableInt("nPaired_AnimationCMF", 0);
        }
	}
    
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Sink::NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
    if (!a_event || !a_event->actor) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto actor = a_event->actor.get();
    auto* npc = actor->As<RE::Actor>();
    if (npc && npc != RE::PlayerCharacter::GetSingleton()) {  // Garante que é um ator válido
        switch (a_event->newState.get()) {
        case RE::ACTOR_COMBAT_STATE::kCombat:
            NpcCombatTracker::RegisterSink(npc);
            break;
        case RE::ACTOR_COMBAT_STATE::kNone:
            NpcCombatTracker::UnregisterSink(npc);
            break;
        }
    }


    return RE::BSEventNotifyControl::kContinue;
}

void Sink::NpcCombatTracker::RegisterSink(RE::Actor* a_actor)
{
    std::unique_lock lock(g_mutex);
    if (g_trackedNPCs.find(a_actor->GetFormID()) == g_trackedNPCs.end()) {
        a_actor->AddAnimationGraphEventSink(&g_npcSink);
        g_trackedNPCs.insert(a_actor->GetFormID());
        //SKSE::log::info("[NpcCombatTracker] Começando a rastrear animações do ator {:08X}", a_actor->GetFormID());
    }
}

void Sink::NpcCombatTracker::UnregisterSink(RE::Actor* a_actor)
{
    if (!a_actor || a_actor->IsPlayerRef()) return;

    std::unique_lock lock(g_mutex);
    if (g_trackedNPCs.find(a_actor->GetFormID()) != g_trackedNPCs.end()) {
        a_actor->RemoveAnimationGraphEventSink(&g_npcSink);
        g_trackedNPCs.erase(a_actor->GetFormID());
        //SKSE::log::info("[NpcCombatTracker] Parando de rastrear animações do ator {:08X}", a_actor->GetFormID());
    }
}

void Sink::NpcCombatTracker::RegisterSinksForExistingCombatants()
{
    SKSE::log::info("[NpcCombatTracker] Verificando NPCs já em combate após carregar o jogo...");

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        SKSE::log::warn("[NpcCombatTracker] Não foi possível obter ProcessLists.");
        return;
    }

    // Itera sobre todos os atores que estão "ativos" no jogo
    for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get().get()) {
            // A função IsInCombat() nos diz se o ator já está em um estado de combate
            if (!actor->IsPlayerRef()) {
                if (actor->IsInCombat()) {
                    SKSE::log::info("[NpcCombatTracker] Ator '{}' ({:08X}) já está em combate. Registrando sink...",
                        actor->GetName(), actor->GetFormID());
                    // Usamos a mesma função de registro que já existe!
                    RegisterSink(actor);
                }
            }

        }
    }

    SKSE::log::info("[NpcCombatTracker] Verificação concluída.");
}

RE::BSEventNotifyControl Sink::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
    if (a_event && a_event->holder) {
        auto actor = a_event->holder->As<RE::Actor>();
        if (!actor) {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto npc = const_cast<RE::Actor*>(actor);
        const RE::FormID formID = actor->GetFormID();
        const std::string_view eventName = a_event->tag;
        //logger::info("[NPC Anim Event] Ator: '{}' ({:08X}), Evento: '{}'", actor->GetName(), actor->GetFormID(),eventName);
		auto player = RE::PlayerCharacter::GetSingleton();
        if (eventName == "blockStartOut") {
            npc->SetGraphVariableBool("Parry_AnimationCMF", true);
            ParryTimerManager::StartWindow(formID);
            logger::info("Janela de Parry iniciada para: {:08X}", formID);
            player->SetGraphVariableBool("Paired_AnimationCMF", true);

        }
        
    }
    return RE::BSEventNotifyControl::kContinue;
}

void Sink::ParryTimerManager::StartWindow(RE::FormID a_formID) {
    std::unique_lock lock(g_parryMutex);
    g_parryWindows[a_formID] = std::chrono::steady_clock::now();
}

Sink::ParryType Sink::ParryTimerManager::GetParryType(RE::FormID a_formID) {
    std::shared_lock lock(g_parryMutex);
    auto it = g_parryWindows.find(a_formID);

    if (it != g_parryWindows.end()) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();

        if (duration <= g_PerfectParryMS) {
            return ParryType::Perfect;
        }
        else if (duration <= g_NormalParryMS) {
            return ParryType::Normal;
        }
    }
    return ParryType::None;
}



void Sink::ParryTimerManager::RemoveWindow(RE::FormID a_formID) {
    std::unique_lock lock(g_parryMutex);
    g_parryWindows.erase(a_formID);
}

RE::TESEffectShader* Sink::GetEffectShaderByFormID(RE::FormID a_formID, const std::string& a_pluginName) {
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    auto* lookupForm = dataHandler ? dataHandler->LookupForm(a_formID, a_pluginName) : nullptr;

    // 0x55 na sua lista é EffectShader (TESEffectShader)
    if (lookupForm && lookupForm->GetFormType() == RE::FormType::EffectShader) {
        return static_cast<RE::TESEffectShader*>(lookupForm);
    }

    SKSE::log::warn("Não foi possível encontrar EffectShader 0x{:X} no plugin {}", a_formID, a_pluginName);
    return nullptr;
}

void Sink::ApplySlowTime(float a_multiplier)
{
    auto* timer = RE::BSTimer::GetSingleton();
    if (timer) {
        // Usamos a função fornecida: o segundo parâmetro (bool) 
        // geralmente define se a mudança é imediata
        timer->SetGlobalTimeMultiplier(a_multiplier, true);
    }

}

void Sink::ResetTimeTask()
{
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_SlowTimeDurationMS));

        // Retorna para a thread principal do SKSE para evitar instabilidade
        SKSE::GetTaskInterface()->AddTask([]() {
            auto* timer = RE::BSTimer::GetSingleton();
            if (timer) {
                timer->SetGlobalTimeMultiplier(1.0f, true);
            }
            });
        }).detach();

}

void Sink::PlayParryEffects(RE::Actor* a_target, ParryType a_type) {
    if (!a_target) return;

    if (a_type == ParryType::Perfect) {
        // 1. Variável para o Behavior (NPC ou Player)
        a_target->SetGraphVariableBool("PerfectParry_AnimationCMF", true); //

        // 2. Som de Parry Perfeito (ex: um "tink" mais agudo ou eco)
        RE::PlaySound("WPNMagicalWeaponImpactMetal");
        logger::info("PARRY PERFEITO executado por: {:08X}", a_target->GetFormID());

    }
    else if (a_type == ParryType::Normal) {
        auto* parryVisualEffect = GetEffectShaderByFormID(0x802, "Trigger Combat Behaviour.esp");
        a_target->ApplyEffectShader(parryVisualEffect, 10.5f, nullptr, false, false);
        logger::info("Efeito visual de Parry aplicado em: {}", a_target->GetName());
    }
}
