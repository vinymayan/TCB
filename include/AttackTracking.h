#pragma once

#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include <unordered_map>

#include "PayloadAPI.h"

namespace AttackTracking {
    struct Config {
        float maxAngleDegrees = 60.0f;
        float turnSpeedDegreesPerSecond = 180.0f;
        float strength = 8.0f;
        bool allowRetargeting = false;
    };

    class Manager {
    public:
        static Manager* GetSingleton();

        bool Start(RE::Actor* a_actor, const Config& a_config);
        bool StartMagnetism(RE::Actor* a_actor, const Config& a_config);
        void Stop(RE::Actor* a_actor);
        void End(RE::Actor* a_actor);
        void Update(RE::Actor* a_actor, float a_deltaTime);
        void Clear();

        void ApplyRootMotionYawCorrection(const RE::ActorState* a_actorState, RE::NiPoint3& a_rotation);

    private:
        struct State {
            RE::ActorHandle actor;
            RE::ActorHandle target;
            Config config;
            float attackStartYaw = 0.0f;
            float continuousTargetYaw = 0.0f;
            float continuousActorYaw = 0.0f;
            float controlledYaw = 0.0f;
            float lastMovementYawCorrection = 0.0f;
            float lastTurnDirection = 0.0f;
            float maxAbsYawError = 0.0f;
            float maxAbsYawStep = 0.0f;
            float totalAbsYawStep = 0.0f;
            std::uint64_t revision = 0;
            std::uint64_t updateCalls = 0;
            std::uint64_t appliedUpdateCalls = 0;
            bool tracking = true;
            bool hasContinuousYaw = false;
            bool isMagnetism = false;
            bool requiresTDMTargetLock = false;
            bool ownsTDMYawControl = false;
            std::chrono::steady_clock::time_point startedAt;
            std::chrono::steady_clock::time_point lastUpdateAt;
            std::chrono::steady_clock::time_point lastUpdateDebugAt;
            std::chrono::steady_clock::time_point lastRootDebugAt;
            std::uint64_t rootHookCalls = 0;
        };

        bool StartInternal(RE::Actor* a_actor, const Config& a_config, bool a_isMagnetism);
        void End(const RE::ActorState* a_actorState);

        mutable std::shared_mutex _mutex;
        std::unordered_map<const RE::ActorState*, State> _states;
        std::uint64_t _nextRevision = 1;
    };

    class TrackStartPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static TrackStartPayloadHandler* GetSingleton();

        void Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                     RE::BShkbAnimationGraph* a_animationGraph) override;
    };

    class TrackStopPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static TrackStopPayloadHandler* GetSingleton();

        void Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                     RE::BShkbAnimationGraph* a_animationGraph) override;
    };

    class MagnetismStartPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static MagnetismStartPayloadHandler* GetSingleton();

        void Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                     RE::BShkbAnimationGraph* a_animationGraph) override;
    };

    class MagnetismLockPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static MagnetismLockPayloadHandler* GetSingleton();

        void Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                     RE::BShkbAnimationGraph* a_animationGraph) override;
    };

    class MagnetismStopPayloadHandler final : public payloadinterpreter::PayloadHandler {
    public:
        static MagnetismStopPayloadHandler* GetSingleton();

        void Process(RE::TESObjectREFR* a_holder, const std::string_view& a_payload,
                     RE::BShkbAnimationGraph* a_animationGraph) override;
    };

    void RegisterPayloadHandlers(payloadinterpreter::API::Message* a_message);
}
