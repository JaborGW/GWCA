#include "stdafx.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Macros.h>
#include <GWCA/Utilities/Scanner.h>
#include <GWCA/Utilities/Hooker.h>

#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Map.h>
#include <GWCA/GameEntities/Pathing.h>

#include <GWCA/Context/Cinematic.h>
#include <GWCA/Context/MapContext.h>
#include <GWCA/Context/CharContext.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/AgentContext.h>
#include <GWCA/Context/WorldContext.h>

#include <GWCA/Managers/Module.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Utilities/MemoryPatcher.h>


namespace {
    using namespace GW;

    GW::Constants::ServerRegion* region_id_addr = 0;
    AreaInfo* area_info_addr = 0;

    typedef int(__cdecl* QueryAltitude_pt)(
        const GamePos* point,
        float radius,
        float* alt,
        Vec3f* unk);
    QueryAltitude_pt QueryAltitude_Func;
    GW::MemoryPatcher bypass_tolerance_patch;

    typedef void(__cdecl* DoAction_pt)(uint32_t identifier);
    DoAction_pt EnterChallengeMission_Func = 0;
    DoAction_pt EnterChallengeMission_Ret = 0;
    HookEntry EnterChallengeMission_Entry;

    WorldMapContext* world_map_context = nullptr;

    UI::UIInteractionCallback WorldMap_UICallback_Func = nullptr, WorldMap_UICallback_Ret = nullptr;

    void OnWorldMap_UICallback(UI::InteractionMessage* message, void* wParam, void* lParam) {
        GW::Hook::EnterHook();
        WorldMap_UICallback_Ret(message, wParam, lParam);
        if(message && message->wParam)
            world_map_context = *(WorldMapContext**)message->wParam;
        if (message && message->message_id == GW::UI::UIMessage::kDestroyFrame)
            world_map_context = nullptr;
            
        GW::Hook::LeaveHook();
    }

    MissionMapContext* mission_map_context = nullptr;
    UI::UIInteractionCallback MissionMap_UICallback_Func = nullptr, MissionMap_UICallback_Ret = nullptr;

    void OnMissionMap_UICallback(UI::InteractionMessage* message, void* wParam, void* lParam) {
        GW::Hook::EnterHook();
        MissionMap_UICallback_Ret(message, wParam, lParam);
        if (message && message->wParam)
            mission_map_context = *(MissionMapContext**)message->wParam;
        if (message && message->message_id == GW::UI::UIMessage::kDestroyFrame)
            mission_map_context = nullptr;

        GW::Hook::LeaveHook();
    }

    void OnEnterChallengeMission_Hook(uint32_t identifier) {
        GW::UI::SendUIMessage(UI::UIMessage::kSendEnterMission, (void*)identifier);
    }
    void OnEnterChallengeMission_UIMessage(GW::HookStatus* status, UI::UIMessage, void* wparam, void*) {
        if (!status->blocked && EnterChallengeMission_Ret)
            EnterChallengeMission_Ret((uint32_t)wparam);
    }

    typedef void(__cdecl* Void_pt)();
    Void_pt SkipCinematic_Func = 0;
    Void_pt CancelEnterChallengeMission_Func = 0;



    enum class EnterMissionArena : uint32_t {

        DAlessioArena = 0x13E,
        AmnoonArena = 0x13F,
        FortKoga = 0x140,
        HeroesCrypt = 0x141,
        ShiverpeakArena = 0x142,

        CurrentMap = 0x36d
    };
    enum class EnterMissionFoe : uint32_t {
        None = 0x0,
        IllusionaryWeaponry = 0x32,
        IWillAvengeYouWarriors = 0x33,
        ObsidianSpikeElementalists = 0x34,
        DegenerationTeam = 0x37,
        SmitingMonks = 0x39,
        VictoryIsMineTrappers = 0x3c
    };

    struct MapDimensions {
        uint32_t unk;
        uint32_t start_x;
        uint32_t start_y;
        uint32_t end_x;
        uint32_t end_y;
        uint32_t unk1;
    };

    MapTypeInstanceInfo* map_type_instance_infos = 0;
    uint32_t map_type_instance_infos_size = 0;

    struct InstanceInfo {
        MapDimensions* terrain_info1;
        GW::Constants::InstanceType instance_type;
        AreaInfo* current_map_info;
        uint32_t terrain_count;
        MapDimensions* terrain_info2;
    } *InstanceInfoPtr = 0;

