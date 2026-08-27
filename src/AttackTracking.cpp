#include "AttackTracking.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

#include "Events.h"

namespace {
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

    float NormalizeAngle(float a_angle) { return std::remainder(a_angle, kTwoPi); }

    float UnwrapAngleNear(float a_angle, float a_reference, float a_preferredDirection = 0.0f) {
        float delta = NormalizeAngle(a_angle - a_reference);
        constexpr float kOppositeAngleEpsilon = 0.0005f;
        if (std::abs(std::abs(delta) - std::numbers::pi_v<float>) <= kOppositeAngleEpsilon &&
            std::abs(a_preferredDirection) > 0.0f) {
            delta = std::copysign(std::numbers::pi_v<float>, a_preferredDirection);
        }
        return a_reference + delta;
    }

    std::string_view Trim(std::string_view a_value) {
        while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.front()))) {
            a_value.remove_prefix(1);
        }
        while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.back()))) {
            a_value.remove_suffix(1);
        }
        return a_value;
    }

    bool ParseFloat(std::string_view a_value, float& a_result) {
        a_value = Trim(a_value);
        if (a_value.empty()) {
            return false;
        }

        const auto* begin = a_value.data();
        const auto* end = begin + a_value.size();
        const auto [ptr, error] = std::from_chars(begin, end, a_result);
        return error == std::errc{} && ptr == end && std::isfinite(a_result);
    }

    bool ParseBool(std::string_view a_value, bool& a_result) {
        a_value = Trim(a_value);
        std::string normalized{a_value};
        std::ranges::transform(normalized, normalized.begin(),
                               [](unsigned char a_character) { return static_cast<char>(std::tolower(a_character)); });

        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
            a_result = true;
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
            a_result = false;
            return true;
        }
        return false;
    }

    std::vector<std::string_view> SplitPayload(std::string_view a_payload) {
        a_payload = Trim(a_payload);
        if (!a_payload.empty() && a_payload.front() == '.') {
            a_payload.remove_prefix(1);
        }

        std::vector<std::string_view> values;
        while (true) {
            const auto separator = a_payload.find('|');
            values.push_back(Trim(a_payload.substr(0, separator)));
            if (separator == std::string_view::npos) {
                break;
            }
            a_payload.remove_prefix(separator + 1);
        }
        return values;
    }

    bool ParseConfig(std::string_view a_payload, AttackTracking::Config& a_config) {
        const auto values = SplitPayload(a_payload);
        if (values.empty() || values.front().empty()) {
            return true;
        }

        if (!ParseFloat(values[0], a_config.maxAngleDegrees)) {
            return false;
        }
        if (values.size() > 1 && !values[1].empty() && !ParseFloat(values[1], a_config.turnSpeedDegreesPerSecond)) {
            return false;
        }
        if (values.size() > 2 && !values[2].empty() && !ParseFloat(values[2], a_config.strength)) {
            return false;
        }
        if (values.size() > 3 && !values[3].empty() && !ParseBool(values[3], a_config.allowRetargeting)) {
            return false;
        }

        a_config.maxAngleDegrees = std::clamp(a_config.maxAngleDegrees, 0.0f, 180.0f);
        a_config.turnSpeedDegreesPerSecond = std::clamp(a_config.turnSpeedDegreesPerSecond, 0.0f, 1080.0f);
        a_config.strength = std::clamp(a_config.strength, 0.0f, 100.0f);
        return true;
    }

    RE::NiPointer<RE::Actor> GetValidCombatTarget(RE::Actor* a_actor) {
        if (!a_actor) {
            return {};
        }

        auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
        if (!target || target.get() == a_actor || target->IsDead() || target->IsDisabled()) {
            return {};
        }
        return target;
    }

    RE::NiPointer<RE::Actor> GetValidTDMTarget(RE::Actor* a_actor) {
        if (!a_actor || !a_actor->IsPlayerRef() || !tdmAPI || !tdmAPI->GetTargetLockState()) {
            return {};
        }

        auto target = tdmAPI->GetCurrentTarget().get();
        if (!target || target.get() == a_actor || target->IsDead() || target->IsDisabled()) {
            return {};
        }
        return target;
    }

    bool AcquireTDMYawControl() {
        if (!tdmAPI) {
            return false;
        }

        const auto result = tdmAPI->RequestYawControl(SKSE::GetPluginHandle(), 0.0f);
        return result == TDM_API::APIResult::OK || result == TDM_API::APIResult::AlreadyGiven;
    }

    void ReleaseTDMYawControl() {
        if (tdmAPI) {
            tdmAPI->ReleaseYawControl(SKSE::GetPluginHandle());
        }
    }
}

