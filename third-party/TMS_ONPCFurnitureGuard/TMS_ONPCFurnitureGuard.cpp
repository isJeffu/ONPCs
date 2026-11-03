#include "PCH.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <KnownFolders.h>
#include <ShlObj_core.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    constexpr const char* kPluginName = "TMS_ONPCFurnitureGuard";
    constexpr const char* kLogFileName = "TMS_ONPCFurnitureGuard.log";

    constexpr bool kVerboseScanLog = false;

    std::filesystem::path GetDocumentsPath()
    {
        PWSTR knownPath = nullptr;

        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &knownPath);
        if (SUCCEEDED(result) && knownPath) {
            std::filesystem::path path = knownPath;
            CoTaskMemFree(knownPath);
            return path;
        }

        if (knownPath) {
            CoTaskMemFree(knownPath);
        }

        char* userProfile = nullptr;
        std::size_t len = 0;

        if (_dupenv_s(&userProfile, &len, "USERPROFILE") == 0 && userProfile && userProfile[0] != '\0') {
            std::filesystem::path path = std::filesystem::path(userProfile) / "Documents";
            std::free(userProfile);
            return path;
        }

        if (userProfile) {
            std::free(userProfile);
        }

        return std::filesystem::current_path();
    }

    std::filesystem::path GetLogPath()
    {
        return GetDocumentsPath() /
            "My Games" /
            "Skyrim Special Edition" /
            "SKSE" /
            kLogFileName;
    }

    void InitLogger()
    {
        try {
            const auto logPath = GetLogPath();
            std::filesystem::create_directories(logPath.parent_path());

            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));

            spdlog::set_default_logger(std::move(logger));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::info);

            spdlog::info("{}: logger initialized", kPluginName);
            spdlog::info("{}: log path = {}", kPluginName, logPath.string());
        } catch (const std::exception&) {
            RE::DebugNotification("TMS_ONPCFurnitureGuard: logger init failed");
        }
    }

    std::string FormIDString(RE::TESForm* a_form)
    {
        if (!a_form) {
            return "None";
        }

        return fmt::format("{:08X}", a_form->GetFormID());
    }

    RE::AIProcess* GetActorProcess(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return nullptr;
        }

#ifdef ENABLE_SKYRIM_AE
        return a_actor->GetActorRuntimeData().currentProcess;
#else
        return a_actor->currentProcess;