    void Init() {

        DWORD address = 0;
        SkipCinematic_Func = (Void_pt)Scanner::Find("\x8b\x40\x30\x83\x78\x04\x00", "xxxxxxx", -0x5);

        address = GW::Scanner::Find("\x6a\x54\x8d\x46\x24\x89\x08", "xxxxxxx", -0x4);
        if(address && Scanner::IsValidPtr(*(uintptr_t*)(address)))
            region_id_addr = *(GW::Constants::ServerRegion**)(address);

        address = Scanner::Find("\x6B\xC6\x7C\x5E\x05", "xxxxx", 5);
        if (address && Scanner::IsValidPtr(*(uintptr_t*)address, ScannerSection::Section_RDATA))
            area_info_addr = *(AreaInfo**)(address);

        address = Scanner::Find("\x6A\x2C\x50\xE8\x00\x00\x00\x00\x83\xC4\x08\xC7", "xxxx????xxxx", +0xd);
        if (address && Scanner::IsValidPtr(*(uintptr_t*)(address)))
            InstanceInfoPtr = *(InstanceInfo**)(address);

        address = Scanner::Find("\xd9\x06\x8d\x45\xc8\x83\xc4\x10", "xxxxxxxx", -0x5);
        QueryAltitude_Func = (QueryAltitude_pt)Scanner::FunctionFromNearCall(address);

        address = Scanner::Find("\x74\x1d\x68\xa9\x01\x00\x00", "xxxxxxx");
        if (address) {
            // Theres a tolerance check in the QueryAltitude call stack, basically throws an assertion if the altitude found is less than 0.0f
            bypass_tolerance_patch.SetPatch(address, "\xeb", 1);
        }

        address = Scanner::Find("\xa9\x00\x00\x10\x00\x74\x3a", "xxxxxxx");
        CancelEnterChallengeMission_Func = (Void_pt)Scanner::FunctionFromNearCall(address + 0x19);
        EnterChallengeMission_Func = (DoAction_pt)Scanner::FunctionFromNearCall(address + 0x51);


        address = Scanner::Find("\x83\xc0\x0c\x41\x3d\x68\x01\x00\x00", "xxxxxxxxx");
        if (address) {
            map_type_instance_infos = *(MapTypeInstanceInfo**)(address + 0x19);
            map_type_instance_infos_size = (*(uint32_t*)(address + 5)) / sizeof(MapTypeInstanceInfo);
        }

        WorldMap_UICallback_Func = (UI::UIInteractionCallback)GW::Scanner::ToFunctionStart(GW::Scanner::Find("\x83\xe8\x04\x83\xf8\x50", "xxxxxx"));


        MissionMap_UICallback_Func = (UI::UIInteractionCallback)GW::Scanner::ToFunctionStart(GW::Scanner::Find("\x08\x3d\x00\x00\x00\x10", "xxxxxx"));


        GWCA_INFO("[SCAN] Minimap_UICallback_Func = %p", WorldMap_UICallback_Func);
        GWCA_INFO("[SCAN] WorldMap_UICallback_Func = %p", WorldMap_UICallback_Func);
        GWCA_INFO("[SCAN] map_type_instance_infos address = %p, size = %d", map_type_instance_infos, map_type_instance_infos_size);
        GWCA_INFO("[SCAN] RegionId address = %p", region_id_addr);
        GWCA_INFO("[SCAN] AreaInfo address = %p", area_info_addr);
        GWCA_INFO("[SCAN] InstanceInfoPtr address = %p", InstanceInfoPtr);
        GWCA_INFO("[SCAN] QueryAltitude Function = %p", QueryAltitude_Func);
        GWCA_INFO("[SCAN] EnterChallengeMission_Func = %p", EnterChallengeMission_Func);
        GWCA_INFO("[SCAN] CancelEnterChallengeMission_Func = %p", CancelEnterChallengeMission_Func);
#ifdef _DEBUG
        GWCA_ASSERT(MissionMap_UICallback_Func);
        GWCA_ASSERT(WorldMap_UICallback_Func);
        GWCA_ASSERT(map_type_instance_infos);
        GWCA_ASSERT(region_id_addr);
        GWCA_ASSERT(area_info_addr);
        GWCA_ASSERT(InstanceInfoPtr);
        GWCA_ASSERT(QueryAltitude_Func);
        GWCA_ASSERT(bypass_tolerance_patch.IsValid());
        GWCA_ASSERT(EnterChallengeMission_Func);
        GWCA_ASSERT(CancelEnterChallengeMission_Func);
#endif
        if (WorldMap_UICallback_Func)
            GW::HookBase::CreateHook((void**)&WorldMap_UICallback_Func, OnWorldMap_UICallback, (void**)&WorldMap_UICallback_Ret);
        if (MissionMap_UICallback_Func)
            GW::HookBase::CreateHook((void**)&MissionMap_UICallback_Func, OnMissionMap_UICallback, (void**)&MissionMap_UICallback_Ret);
        if (EnterChallengeMission_Func) {
            GW::HookBase::CreateHook((void**)&EnterChallengeMission_Func, OnEnterChallengeMission_Hook, (void**)&EnterChallengeMission_Ret);
            UI::RegisterUIMessageCallback(&EnterChallengeMission_Entry, UI::UIMessage::kSendEnterMission, OnEnterChallengeMission_UIMessage, 0x1);
        }
    }
    void EnableHooks() {
        if (EnterChallengeMission_Func)
            HookBase::EnableHooks(EnterChallengeMission_Func);
        if(WorldMap_UICallback_Func)
            HookBase::EnableHooks(WorldMap_UICallback_Func);
        if (MissionMap_UICallback_Func)
            HookBase::EnableHooks(MissionMap_UICallback_Func);
        if(bypass_tolerance_patch.IsValid())
            bypass_tolerance_patch.TogglePatch(true);
    }
    void DisableHooks() {
        if (EnterChallengeMission_Func)
            HookBase::DisableHooks(EnterChallengeMission_Func);
        if (WorldMap_UICallback_Func)
            HookBase::DisableHooks(WorldMap_UICallback_Func);
        if (MissionMap_UICallback_Func)
            HookBase::DisableHooks(MissionMap_UICallback_Func);
        if (bypass_tolerance_patch.IsValid())
            bypass_tolerance_patch.TogglePatch(false);

    }
    void Exit() {
        HookBase::RemoveHook(EnterChallengeMission_Func);
        HookBase::RemoveHook(WorldMap_UICallback_Func);
        bypass_tolerance_patch.Reset();
    }
}