namespace AttackTracking {
    Manager* Manager::GetSingleton() {
        static Manager singleton;
        return std::addressof(singleton);
    }

    bool Manager::Start(RE::Actor* a_actor, const Config& a_config) { return StartInternal(a_actor, a_config, false); }

    bool Manager::StartMagnetism(RE::Actor* a_actor, const Config& a_config) {
        return StartInternal(a_actor, a_config, true);
    }

    bool Manager::StartInternal(RE::Actor* a_actor, const Config& a_config, bool a_isMagnetism) {
        if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled() || (!a_isMagnetism && a_actor->IsPlayerRef())) {
            return false;
        }

        auto* actorState = a_actor->AsActorState();
        if (!actorState) {
            return false;
        }

        State state;
        state.actor = a_actor->GetHandle();
        state.isMagnetism = a_isMagnetism;
        state.requiresTDMTargetLock = a_isMagnetism && a_actor->IsPlayerRef();
        state.startedAt = std::chrono::steady_clock::now();
        state.lastUpdateAt = state.startedAt;

        auto target = state.requiresTDMTargetLock ? GetValidTDMTarget(a_actor) : GetValidCombatTarget(a_actor);
        if (state.requiresTDMTargetLock && !target) {
            SKSE::log::info("[MagnetismTCB] Start ignorado: player sem target lock valido.");
            return false;
        }
        if (target) {
            state.target = target->GetHandle();
        }

        if (state.requiresTDMTargetLock) {
            if (!AcquireTDMYawControl()) {
                SKSE::log::warn("[AttackTracking] TDM recusou o controle de yaw para MagnetismStartTCB.");
                return false;
            }
            state.ownsTDMYawControl = true;
        }

        state.config = a_config;
        state.attackStartYaw = a_actor->data.angle.z;
        state.controlledYaw = NormalizeAngle(state.attackStartYaw);

        {
            std::unique_lock lock(_mutex);
            state.revision = _nextRevision++;
            _states.insert_or_assign(actorState, state);
        }