#endif
    }

    RE::NiPointer<RE::TESObjectREFR> GetOccupiedFurniturePtr(RE::Actor* a_actor)
    {
        auto* process = GetActorProcess(a_actor);
        if (!process) {
            return nullptr;
        }

        const auto furnitureHandle = process->GetOccupiedFurniture();
        return furnitureHandle.get();
    }

    bool SameRef(RE::TESObjectREFR* a_lhs, RE::TESObjectREFR* a_rhs)
    {
        if (!a_lhs || !a_rhs) {
            return false;
        }

        return a_lhs->GetFormID() == a_rhs->GetFormID();
    }

    bool IsIgnoredActor(
        RE::Actor* a_actor,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC)
    {
        if (!a_actor) {
            return true;
        }

        if (a_ignoreA && a_actor == a_ignoreA) {
            return true;
        }

        if (a_ignoreB && a_actor == a_ignoreB) {
            return true;
        }

        if (a_ignoreC && a_actor == a_ignoreC) {
            return true;
        }

        return false;
    }

    float DistanceSquared(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
    {
        const float dx = a_lhs.x - a_rhs.x;
        const float dy = a_lhs.y - a_rhs.y;
        const float dz = a_lhs.z - a_rhs.z;

        return dx * dx + dy * dy + dz * dz;
    }

    bool IsActorInvalidForFurnitureScan(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return true;
        }

        if (a_actor->IsDisabled() || a_actor->IsDead()) {
            return true;
        }

        return false;
    }

    struct FurnitureScanResult
    {
        bool occupied = false;
        bool blocked = false;
        RE::Actor* firstOccupant = nullptr;
        RE::Actor* firstBlocker = nullptr;
        std::int32_t occupiedCount = 0;
        std::int32_t blockedCount = 0;
    };

    bool TryScanActorForFurniture(
        RE::Actor* a_actor,
        RE::TESObjectREFR* a_furniture,
        const RE::NiPoint3& a_furniturePos,
        float a_scanRadiusSq,
        float a_blockDistanceSq,
        bool a_checkNearbyBlocker,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC,
        FurnitureScanResult& a_result)
    {
        if (!a_actor || !a_furniture) {
            return false;
        }

        if (IsIgnoredActor(a_actor, a_ignoreA, a_ignoreB, a_ignoreC)) {
            return false;
        }

        if (IsActorInvalidForFurnitureScan(a_actor)) {
            return false;
        }

        const float distSq = DistanceSquared(a_actor->GetPosition(), a_furniturePos);
        if (distSq > a_scanRadiusSq) {
            return false;
        }

        bool matched = false;

        auto occupiedFurniturePtr = GetOccupiedFurniturePtr(a_actor);
        auto* occupiedFurniture = occupiedFurniturePtr.get();

        if (SameRef(occupiedFurniture, a_furniture)) {
            a_result.occupied = true;
            a_result.occupiedCount += 1;

            if (!a_result.firstOccupant) {
                a_result.firstOccupant = a_actor;
            }

            matched = true;

            spdlog::info(
                "{}: native occupied furniture={} occupant={} name={} occupiedCount={} distSq={}",
                kPluginName,
                FormIDString(a_furniture),
                FormIDString(a_actor),
                a_actor->GetName(),
                a_result.occupiedCount,
                distSq);
        }

        if (a_checkNearbyBlocker && distSq <= a_blockDistanceSq) {
            a_result.blocked = true;
            a_result.blockedCount += 1;

            if (!a_result.firstBlocker) {
                a_result.firstBlocker = a_actor;
            }

            matched = true;

            spdlog::info(
                "{}: nearby blocker furniture={} blocker={} name={} blockedCount={} distSq={}",
                kPluginName,
                FormIDString(a_furniture),
                FormIDString(a_actor),
                a_actor->GetName(),
                a_result.blockedCount,
                distSq);
        }

        return matched;
    }

    FurnitureScanResult ScanFurniture(
        RE::TESObjectREFR* a_furniture,
        float a_scanRadius,
        float a_blockDistance,
        bool a_checkNearbyBlocker,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC,
        bool a_stopAtFirst)
    {
        FurnitureScanResult result;

        if (!a_furniture) {
            spdlog::warn("{}: ScanFurniture furniture is null", kPluginName);
            return result;
        }

        if (a_scanRadius <= 0.0f) {
            a_scanRadius = 4096.0f;
        }

        if (a_blockDistance <= 0.0f) {
            a_blockDistance = 192.0f;
        }

        const float scanRadiusSq = a_scanRadius * a_scanRadius;
        const float blockDistanceSq = a_blockDistance * a_blockDistance;
        const RE::NiPoint3 furniturePos = a_furniture->GetPosition();

        if (kVerboseScanLog) {
            spdlog::info(
                "{}: ScanFurniture begin furniture={} scanRadius={} blockDistance={} checkNearbyBlocker={} ignoreA={} ignoreB={} ignoreC={}",
                kPluginName,
                FormIDString(a_furniture),
                a_scanRadius,
                a_blockDistance,
                a_checkNearbyBlocker ? "true" : "false",
                FormIDString(a_ignoreA),
                FormIDString(a_ignoreB),
                FormIDString(a_ignoreC));
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            const bool matched = TryScanActorForFurniture(
                player,
                a_furniture,
                furniturePos,
                scanRadiusSq,
                blockDistanceSq,
                a_checkNearbyBlocker,
                a_ignoreA,
                a_ignoreB,
                a_ignoreC,
                result);

            if (matched && a_stopAtFirst) {
                if (kVerboseScanLog) {
                    spdlog::info(
                        "{}: ScanFurniture stop at player furniture={} occupied={} blocked={} occupant={} blocker={}",
                        kPluginName,
                        FormIDString(a_furniture),
                        result.occupied ? "true" : "false",
                        result.blocked ? "true" : "false",
                        FormIDString(result.firstOccupant),
                        FormIDString(result.firstBlocker));
                }

                return result;
            }
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            spdlog::warn("{}: ScanFurniture ProcessLists is null", kPluginName);
            return result;
        }

        processLists->ForEachHighActor([&](RE::Actor& a_actorRef) {
            RE::Actor* actor = std::addressof(a_actorRef);

            if (!actor) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (player && actor == player) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            const bool matched = TryScanActorForFurniture(
                actor,
                a_furniture,
                furniturePos,
                scanRadiusSq,
                blockDistanceSq,
                a_checkNearbyBlocker,
                a_ignoreA,
                a_ignoreB,
                a_ignoreC,
                result);

            if (matched && a_stopAtFirst) {
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (kVerboseScanLog) {
            spdlog::info(
                "{}: ScanFurniture done furniture={} occupied={} blocked={} occupiedCount={} blockedCount={} firstOccupant={} firstBlocker={}",
                kPluginName,
                FormIDString(a_furniture),
                result.occupied ? "true" : "false",
                result.blocked ? "true" : "false",
                result.occupiedCount,
                result.blockedCount,
                FormIDString(result.firstOccupant),
                FormIDString(result.firstBlocker));
        }

        return result;
    }
}

namespace Papyrus
{
    bool IsFurnitureActuallyOccupied(
        RE::StaticFunctionTag*,
        RE::TESObjectREFR* a_furniture,
        float a_radius,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC)
    {
        auto result = ScanFurniture(
            a_furniture,
            a_radius,
            0.0f,
            false,
            a_ignoreA,
            a_ignoreB,
            a_ignoreC,
            true);

        return result.occupied;
    }

    bool IsFurnitureBlockedOrOccupied(
        RE::StaticFunctionTag*,
        RE::TESObjectREFR* a_furniture,
        float a_radius,
        float a_blockDistance,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC)
    {
        auto result = ScanFurniture(
            a_furniture,
            a_radius,
            a_blockDistance,
            true,
            a_ignoreA,
            a_ignoreB,
            a_ignoreC,
            true);

        return result.occupied || result.blocked;
    }

    RE::Actor* GetFurnitureOccupant(
        RE::StaticFunctionTag*,
        RE::TESObjectREFR* a_furniture,
        float a_radius,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC)
    {
        auto result = ScanFurniture(
            a_furniture,
            a_radius,
            0.0f,
            false,
            a_ignoreA,
            a_ignoreB,
            a_ignoreC,
            true);

        return result.firstOccupant;
    }

    std::int32_t CountFurnitureOccupants(
        RE::StaticFunctionTag*,
        RE::TESObjectREFR* a_furniture,
        float a_radius,
        RE::Actor* a_ignoreA,
        RE::Actor* a_ignoreB,
        RE::Actor* a_ignoreC)
    {
        auto result = ScanFurniture(
            a_furniture,
            a_radius,
            0.0f,
            false,
            a_ignoreA,
            a_ignoreB,
            a_ignoreC,
            false);

        return result.occupiedCount;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm) {
            spdlog::error("{}: Papyrus VM is null", kPluginName);
            return false;
        }

        a_vm->RegisterFunction(
            "IsFurnitureActuallyOccupied",
            "TMS_ONPCFurnitureGuardNative",
            IsFurnitureActuallyOccupied);

        a_vm->RegisterFunction(
            "IsFurnitureBlockedOrOccupied",
            "TMS_ONPCFurnitureGuardNative",
            IsFurnitureBlockedOrOccupied);

        a_vm->RegisterFunction(
            "GetFurnitureOccupant",
            "TMS_ONPCFurnitureGuardNative",
            GetFurnitureOccupant);

        a_vm->RegisterFunction(
            "CountFurnitureOccupants",
            "TMS_ONPCFurnitureGuardNative",
            CountFurnitureOccupants);

        spdlog::info("{}: Papyrus functions registered", kPluginName);
        return true;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitLogger();

    spdlog::info("{}: SKSEPluginLoad begin", kPluginName);

    SKSE::Init(skse);
    spdlog::info("{}: SKSE initialized", kPluginName);

    auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus) {
        spdlog::critical("{}: failed to get Papyrus interface", kPluginName);
        return false;
    }

    const bool registered = papyrus->Register(Papyrus::RegisterFunctions);
    if (!registered) {
        spdlog::critical("{}: failed to register Papyrus callback", kPluginName);
        return false;
    }

    spdlog::info("{}: SKSEPluginLoad success", kPluginName);
    return true;
}