namespace GW {

    Module MapModule {
        "MapModule",    // name
        NULL,           // param
        ::Init,         // init_module
        ::Exit,           // exit_module
        ::EnableHooks,           // enable_hooks
        ::DisableHooks,           // disable_hooks
    };
    namespace Map {

        MissionMapContext* GetMissionMapContext() {
            return mission_map_context;
        }

        WorldMapContext* GetWorldMapContext() {
            return world_map_context;
        }

        int QueryAltitude(const GamePos& pos, float radius, float& alt, Vec3f* terrain_normal) {
            if (QueryAltitude_Func)
                return QueryAltitude_Func(&pos, radius, &alt, terrain_normal);
            return 0;
        }

        bool GetIsMapLoaded() {
            auto* g = GetGameContext();
            return g && g->map != nullptr;
        }

        bool Travel(Constants::MapID map_id, Constants::ServerRegion region, int district_number, Constants::Language language) {
            struct MapStruct {
                GW::Constants::MapID map_id;
                Constants::ServerRegion region;
                Constants::Language language;
                int district_number;
            };
            MapStruct t;
            t.map_id = map_id;
            t.district_number = district_number;
            t.region = region;
            t.language = language;
            return UI::SendUIMessage(UI::UIMessage::kTravel, &t);
        }
        GW::Constants::ServerRegion RegionFromDistrict(const GW::Constants::District _district)
        {
            switch (_district) {
            case GW::Constants::District::International:
                return GW::Constants::ServerRegion::International;
            case GW::Constants::District::American:
                return GW::Constants::ServerRegion::America;
            case GW::Constants::District::EuropeEnglish:
            case GW::Constants::District::EuropeFrench:
            case GW::Constants::District::EuropeGerman:
            case GW::Constants::District::EuropeItalian:
            case GW::Constants::District::EuropeSpanish:
            case GW::Constants::District::EuropePolish:
            case GW::Constants::District::EuropeRussian:
                return GW::Constants::ServerRegion::Europe;
            case GW::Constants::District::AsiaKorean:
                return GW::Constants::ServerRegion::Korea;
            case GW::Constants::District::AsiaChinese:
                return GW::Constants::ServerRegion::China;
            case GW::Constants::District::AsiaJapanese:
                return GW::Constants::ServerRegion::Japan;
            default:
                break;
            }
            return GW::Map::GetRegion();
        }

