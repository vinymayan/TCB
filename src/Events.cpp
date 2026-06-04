#include "Events.h"
#include "DelayedDispatcher.h"


RE::TESIdleForm* anim = nullptr;

namespace TimeStop {
    // Estrutura para salvar o que cada ator congelou
    struct FrozenData {
        std::vector<RE::ActorHandle> actors;
        std::vector<std::pair<RE::ObjectRefHandle, RE::NiPoint3>> projectiles;
    };

    std::unordered_map<RE::FormID, FrozenData> g_ActiveTimeStops;
    std::shared_mutex g_timeStopMutex;

    void Pause3DControllers(RE::NiAVObject* object, bool pause) {
        if (!object) return;

        auto controller = object->GetControllers();
        while (controller) {
            controller->frequency = pause ? 0.0f : 1.0f;
            if (pause) {
                controller->flags.reset(RE::NiTimeController::Flag::kActive);
            }
            else {
                controller->flags.set(RE::NiTimeController::Flag::kActive);
            }
            controller = controller->next.get();
        }

        if (auto node = object->AsNode()) {
            for (auto& child : node->GetChildren()) {
                Pause3DControllers(child.get(), pause);
            }
        }
    }

    void SetActorFrozen(RE::Actor* target, bool frozen) {
        if (!target) return;

        if (frozen) {
            target->EnableAI(false);
            target->SetGraphVariableFloat("SpeedPlay", 0.0f);
            if (auto root3D = target->Get3D()) {
                Pause3DControllers(root3D, true);
            }
        }
        else {
            target->EnableAI(true);
            target->SetGraphVariableFloat("SpeedPlay", 1.0f);
            if (auto root3D = target->Get3D()) {
                Pause3DControllers(root3D, false);
            }
        }
    }