        target = state.target.get();
        logger::debug("[{}][Start] session={} actor={:08X} target={:08X} attackStartYaw={:.4f} maxAngle={} "
                      "turnSpeed={} strength={} retarget={}",
                      a_isMagnetism ? "MagnetismTCB" : "TrackTCB", state.revision, a_actor->GetFormID(),
                      target ? target->GetFormID() : 0, state.attackStartYaw, state.config.maxAngleDegrees,
                      state.config.turnSpeedDegreesPerSecond, state.config.strength, state.config.allowRetargeting);
        if (a_isMagnetism) {
            SKSE::log::info("[MagnetismTCB] Start actor={:08X}, target={:08X}", a_actor->GetFormID(),
                            target ? target->GetFormID() : 0);
        }
        return true;
    }

    void Manager::Stop(RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }

        auto* actorState = a_actor->AsActorState();
        if (!actorState) {
            return;
        }

        std::unique_lock lock(_mutex);
        const auto it = _states.find(actorState);
        if (it != _states.end()) {
            // Keep reconciling movement to the last controlled yaw until the
            // attack ends. This locks the remaining root motion without
            // continuing to turn toward the target.
            it->second.tracking = false;
            logger::debug("[{}][Lock] session={} actor={:08X} lockedControlledYaw={:.4f} "
                          "lastMovementCorrection={:.4f} updates={} appliedUpdates={} rootHookCalls={}",
                          it->second.isMagnetism ? "MagnetismTCB" : "TrackTCB", it->second.revision,
                          a_actor->GetFormID(), it->second.controlledYaw,
                          it->second.lastMovementYawCorrection, it->second.updateCalls, it->second.appliedUpdateCalls,
                          it->second.rootHookCalls);
            if (it->second.isMagnetism) {
                SKSE::log::info("[MagnetismTCB] Lock actor={:08X}", a_actor->GetFormID());
            }
        }
    }

    void Manager::End(RE::Actor* a_actor) {
        if (a_actor) {
            End(a_actor->AsActorState());
        }
    }

    void Manager::End(const RE::ActorState* a_actorState) {
        if (!a_actorState) {
            return;
        }

        bool releaseTDMYaw = false;
        bool wasMagnetism = false;
        RE::FormID actorFormID = 0;
        float finalControlledYaw = 0.0f;
        float finalMovementCorrection = 0.0f;
        float maxAbsYawError = 0.0f;
        float maxAbsYawStep = 0.0f;
        float totalAbsYawStep = 0.0f;
        std::uint64_t session = 0;
        std::uint64_t updateCalls = 0;
        std::uint64_t appliedUpdateCalls = 0;
        std::uint64_t rootHookCalls = 0;
        {
            std::unique_lock lock(_mutex);
            const auto it = _states.find(a_actorState);
            if (it == _states.end()) {
                return;
            }
            releaseTDMYaw = it->second.ownsTDMYawControl;
            wasMagnetism = it->second.isMagnetism;
            finalControlledYaw = it->second.controlledYaw;
            finalMovementCorrection = it->second.lastMovementYawCorrection;
            maxAbsYawError = it->second.maxAbsYawError;
            maxAbsYawStep = it->second.maxAbsYawStep;
            totalAbsYawStep = it->second.totalAbsYawStep;
            session = it->second.revision;
            updateCalls = it->second.updateCalls;
            appliedUpdateCalls = it->second.appliedUpdateCalls;
            rootHookCalls = it->second.rootHookCalls;
            if (auto actor = it->second.actor.get()) {
                actorFormID = actor->GetFormID();
            }
            _states.erase(it);
        }

        if (releaseTDMYaw) {
            ReleaseTDMYawControl();
        }
        logger::debug("[{}][Stop] session={} actor={:08X} finalControlledYaw={:.4f} "
                      "lastMovementCorrection={:.4f} updates={} appliedUpdates={} maxAbsError={:.4f} "
                      "maxAbsStep={:.4f} totalAbsTrackingStep={:.4f} rootHookCalls={}",
                      wasMagnetism ? "MagnetismTCB" : "TrackTCB", session, actorFormID, finalControlledYaw,
                      finalMovementCorrection, updateCalls, appliedUpdateCalls, maxAbsYawError, maxAbsYawStep,
                      totalAbsYawStep, rootHookCalls);
        if (wasMagnetism) {
            SKSE::log::info("[MagnetismTCB] Stop actor={:08X}", actorFormID);
        }
    }

    void Manager::Update(RE::Actor* a_actor, float a_deltaTime) {
        if (!a_actor) {
            return;
        }

        auto* actorState = a_actor->AsActorState();
        if (!actorState) {
            return;
        }

        State state;
        {
            std::shared_lock lock(_mutex);
            const auto it = _states.find(actorState);
            if (it == _states.end()) {
                return;
            }
            state = it->second;
        }

        auto trackedActor = state.actor.get();
        if (!trackedActor || trackedActor.get() != a_actor || a_actor->IsDead() || a_actor->IsDisabled() ||
            !a_actor->Is3DLoaded()) {
            End(actorState);
            return;
        }

        if (state.requiresTDMTargetLock && (!tdmAPI || !tdmAPI->GetTargetLockState())) {
            End(actorState);
            return;
        }

        if (state.isMagnetism && std::chrono::steady_clock::now() - state.startedAt > std::chrono::seconds(10)) {
            SKSE::log::warn("[AttackTracking] Magnetismo encerrado pelo safeguard de 10 segundos.");
            End(actorState);
            return;
        }

        if (!state.tracking) {
            return;
        }

        auto target = state.target.get();
        const bool previousTargetInvalid = !target || target->IsDead() || target->IsDisabled();
        if (state.config.allowRetargeting || previousTargetInvalid) {
            if (previousTargetInvalid && state.target && !state.config.allowRetargeting) {
                if (state.requiresTDMTargetLock) {
                    End(actorState);
                } else {
                    Stop(a_actor);
                }
                return;
            }
            target = state.requiresTDMTargetLock ? GetValidTDMTarget(a_actor) : GetValidCombatTarget(a_actor);
        }

        if (!target) {
            if (state.requiresTDMTargetLock) {
                End(actorState);
            }
            return;
        }

        const auto actorPosition = a_actor->GetPosition();
        const auto targetPosition = target->GetPosition();
        const float deltaX = targetPosition.x - actorPosition.x;
        const float deltaY = targetPosition.y - actorPosition.y;
        if (deltaX * deltaX + deltaY * deltaY < 0.01f) {
            return;
        }

        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        const float desiredTargetYaw = std::atan2(deltaX, deltaY);
        const float maxAngle = state.config.maxAngleDegrees * std::numbers::pi_v<float> / 180.0f;
        const float currentYaw = a_actor->data.angle.z;

        float continuousTargetYaw = 0.0f;
        float continuousActorYaw = 0.0f;
        if (state.hasContinuousYaw) {
            continuousTargetYaw =
                UnwrapAngleNear(desiredTargetYaw, state.continuousTargetYaw, state.lastTurnDirection);
            continuousActorYaw = UnwrapAngleNear(currentYaw, state.continuousActorYaw, state.lastTurnDirection);
        } else {
            continuousTargetYaw = UnwrapAngleNear(desiredTargetYaw, state.attackStartYaw);
            continuousActorYaw = UnwrapAngleNear(currentYaw, state.attackStartYaw);
        }

        // 180 degrees means unrestricted yaw. Clamping a continuous angle to
        // [-pi, pi] would make tracking stick at the rear boundary after the
        // target crosses from +179 to -179 degrees.
        const bool unrestrictedYaw = maxAngle >= std::numbers::pi_v<float> - 0.0001f;
        const float targetOffset = unrestrictedYaw ? continuousTargetYaw - state.attackStartYaw :
                                                     std::clamp(continuousTargetYaw - state.attackStartYaw,
                                                                -maxAngle, maxAngle);
        const float limitedTargetYaw = state.attackStartYaw + targetOffset;
        const float yawError = limitedTargetYaw - continuousActorYaw;

        const auto updateNow = std::chrono::steady_clock::now();
        const float wallDeltaTime =
            std::chrono::duration<float>(updateNow - state.lastUpdateAt).count();
        const float frameDeltaTime = RE::GetSecondsSinceLastFrame();

        constexpr float kMinUsableDeltaTime = 0.0001f;
        float effectiveDeltaTime = a_deltaTime;
        const char* deltaSource = "hook";
        if (!std::isfinite(effectiveDeltaTime) || effectiveDeltaTime < kMinUsableDeltaTime) {
            effectiveDeltaTime = frameDeltaTime;
            deltaSource = "frame";
        }
        if (!std::isfinite(effectiveDeltaTime) || effectiveDeltaTime < kMinUsableDeltaTime) {
            effectiveDeltaTime = wallDeltaTime;
            deltaSource = "clock";
        }

        const float deltaTime = std::clamp(effectiveDeltaTime, 0.0f, 0.1f);
        const float response = std::clamp(state.config.strength * deltaTime, 0.0f, 1.0f);
        const float requestedStep = yawError * response;
        const float maxStep = state.config.turnSpeedDegreesPerSecond * std::numbers::pi_v<float> / 180.0f * deltaTime;
        const float yawStep = std::clamp(requestedStep, -maxStep, maxStep);
        const float newContinuousYaw = continuousActorYaw + yawStep;
        const float newYaw = NormalizeAngle(newContinuousYaw);
        float turnDirection = state.lastTurnDirection;
        if (std::abs(yawStep) > 0.00001f) {
            turnDirection = std::copysign(1.0f, yawStep);
        }

        bool writeUpdateDebug = false;
        {
            std::unique_lock lock(_mutex);
            const auto it = _states.find(actorState);
            if (it == _states.end() || it->second.revision != state.revision) {
                return;
            }
            it->second.target = target->GetHandle();
            it->second.continuousTargetYaw = continuousTargetYaw;
            it->second.continuousActorYaw = newContinuousYaw;
            it->second.controlledYaw = newYaw;
            it->second.lastTurnDirection = turnDirection;
            it->second.hasContinuousYaw = true;
            ++it->second.updateCalls;
            if (std::abs(yawStep) > 0.00001f) {
                ++it->second.appliedUpdateCalls;
            }
            it->second.maxAbsYawError = std::max(it->second.maxAbsYawError, std::abs(yawError));
            it->second.maxAbsYawStep = std::max(it->second.maxAbsYawStep, std::abs(yawStep));
            it->second.totalAbsYawStep += std::abs(yawStep);
            it->second.lastUpdateAt = updateNow;
            if (updateNow - it->second.lastUpdateDebugAt >= std::chrono::milliseconds(200)) {
                it->second.lastUpdateDebugAt = updateNow;
                writeUpdateDebug = true;
            }
        }

        const char* appliedBy = "none";
        if (std::abs(yawStep) > 0.00001f && state.requiresTDMTargetLock) {
            if (!tdmAPI || tdmAPI->SetPlayerYaw(SKSE::GetPluginHandle(), newYaw) != TDM_API::APIResult::OK) {
                End(actorState);
            } else {
                appliedBy = "TDM";
            }
        } else if (std::abs(yawStep) > 0.00001f) {
            a_actor->GetActorRuntimeData().boolBits.reset(RE::Actor::BOOL_BITS::kHeadingFixed);
            a_actor->SetHeading(newYaw);
            appliedBy = "SetHeading";
        }

        if (writeUpdateDebug) {
            logger::debug(
                "[{}][Update] actor={:08X} target={:08X} distance={:.2f} animationYaw={:.4f} targetYaw={:.4f} "
                "continuousTargetYaw={:.4f} limitedYaw={:.4f} error={:.4f} trackingStep={:.4f} "
                "controlledYaw={:.4f} session={} "
                "rawDelta={:.6f} usedDelta={:.6f} deltaSource={} appliedBy={}",
                state.isMagnetism ? "MagnetismTCB" : "TrackTCB", a_actor->GetFormID(), target->GetFormID(), distance,
                currentYaw, desiredTargetYaw, continuousTargetYaw, limitedTargetYaw, yawError, yawStep, newYaw,
                state.revision, a_deltaTime, deltaTime, deltaSource, appliedBy);
        }
    }

    void Manager::Clear() {
        bool releaseTDMYaw = false;
        {
            std::unique_lock lock(_mutex);
            releaseTDMYaw =
                std::ranges::any_of(_states, [](const auto& a_entry) { return a_entry.second.ownsTDMYawControl; });
            _states.clear();
        }

        if (releaseTDMYaw) {
            ReleaseTDMYawControl();
        }
    }

    void Manager::ApplyRootMotionYawCorrection(const RE::ActorState* a_actorState, RE::NiPoint3& a_rotation) {
        if (!a_actorState) {
            return;
        }

        const float rotationBefore = a_rotation.z;
        float correction = 0.0f;
        bool writeRootDebug = false;
        bool isMagnetism = false;
        RE::FormID actorFormID = 0;
        std::uint64_t session = 0;
        std::uint64_t hookCalls = 0;
        float actorVisualYaw = 0.0f;
        float controlledYaw = 0.0f;
        {
            std::unique_lock lock(_mutex);
            const auto it = _states.find(a_actorState);
            if (it == _states.end()) {
                return;
            }

            controlledYaw = it->second.controlledYaw;
            correction = NormalizeAngle(controlledYaw - rotationBefore);
            it->second.lastMovementYawCorrection = correction;
            isMagnetism = it->second.isMagnetism;
            session = it->second.revision;
            hookCalls = ++it->second.rootHookCalls;
            if (auto actor = it->second.actor.get()) {
                actorFormID = actor->GetFormID();
                actorVisualYaw = actor->data.angle.z;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - it->second.lastRootDebugAt >= std::chrono::milliseconds(200)) {
                it->second.lastRootDebugAt = now;
                writeRootDebug = true;
            }
        }

        a_rotation.z += correction;
        if (writeRootDebug) {
            logger::debug(
                "[{}][RootHook] session={} actor={:08X} calls={} animationMovementYaw={:.4f} "
                "actorVisualYaw={:.4f} controlledYaw={:.4f} movementCorrection={:.4f} finalMovementYaw={:.4f}",
                isMagnetism ? "MagnetismTCB" : "TrackTCB", session, actorFormID, hookCalls, rotationBefore,
                actorVisualYaw, controlledYaw, correction, a_rotation.z);
        }
    }

    TrackStartPayloadHandler* TrackStartPayloadHandler::GetSingleton() {
        static TrackStartPayloadHandler singleton;
        return std::addressof(singleton);
    }

    void TrackStartPayloadHandler::Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                                           RE::BShkbAnimationGraph*) {
        auto* actor = a_holder ? a_holder->As<RE::Actor>() : nullptr;
        if (!actor) {
            return;
        }

        Config config;
        if (!ParseConfig(a_payload, config)) {
            SKSE::log::warn("[AttackTracking] Payload TrackStartTCB invalido: '{}'", a_payload);
            return;
        }

        if (!Manager::GetSingleton()->Start(actor, config)) {
            SKSE::log::debug("[AttackTracking] TrackStartTCB ignorado para actor {:08X}", actor->GetFormID());
        }
    }

    TrackStopPayloadHandler* TrackStopPayloadHandler::GetSingleton() {
        static TrackStopPayloadHandler singleton;
        return std::addressof(singleton);
    }

    void TrackStopPayloadHandler::Process(RE::TESObjectREFR* a_holder, const std::string_view&,
                                          RE::BShkbAnimationGraph*) {
        auto* actor = a_holder ? a_holder->As<RE::Actor>() : nullptr;
        Manager::GetSingleton()->Stop(actor);
    }

    MagnetismStartPayloadHandler* MagnetismStartPayloadHandler::GetSingleton() {
        static MagnetismStartPayloadHandler singleton;
        return std::addressof(singleton);
    }

    void MagnetismStartPayloadHandler::Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                                               RE::BShkbAnimationGraph*) {
        auto* actor = a_holder ? a_holder->As<RE::Actor>() : nullptr;
        if (!actor) {
            return;
        }

        Config config;
        config.turnSpeedDegreesPerSecond = 360.0f;
        config.allowRetargeting = true;
        if (!ParseConfig(a_payload, config)) {
            SKSE::log::warn("[AttackTracking] Payload MagnetismStartTCB invalido: '{}'", a_payload);
            return;
        }

        if (!Manager::GetSingleton()->StartMagnetism(actor, config)) {
            SKSE::log::debug("[AttackTracking] MagnetismStartTCB ignorado para actor {:08X}", actor->GetFormID());
        }
    }

    MagnetismLockPayloadHandler* MagnetismLockPayloadHandler::GetSingleton() {
        static MagnetismLockPayloadHandler singleton;
        return std::addressof(singleton);
    }

    void MagnetismLockPayloadHandler::Process(RE::TESObjectREFR* a_holder, const std::string_view&,
                                              RE::BShkbAnimationGraph*) {
        auto* actor = a_holder ? a_holder->As<RE::Actor>() : nullptr;
        Manager::GetSingleton()->Stop(actor);
    }

    MagnetismStopPayloadHandler* MagnetismStopPayloadHandler::GetSingleton() {
        static MagnetismStopPayloadHandler singleton;
        return std::addressof(singleton);
    }

    void MagnetismStopPayloadHandler::Process(RE::TESObjectREFR* a_holder, const std::string_view&,
                                              RE::BShkbAnimationGraph*) {
        auto* actor = a_holder ? a_holder->As<RE::Actor>() : nullptr;
        Manager::GetSingleton()->End(actor);
    }

    void RegisterPayloadHandlers(payloadinterpreter::API::Message* a_message) {
        if (!a_message || !a_message->payloadHandlerCollector) {
            SKSE::log::error("[AttackTracking] Payload Interpreter enviou um coletor invalido.");
            return;
        }

        a_message->payloadHandlerCollector->RegisterPayloadHandler("TrackStartTCB",
                                                                   TrackStartPayloadHandler::GetSingleton());
        a_message->payloadHandlerCollector->RegisterPayloadHandler("TrackStopTCB",
                                                                   TrackStopPayloadHandler::GetSingleton());
        a_message->payloadHandlerCollector->RegisterPayloadHandler("MagnetismStartTCB",
                                                                   MagnetismStartPayloadHandler::GetSingleton());
        a_message->payloadHandlerCollector->RegisterPayloadHandler("MagnetismLockTCB",
                                                                   MagnetismLockPayloadHandler::GetSingleton());
        a_message->payloadHandlerCollector->RegisterPayloadHandler("MagnetismStopTCB",
                                                                   MagnetismStopPayloadHandler::GetSingleton());
        SKSE::log::info("[AttackTracking] Payloads TrackTCB e MagnetismTCB registrados.");
    }
}