        GW::Constants::Language LanguageFromDistrict(const GW::Constants::District _district)
        {
            switch (_district) {
            case GW::Constants::District::EuropeFrench:
                return GW::Constants::Language::French;
            case GW::Constants::District::EuropeGerman:
                return GW::Constants::Language::German;
            case GW::Constants::District::EuropeItalian:
                return GW::Constants::Language::Italian;
            case GW::Constants::District::EuropeSpanish:
                return GW::Constants::Language::Spanish;
            case GW::Constants::District::EuropePolish:
                return GW::Constants::Language::Polish;
            case GW::Constants::District::EuropeRussian:
                return GW::Constants::Language::Russian;
            case GW::Constants::District::EuropeEnglish:
            case GW::Constants::District::AsiaKorean:
            case GW::Constants::District::AsiaChinese:
            case GW::Constants::District::AsiaJapanese:
            case GW::Constants::District::International:
            case GW::Constants::District::American:
                return GW::Constants::Language::English;
            default:
                break;
            }
            return GetLanguage();
        }

        bool Travel(Constants::MapID map_id, Constants::District district, int district_number) {
            return Travel(map_id, RegionFromDistrict(district), district_number, LanguageFromDistrict(district));
        }

        uint32_t GetInstanceTime() {
            auto* a = GetAgentContext();
            return a ? a->instance_timer : 0;
        }

        Constants::MapID GetMapID() {
            auto* c = GetCharContext();
            return c ? c->current_map_id : Constants::MapID::None;
        }

        GW::Constants::ServerRegion GetRegion() {
            return region_id_addr ? *region_id_addr : GW::Constants::ServerRegion::Unknown;
        }

        bool GetIsMapUnlocked(Constants::MapID map_id) {
            auto* w = GetWorldContext();
            Array<uint32_t>* unlocked_map = w && w->unlocked_map.valid() ? &w->unlocked_map : nullptr;
            if (!unlocked_map)
                return false;
            uint32_t real_index = (uint32_t)map_id / 32;
            if (real_index >= unlocked_map->size())
                return false;
            uint32_t shift = (uint32_t)map_id % 32;
            uint32_t flag = 1u << shift;
            return (unlocked_map->at(real_index) & flag) != 0;
        }

        GW::Constants::Language GetLanguage() {
            auto* c = GetCharContext();
            return c ? c->language : GW::Constants::Language::English;
        }

        bool GetIsObserving() {
            auto* c = GetCharContext();
            return c ? c->current_map_id != c->observe_map_id : false;
        }

        int GetDistrict() {
            auto* c = GetCharContext();
            return c ? (int)c->district_number : 0;
        }

        Constants::InstanceType GetInstanceType() {
            return InstanceInfoPtr ? InstanceInfoPtr->instance_type : Constants::InstanceType::Loading;
        }

        MissionMapIconArray* GetMissionMapIconArray() {
            auto* w = GetWorldContext();
            return w && w->mission_map_icons.valid() ? &w->mission_map_icons : nullptr;
        }

        PathingMapArray* GetPathingMap() {
            const auto m = GetMapContext();
            if (!(m && m->sub1 && m->sub1->sub2))
                return nullptr;
            return &m->sub1->sub2->pmaps;
        }

        uint32_t GetFoesKilled() {
            auto* w = GetWorldContext();
            return w ? w->foes_killed : 0;
        }

        uint32_t GetFoesToKill() {
            auto* w = GetWorldContext();
            return w ? w->foes_to_kill : 0;
        }

        AreaInfo* GetMapInfo(Constants::MapID map_id) {
            if (map_id == Constants::MapID::None) {
                map_id = GetMapID();
            }
            return area_info_addr && map_id > Constants::MapID::None && map_id < Constants::MapID::Count ? &area_info_addr[(uint32_t)map_id] : nullptr;
        }

        bool GetIsInCinematic() {
            auto* g = GetGameContext();
            return g && g->cinematic ? g->cinematic->h0004 != 0 : false;
        }

        bool SkipCinematic() {
            return SkipCinematic_Func ? SkipCinematic_Func(), true : false;
        }

        bool EnterChallenge() {
            return UI::SendUIMessage(UI::UIMessage::kSendEnterMission, (void*)Constants::MapID::Count);
        }

        bool CancelEnterChallenge() {
            return CancelEnterChallengeMission_Func ? CancelEnterChallengeMission_Func(), true : false;
        }
        MapTypeInstanceInfo* GetMapTypeInstanceInfo(RegionType map_region_type) {
            bool is_outpost = !(map_region_type == RegionType::ExplorableZone
                || map_region_type == RegionType::MissionArea
                || map_region_type == RegionType::Dungeon);
            for (size_t i = 0; i < map_type_instance_infos_size; i++) {
                if (map_type_instance_infos[i].map_region_type == map_region_type
                    && map_type_instance_infos[i].is_outpost == is_outpost) {
                    return &map_type_instance_infos[i];
                }
            }
            return nullptr;
        }

    }
} // namespace GW