    void StartTimeStop(RE::Actor* caster) {
        if (!caster) return;

        std::vector<RE::ActorHandle> affectedActors;
        std::vector<std::pair<RE::ObjectRefHandle, RE::NiPoint3>> affectedProjectiles;

        if (auto tes = RE::TES::GetSingleton(); tes) {
            tes->ForEachReferenceInRange(caster, 1500.0f, [&](RE::TESObjectREFR* ref) {
                if (!ref) return RE::BSContainer::ForEachResult::kContinue;

                if (auto a = ref->As<RE::Actor>()) {
                    // Verifica se não é o caster, não está morto, e se são hostis um ao outro (Aplica-se ao Player também)
                    if (a != caster && !a->IsDead() && !a->IsDisabled() && (a->IsHostileToActor(caster) || caster->IsHostileToActor(a))) {
                        SetActorFrozen(a, true);
                        affectedActors.push_back(a->GetHandle());
                    }
                }
                else if (auto proj = ref->As<RE::Projectile>()) {
                    RE::NiPoint3 currentVel = proj->GetProjectileRuntimeData().linearVelocity;
                    affectedProjectiles.push_back({ proj->GetHandle(), currentVel });

                    proj->GetProjectileRuntimeData().linearVelocity = RE::NiPoint3(0.0f, 0.0f, 0.0f);

                    if (auto root3D = proj->Get3D()) {
                        Pause3DControllers(root3D, true);
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
                });
        }

        if (!affectedActors.empty() || !affectedProjectiles.empty()) {
            std::unique_lock lock(g_timeStopMutex);
            g_ActiveTimeStops[caster->GetFormID()] = { affectedActors, affectedProjectiles };
        }
    }

    void StopTimeStop(RE::Actor* caster) {
        if (!caster) return;

        FrozenData data;
        {
            std::unique_lock lock(g_timeStopMutex);
            auto it = g_ActiveTimeStops.find(caster->GetFormID());
            if (it != g_ActiveTimeStops.end()) {
                data = it->second;
                g_ActiveTimeStops.erase(it); // Remove o registro
            }
            else {
                return; // Nada foi congelado por esse ator
            }
        }

        // Descongela Atores
        for (auto& handle : data.actors) {
            if (auto a = handle.get(); a) {
                if (!a->IsDead() && !a->IsDisabled() && a->Is3DLoaded()) {
                    SetActorFrozen(a.get(), false);
                }
            }
        }

        // Restaura Projéteis
        for (auto& pair : data.projectiles) {
            if (auto ref = pair.first.get(); ref) {
                if (!ref->IsDisabled() && ref->Is3DLoaded()) {
                    if (auto proj = ref->As<RE::Projectile>()) {
                        proj->GetProjectileRuntimeData().linearVelocity = pair.second;
                        if (auto root3D = proj->Get3D()) {
                            Pause3DControllers(root3D, false);
                        }
                    }
                }
            }
        }
    }
}

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
    bool PlayPairedAll = false;
    bool PlayStagger = false;
    bool PlayParry = false;
    float StaggerAmount = 1.0f;
    float StaggerDirection = 0.0f;
    int nStagger = 0;
	int nPaired = 0;

    attacker->GetGraphVariableBool("Paired_AnimationCMF", PlayPaired);
    attacker->GetGraphVariableBool("PairedAllCMF", PlayPairedAll);
    attacker->GetGraphVariableBool("Stagger_AnimationCMF", PlayStagger);
    target->GetGraphVariableBool("Parry_AnimationCMF", PlayParry);

    attacker->GetGraphVariableFloat("StaggerAmount_AnimationCMF", StaggerAmount);
    attacker->GetGraphVariableFloat("StaggerDirection_AnimationCMF", StaggerDirection);
    attacker->GetGraphVariableInt("nStagger_AnimationCMF", nStagger);
    target->SetGraphVariableInt("nStagger_AnimationCMF", nStagger);
    attacker->SetGraphVariableInt("nStagger_AnimationCMF", 0);

    if (PlayPaired == true) {
        if (target && !target->IsDead() && target->IsHumanoid()) {
            if (target == player || attacker == player) {
                if (tdmAPI && tdmAPI->GetTargetLockState()) {
                    auto myPluginHandle = SKSE::GetPluginHandle();

                    // Força a desativação temporária (limpa o lock-on instantaneamente)
                    tdmAPI->RequestDisableDirectionalMovement(myPluginHandle);
                    // Libera logo em seguida para que o movimento direcional volte a funcionar normalmente
                    tdmAPI->ReleaseDisableDirectionalMovement(myPluginHandle);
                }
            }
            attacker->NotifyAnimationGraph("MCO_EndAnimation");
            target->NotifyAnimationGraph("MCO_EndAnimation");
            PlayIdleAnimationTarget(attacker, anim, target);
            attacker->SetGraphVariableBool("Paired_AnimationCMF", false);
            attacker->SetGraphVariableBool("PairedAllCMF", false);
            //attacker->SetGraphVariableInt("nPaired_AnimationCMF", 0);
        }
    }
    else if (PlayStagger == true) {
        if (target && !target->IsDead()) {
            float direction = CalculateStaggerDirection(attacker, target);
            target->SetGraphVariableFloat("staggerMagnitude", StaggerAmount);
            target->SetGraphVariableFloat("staggerDirection", direction);
            target->NotifyAnimationGraph("staggerStart");
            attacker->SetGraphVariableBool("Stagger_AnimationCMF", false);
        }
    }else if (PlayPairedAll == true) {
        if (target && !target->IsDead()) {
            if (target == player || attacker == player) {
                if (tdmAPI && tdmAPI->GetTargetLockState()) {
                    auto myPluginHandle = SKSE::GetPluginHandle();

                    // Força a desativação temporária (limpa o lock-on instantaneamente)
                    tdmAPI->RequestDisableDirectionalMovement(myPluginHandle);
                    // Libera logo em seguida para que o movimento direcional volte a funcionar normalmente
                    tdmAPI->ReleaseDisableDirectionalMovement(myPluginHandle);
                }
			}
            attacker->NotifyAnimationGraph("MCO_EndAnimation");
            target->NotifyAnimationGraph("MCO_EndAnimation");
            PlayIdleAnimationTarget(attacker, anim, target);
            attacker->SetGraphVariableBool("PairedAllCMF", false);
            attacker->SetGraphVariableBool("Paired_AnimationCMF", false);
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
    if (!a_actor) return;

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

    for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get().get()) {
            if (!actor->IsPlayerRef()) {
                if (actor->IsInCombat()) {
                    RegisterSink(actor);
                }
            }

        }
    }

    SKSE::log::info("[NpcCombatTracker] Verificação concluída.");
}

// -----------------------------------------------------------------------------
// SISTEMA DE MAGNETISMO (EVENT-DRIVEN / SNAP)
// -----------------------------------------------------------------------------
namespace Magnetism {

    void SnapToTarget(RE::Actor* a_actor) {
        if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded()) return;

        // Pega o alvo atual de combate do ator
        RE::ActorHandle targetHandle = a_actor->GetActorRuntimeData().currentCombatTarget;
        auto target = targetHandle.get();

        // Se não tiver alvo em combate, cancela
        if (!target || target->IsDead() || !a_actor->Is3DLoaded()) return;

        // Pega as posições
        auto actorPos = a_actor->GetPosition();
        auto targetPos = target->GetPosition();

        // Calcula a direção
        float dx = targetPos.x - actorPos.x;
        float dy = targetPos.y - actorPos.y;
        float dz = targetPos.z - actorPos.z;

        // Distância horizontal (hipotenusa de X e Y)
        float distanceXY = std::sqrt(dx * dx + dy * dy);

        // Evita divisão por zero ou proximidade extrema bizarra
        if (distanceXY < 0.1f) {
            return;
        }

        // 1. CALCULA O ÂNGULO HORIZONTAL (Yaw - Esquerda/Direita)
        float desiredYaw = std::atan2(dx, dy);

        // 2. CALCULA O ÂNGULO VERTICAL (Pitch - Cima/Baixo)
        float desiredPitch = std::atan2(dz, distanceXY);

        // --- PROTEÇÃO CONTRA CRASHES ---
        if (std::isnan(desiredYaw) || std::isinf(desiredYaw) ||
            std::isnan(desiredPitch) || std::isinf(desiredPitch)) {
            SKSE::log::warn("[SnapToTarget] ALERTA: Ângulo inválido detectado! Rotação cancelada.");
            return;
        }

        SKSE::log::debug("[SnapToTarget] Snap ativado -> Yaw: {:.4f} | Pitch: {:.4f}", desiredYaw, desiredPitch);
        a_actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kHeadingFixed);
        a_actor->SetHeading(desiredYaw);   // Alinha o corpo horizontalmente
        a_actor->SetLooking(desiredPitch); // Alinha o olhar/mira verticalmente
    }
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
        if (eventName == "IframeStartCMF") {
            npc->SetGraphVariableBool("hasIframeCMF", true);
        }
        else if (eventName == "IframeEndCMF") {
            npc->SetGraphVariableBool("hasIframeCMF", false);
        }
        else if (eventName == "StopTimeStartCMF") {
            TimeStop::StartTimeStop(npc);
        }
        else if (eventName == "StopTimeEndCMF") {
            TimeStop::StopTimeStop(npc);
        }
        else if (eventName == "SnapToTargetCMF") {
            Magnetism::SnapToTarget(npc);
        }
        else if (eventName == "ParriedStartCMF") {
            npc->SetGraphVariableBool("PairedAllCMF", true);
        }
        else if (eventName == "ParriedEndCMF") {
            npc->SetGraphVariableBool("PairedAllCMF", false);
        }
        else if (eventName == "attackStop" || eventName == "attackStart" || eventName == "CastOKStop" ||
            eventName == "attackPowerStartRight" ||
            eventName == "attackPowerStartInPlace" ||
            eventName == "attackPowerStartForwardH2HRightHand" ||
            eventName == "attackPowerStartBackward" ||
            eventName == "attackPowerStartLeft" ||
            eventName == "PowerAttack_Start_end" ||
            eventName == "bashPowerStart" ||
            eventName == "PowerAttackStop" ||
            eventName == "attackPowerStartInPlaceLeftHand" ||
            eventName == "attackPowerStartForwardLeftHand" ||
            eventName == "attackPowerStartDualWield" ||
            eventName == "attackPowerStartRightLeftHand" ||
            eventName == "attackPowerStartLeftLeftHand" ||
            eventName == "attackPowerStartBackLeftHand" ||
            eventName == "attackPowerStarth2HCombo" ||
            eventName == "attackPowerStartForwardH2HLeftHand" ||
            eventName == "attackPowerStartForward" ||
            eventName == "attackPowerStart_2HMSprint" ||
            eventName == "attackPowerStart_Sprint" ||
            eventName == "attackPowerStart_2HWSprint" ||
            eventName == "attackPowerStart_SprintLeftHand" ||
            eventName == "blockStart" || eventName == "attackStartDualWield") {

            npc->SetGraphVariableBool("IsPowerAttackingCMF", npc->IsPowerAttacking());
        }


    }
    return RE::BSEventNotifyControl::kContinue;
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
void ScheduleSinkRegistration(RE::Actor* actor, int attempts)
{
    if (attempts > 20) {
        SKSE::log::critical("[Actor3DLoadEventHandler] Desistindo após {} tentativas para o ator {:08X}.", attempts, actor->GetFormID());
        return;
    }

    auto actorHandle = actor->CreateRefHandle();

    Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(100), [actorHandle, attempts]() {
        SKSE::GetTaskInterface()->AddTask([actorHandle, attempts]() {
            if (!actorHandle) return;
            if (!actorHandle.get()) return;

            auto actor = actorHandle.get();

            RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
            actor->GetAnimationGraphManager(graphManager);

            if (graphManager) {
                    Sink::NpcCombatTracker::UnregisterSink(actor.get());
                    Sink::NpcCombatTracker::RegisterSink(actor.get());
            }
            else {
                // Graph ainda nulo, tenta de novo
                ScheduleSinkRegistration(actor.get(), attempts + 1);
            }
            });
        });
}
RE::BSEventNotifyControl Sink::PC3DLoadEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
    if (!a_event || !a_event->loaded) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Em vez de pegar o Player Singleton, buscamos o formulário pelo ID do evento
    auto* form = RE::TESForm::LookupByID(a_event->formID);
    if (!form) return RE::BSEventNotifyControl::kContinue;

    // Tentamos converter para Ator. Se não for ator (ex: uma parede), ignoramos.
    auto* actor = form->As<RE::Actor>();

    if (actor) {
        ScheduleSinkRegistration(actor, 0);
    }

    return RE::BSEventNotifyControl::kContinue;
}
