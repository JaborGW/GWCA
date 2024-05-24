#include "stdafx.h"

#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/AgentContext.h>
#include <GWCA/Context/GadgetContext.h>
#include <GWCA/Context/WorldContext.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Item.h>

#include <GWCA/Managers/Module.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Scanner.h>

namespace {
    using namespace GW;

    uint32_t dialog_agent_id = 0;

    typedef void (*SendDialog_pt)(uint32_t dialog_id);
    SendDialog_pt SendAgentDialog_Func = 0, SendAgentDialog_Ret = 0;
    SendDialog_pt SendGadgetDialog_Func = 0, SendGadgetDialog_Ret = 0;

    void OnSendAgentDialog_Func(uint32_t dialog_id) {
        GW::Hook::EnterHook();
        UI::SendUIMessage(UI::UIMessage::kSendAgentDialog, (void*)dialog_id);
        GW::Hook::LeaveHook();
    };
    void OnSendGadgetDialog_Func(uint32_t dialog_id) {
        GW::Hook::EnterHook();
        UI::SendUIMessage(UI::UIMessage::kSendGadgetDialog, (void*)dialog_id);
        GW::Hook::LeaveHook();
    };

    typedef void(*ChangeTarget_pt)(uint32_t agent_id, uint32_t auto_target_id);
    ChangeTarget_pt ChangeTarget_Func = 0, ChangeTarget_Ret = 0;

    void OnChangeTarget_Func(uint32_t agent_id, uint32_t auto_target_id) {
        GW::Hook::EnterHook();
        UI::UIPacket::kSendChangeTarget packet = { agent_id, auto_target_id };
        UI::SendUIMessage(UI::UIMessage::kSendChangeTarget, &packet);
        GW::Hook::LeaveHook();
    };

    enum class InteractionActionType : uint32_t {
        Enemy,
        Player,
        NPC,
        Item,
        Follow,
        Gadget
    };

    typedef void(*InteractPlayer_pt)(uint32_t agent_id);
    InteractPlayer_pt InteractPlayer_Func = 0, InteractPlayer_Ret = 0;

    void OnInteractPlayer_Func(uint32_t agent_id) {
        GW::Hook::EnterHook();
        UI::SendUIMessage(GW::UI::UIMessage::kSendInteractPlayer, (void*)agent_id);
        GW::Hook::LeaveHook();
    }

    typedef void(*InteractCallableAgent_pt)(uint32_t agent_id, bool call_target);
    InteractCallableAgent_pt InteractNPC_Func = 0, InteractNPC_Ret = 0;
    InteractCallableAgent_pt InteractItem_Func = 0, InteractItem_Ret = 0;
    InteractCallableAgent_pt InteractGadget_Func = 0, InteractGadget_Ret = 0;
    InteractCallableAgent_pt InteractEnemy_Func = 0, InteractEnemy_Ret = 0;

    void OnInteractNPC_Func(uint32_t agent_id, bool call_target) {
        GW::Hook::EnterHook();
        auto packet = UI::UIPacket::kInteractAgent{ agent_id, call_target };
        UI::SendUIMessage(GW::UI::UIMessage::kSendInteractNPC, &packet);
        GW::Hook::LeaveHook();
    }
    void OnInteractItem_Func(uint32_t agent_id, bool call_target) {
        GW::Hook::EnterHook();
        auto packet = UI::UIPacket::kInteractAgent{ agent_id, call_target };
        UI::SendUIMessage(GW::UI::UIMessage::kSendInteractItem, &packet);
        GW::Hook::LeaveHook();
    }
    void OnInteractGadget_Func(uint32_t agent_id, bool call_target) {
        GW::Hook::EnterHook();
        auto packet = UI::UIPacket::kInteractAgent{ agent_id, call_target };
        UI::SendUIMessage(GW::UI::UIMessage::kSendInteractGadget, &packet);
        GW::Hook::LeaveHook();
    }
    void OnInteractEnemy_Func(uint32_t agent_id, bool call_target) {
        GW::Hook::EnterHook();
        auto packet = UI::UIPacket::kInteractAgent{ agent_id, call_target };
        UI::SendUIMessage(GW::UI::UIMessage::kSendInteractEnemy, &packet);
        GW::Hook::LeaveHook();
    }

    typedef void(*CallTarget_pt)(uint32_t type, uint32_t agent_id);
    CallTarget_pt CallTarget_Func = 0, CallTarget_Ret = 0;

    void OnCallTarget_Func(uint32_t type, uint32_t agent_id) {
        GW::Hook::EnterHook();
        UI::UIPacket::kSendCallTarget packet = { (CallTargetType)type, agent_id };
        UI::SendUIMessage(GW::UI::UIMessage::kSendCallTarget, &packet);
        GW::Hook::LeaveHook();
    }

    typedef void(*MoveToWorldPoint_pt)(GamePos *pos);
    MoveToWorldPoint_pt MoveToWorldPoint_Func = 0, MoveToWorldPoint_Ret = 0;

    void OnMoveToWorldPoint_Func(GamePos* pos) {
        GW::Hook::EnterHook();
        UI::SendUIMessage(GW::UI::UIMessage::kSendMoveToWorldPoint, pos);
        GW::Hook::LeaveHook();
    }

    uintptr_t AgentArrayPtr = 0;
    uintptr_t PlayerAgentIdPtr = 0;
    uintptr_t TargetAgentIdPtr = 0;
    uintptr_t MouseOverAgentIdPtr = 0;
    uintptr_t IsAutoRunningPtr = 0;

    AgentList *AgentListPtr = nullptr;

    HookEntry UIMessage_Entry;
    constexpr std::array ui_messages_to_hook = {
        UI::UIMessage::kDialogBody,
        UI::UIMessage::kSendAgentDialog,
        UI::UIMessage::kSendGadgetDialog,
        UI::UIMessage::kSendMoveToWorldPoint,
        UI::UIMessage::kSendCallTarget,
        UI::UIMessage::kSendInteractGadget,
        UI::UIMessage::kSendInteractItem,
        UI::UIMessage::kSendInteractNPC,
        UI::UIMessage::kSendInteractPlayer,
        UI::UIMessage::kSendInteractEnemy,
        UI::UIMessage::kSendChangeTarget,
        UI::UIMessage::kChangeTarget,

    };
    void OnUIMessage(GW::HookStatus* status, UI::UIMessage message_id, void* wparam, void*) {
        if (status->blocked)
            return;
        switch (message_id) {
        case UI::UIMessage::kDialogBody: {
            const auto packet = static_cast<UI::DialogBodyInfo*>(wparam);
            dialog_agent_id = packet->agent_id;
        } break;
        case UI::UIMessage::kSendAgentDialog: {
            if (SendAgentDialog_Ret) {
                SendAgentDialog_Ret((uint32_t)wparam);
            }
        } break;
        case UI::UIMessage::kSendChangeTarget: {
            if (ChangeTarget_Ret) {
                const auto packet = static_cast<UI::UIPacket::kSendChangeTarget*>(wparam);
                ChangeTarget_Ret(packet->target_id, packet->auto_target_id);
            }
        } break;
        case UI::UIMessage::kSendGadgetDialog: {
            if (SendGadgetDialog_Ret) {
                SendGadgetDialog_Ret((uint32_t)wparam);
            }
        } break;
        case UI::UIMessage::kSendMoveToWorldPoint: {
            if (MoveToWorldPoint_Ret) {
                MoveToWorldPoint_Ret((GamePos*)wparam);
            }
        } break;
        case UI::UIMessage::kSendCallTarget: {
            if (CallTarget_Ret) {
                if (const auto packet = static_cast<UI::UIPacket::kSendCallTarget*>(wparam))
                    CallTarget_Ret((uint32_t)packet->call_type, packet->agent_id);
            }
        } break;
        case UI::UIMessage::kSendInteractGadget: {
            if (InteractGadget_Ret) {
                if (const auto packet = static_cast<UI::UIPacket::kInteractAgent*>(wparam))
                    InteractGadget_Ret(packet->agent_id, packet->call_target);
            }
        } break;
        case UI::UIMessage::kSendInteractItem: {
            if (InteractItem_Ret) {
                if (const auto packet = static_cast<UI::UIPacket::kInteractAgent*>(wparam))
                InteractItem_Ret(packet->agent_id, packet->call_target);
            }
        } break;
        case UI::UIMessage::kSendInteractNPC: {
            if (InteractNPC_Ret) {
                if (const auto packet = static_cast<UI::UIPacket::kInteractAgent*>(wparam))
                    InteractNPC_Ret(packet->agent_id, packet->call_target);
            }
        } break;
        case UI::UIMessage::kSendInteractEnemy: {
            if (InteractEnemy_Ret) {
                if (const auto packet = static_cast<UI::UIPacket::kInteractAgent*>(wparam))
                    InteractEnemy_Ret(packet->agent_id, packet->call_target);
            }
        } break;
        case UI::UIMessage::kSendInteractPlayer: {
            if (InteractPlayer_Ret) {
                InteractPlayer_Ret((uint32_t)wparam);
            }
        } break;
        }
    }

    void Init() {
        uintptr_t address = 0;

        address = Scanner::Find( "\x3B\xDF\x0F\x95", "xxxx", -0x0089);
        if (address) {
            ChangeTarget_Func = (ChangeTarget_pt)address;

            TargetAgentIdPtr = *(uintptr_t*)(address + 0x94);
            if (!Scanner::IsValidPtr(TargetAgentIdPtr))
                TargetAgentIdPtr = 0;

            MouseOverAgentIdPtr = TargetAgentIdPtr + 0x8;
            if (!Scanner::IsValidPtr(MouseOverAgentIdPtr))
                MouseOverAgentIdPtr = 0;
        }

        address = Scanner::Find("\xFF\x50\x10\x47\x83\xC6\x04\x3B\xFB\x75\xE1", "xxxxxxxxxxx", +0xD);
        if (Scanner::IsValidPtr(*(uintptr_t*)address))
            AgentArrayPtr = *(uintptr_t*)address;

        address = Scanner::Find("\x5D\xE9\x00\x00\x00\x00\x55\x8B\xEC\x53","xx????xxxx", -0xE);
        if (Scanner::IsValidPtr(*(uintptr_t*)address))
            PlayerAgentIdPtr = *(uintptr_t*)address;

        address = Scanner::Find("\x89\x4b\x24\x8b\x4b\x28\x83\xe9\x00", "xxxxxxxxx");
        if (address) {
            SendAgentDialog_Func = (SendDialog_pt)Scanner::FunctionFromNearCall(address + 0x15);
            SendGadgetDialog_Func = (SendDialog_pt)Scanner::FunctionFromNearCall(address + 0x25);
        }


        address = Scanner::Find("\xc7\x45\xf0\x98\x3a\x00\x00", "xxxxxxx", 0x41);
        address = Scanner::FunctionFromNearCall(address); // Interact Agent function
        if (address) {
            InteractEnemy_Func = (InteractCallableAgent_pt)Scanner::FunctionFromNearCall(address + 0x73);
            InteractPlayer_Func = (InteractPlayer_pt)Scanner::FunctionFromNearCall(address + 0xB2);
            MoveToWorldPoint_Func = (MoveToWorldPoint_pt)Scanner::FunctionFromNearCall(address + 0xC7);
            CallTarget_Func = (CallTarget_pt)Scanner::FunctionFromNearCall(address + 0xD6);
            InteractNPC_Func = (InteractCallableAgent_pt)Scanner::FunctionFromNearCall(address + 0xE7);
            InteractItem_Func = (InteractCallableAgent_pt)Scanner::FunctionFromNearCall(address + 0xF8);
            // NB: What is UI message 0x100001a0 ?
            InteractGadget_Func = (InteractCallableAgent_pt)Scanner::FunctionFromNearCall(address + 0x120);
        }
        HookBase::CreateHook((void**)&CallTarget_Func, OnCallTarget_Func, (void**)&CallTarget_Ret);
        HookBase::CreateHook((void**)&InteractNPC_Func, OnInteractNPC_Func, (void**)&InteractNPC_Ret);
        HookBase::CreateHook((void**)&InteractEnemy_Func, OnInteractEnemy_Func, (void**)&InteractEnemy_Ret);
        HookBase::CreateHook((void**)&InteractGadget_Func, OnInteractGadget_Func, (void**)&InteractGadget_Ret);
        HookBase::CreateHook((void**)&InteractItem_Func, OnInteractItem_Func, (void**)&InteractItem_Ret);
        HookBase::CreateHook((void**)&InteractPlayer_Func, OnInteractPlayer_Func, (void**)&InteractPlayer_Ret);
        HookBase::CreateHook((void**)&MoveToWorldPoint_Func, OnMoveToWorldPoint_Func, (void**)&MoveToWorldPoint_Ret);

        HookBase::CreateHook((void**)&SendAgentDialog_Func, OnSendAgentDialog_Func, (void**)&SendAgentDialog_Ret);
        HookBase::CreateHook((void**)&SendGadgetDialog_Func, OnSendGadgetDialog_Func, (void**)&SendGadgetDialog_Ret);
        HookBase::CreateHook((void**)&ChangeTarget_Func, OnChangeTarget_Func, (void**)&ChangeTarget_Ret);

        GWCA_INFO("[SCAN] TargetAgentIdPtr = %p", TargetAgentIdPtr);
        GWCA_INFO("[SCAN] MouseOverAgentIdPtr = %p", MouseOverAgentIdPtr);
        GWCA_INFO("[SCAN] AgentArrayPtr = %p", AgentArrayPtr);
        GWCA_INFO("[SCAN] PlayerAgentIdPtr = %p", PlayerAgentIdPtr);

        GWCA_INFO("[SCAN] ChangeTargetFunction = %p", ChangeTarget_Func);
        GWCA_INFO("[SCAN] SendAgentDialog_Func = %p", SendAgentDialog_Func);
        GWCA_INFO("[SCAN] SendGadgetDialog_Func = %p", SendGadgetDialog_Func);
        GWCA_INFO("[SCAN] InteractEnemy Function = %p", InteractEnemy_Func);
        GWCA_INFO("[SCAN] InteractPlayer Function = %p", InteractPlayer_Func);
        GWCA_INFO("[SCAN] InteractNPC Function = %p", InteractNPC_Func);
        GWCA_INFO("[SCAN] InteractItem Function = %p", InteractItem_Func);
        GWCA_INFO("[SCAN] InteractIGadget Function = %p", InteractGadget_Func);
        GWCA_INFO("[SCAN] MoveToWorldPoint_Func Function = %p", MoveToWorldPoint_Func);
        GWCA_INFO("[SCAN] CallTarget Function = %p", CallTarget_Func);

#ifdef _DEBUG
        GWCA_ASSERT(TargetAgentIdPtr);
        GWCA_ASSERT(MouseOverAgentIdPtr);
        GWCA_ASSERT(AgentArrayPtr);
        GWCA_ASSERT(PlayerAgentIdPtr);

        GWCA_ASSERT(ChangeTarget_Func);
        GWCA_ASSERT(SendAgentDialog_Func);
        GWCA_ASSERT(SendGadgetDialog_Func);
        GWCA_ASSERT(InteractEnemy_Func);
        GWCA_ASSERT(InteractPlayer_Func);
        GWCA_ASSERT(MoveToWorldPoint_Func);
        GWCA_ASSERT(CallTarget_Func);
        GWCA_ASSERT(InteractNPC_Func);
        GWCA_ASSERT(InteractItem_Func);
        GWCA_ASSERT(InteractGadget_Func);
#endif



    }
    void EnableHooks() {
        if (CallTarget_Func)
            HookBase::EnableHooks(CallTarget_Func);
        if (InteractNPC_Func)
            HookBase::EnableHooks(InteractNPC_Func);
        if (InteractEnemy_Func)
            HookBase::EnableHooks(InteractEnemy_Func);
        if (InteractGadget_Func)
            HookBase::EnableHooks(InteractGadget_Func);
        if (InteractPlayer_Func)
            HookBase::EnableHooks(InteractPlayer_Func);
        if (MoveToWorldPoint_Func)
            HookBase::EnableHooks(MoveToWorldPoint_Func);

        if (SendAgentDialog_Func)
            HookBase::EnableHooks(SendAgentDialog_Func);
        if (SendGadgetDialog_Func)
            HookBase::EnableHooks(SendGadgetDialog_Func);

        for (auto ui_message : ui_messages_to_hook) {
            UI::RegisterUIMessageCallback(&UIMessage_Entry, ui_message, OnUIMessage, 0x1);
        }
    }
    void DisableHooks() {
        if (CallTarget_Func)
            HookBase::DisableHooks(CallTarget_Func);
        if (InteractNPC_Func)
            HookBase::DisableHooks(InteractNPC_Func);
        if (InteractEnemy_Func)
            HookBase::DisableHooks(InteractEnemy_Func);
        if (InteractGadget_Func)
            HookBase::DisableHooks(InteractGadget_Func);
        if (InteractPlayer_Func)
            HookBase::DisableHooks(InteractPlayer_Func);
        if (InteractItem_Func)
            HookBase::DisableHooks(InteractItem_Func);
        if (MoveToWorldPoint_Func)
            HookBase::DisableHooks(MoveToWorldPoint_Func);

        if (SendAgentDialog_Func)
            HookBase::DisableHooks(SendAgentDialog_Func);
        if (SendGadgetDialog_Func)
            HookBase::DisableHooks(SendGadgetDialog_Func);

        UI::RemoveUIMessageCallback(&UIMessage_Entry);
    }
    void Exit() {
        if (CallTarget_Func)
            HookBase::RemoveHook(CallTarget_Func);
        if (InteractNPC_Func)
            HookBase::RemoveHook(InteractNPC_Func);
        if (InteractEnemy_Func)
            HookBase::RemoveHook(InteractEnemy_Func);
        if (InteractGadget_Func)
            HookBase::RemoveHook(InteractGadget_Func);
        if (InteractPlayer_Func)
            HookBase::RemoveHook(InteractPlayer_Func);
        if (InteractItem_Func)
            HookBase::RemoveHook(InteractItem_Func);
        if (MoveToWorldPoint_Func)
            HookBase::RemoveHook(MoveToWorldPoint_Func);

        if (SendAgentDialog_Func)
            HookBase::RemoveHook(SendAgentDialog_Func);
        if (SendGadgetDialog_Func)
            HookBase::RemoveHook(SendGadgetDialog_Func);
    }
}

namespace GW {

    Module AgentModule = {
        "AgentModule",      // name
        NULL,               // param
        ::Init,             // init_module
        ::Exit,             // exit_module
        ::EnableHooks,               // enable_hooks
        ::DisableHooks,               // disable_hooks
    };

    namespace Agents {

        bool GetIsAgentTargettable(const GW::Agent* agent) {
            if (!agent) return false;
            if (const auto living = agent->GetAsAgentLiving()) {
                return true;
            }
            else if (const auto gadget = agent->GetAsAgentGadget()) {
                if (GetAgentEncName(gadget))
                    return true;
            }
            return false;
        }
        bool SendDialog(uint32_t dialog_id) {
            const auto a = GW::Agents::GetAgentByID(dialog_agent_id);
            if (!a) return false;
            if (a->GetIsGadgetType()) {
                return UI::SendUIMessage(UI::UIMessage::kSendGadgetDialog, (void*)dialog_id);
            }
            else {
                return UI::SendUIMessage(UI::UIMessage::kSendAgentDialog, (void*)dialog_id);
            }
        }

        AgentArray* GetAgentArray() {
            auto* agents = (AgentArray*)AgentArrayPtr;
            return agents && agents->valid() ? agents : nullptr;
        }
        uint32_t GetControlledCharacterId() {
            const auto w = GetWorldContext();
            return w && w->playerControlledChar ? w->playerControlledChar->agent_id : 0;
        }
        uint32_t GetObservingId() {
            return *(uint32_t*)PlayerAgentIdPtr;
        }
        uint32_t GetTargetId() {
            return *(uint32_t*)TargetAgentIdPtr;
        }
        uint32_t GetMouseoverId() {
            return *(uint32_t*)MouseOverAgentIdPtr;
        }

        bool ChangeTarget(AgentID agent_id) {
            UI::UIPacket::kSendChangeTarget packet = { agent_id, 0 };
            return UI::SendUIMessage(UI::UIMessage::kSendChangeTarget, &packet);
        }

        bool ChangeTarget(const Agent* agent) {
            return agent ? ChangeTarget(agent->agent_id) : false;
        }

        bool Move(float x, float y, uint32_t zplane /*= 0*/) {
            GamePos pos;
            pos.x = x;
            pos.y = y;
            pos.zplane = zplane;
            return Move(pos);
        }

        bool Move(GamePos pos) {
            return UI::SendUIMessage(UI::UIMessage::kSendMoveToWorldPoint, &pos);
        }
        uint32_t GetAmountOfPlayersInInstance() {
            auto* w = GetWorldContext();
            // -1 because the 1st array element is nil
            return w && w->players.valid() ? w->players.size() - 1 : 0;
        }

        MapAgentArray* GetMapAgentArray() {
            auto* w = GetWorldContext();
            return w ? &w->map_agents : nullptr;
        }

        MapAgent* GetMapAgentByID(uint32_t agent_id) {
            auto* agents = agent_id ? GetMapAgentArray() : nullptr;
            return agents && agent_id < agents->size() ? &agents->at(agent_id) : nullptr;
        }

        Agent* GetAgentByID(uint32_t agent_id) {
            auto* agents = agent_id ? GetAgentArray() : nullptr;
            return agents && agent_id < agents->size() ? agents->at(agent_id) : nullptr;
        }

        Agent* GetPlayerByID(uint32_t player_id) {
            return GetAgentByID(PlayerMgr::GetPlayerAgentId(player_id));
        }

        AgentLiving* GetControlledCharacter() {
            const auto a = GetAgentByID(GetControlledCharacterId());
            return a ? a->GetAsAgentLiving() : nullptr;
        }

        bool IsObserving() {
            return !GW::Map::GetIsObserving() && GetControlledCharacterId() != GetObservingId();
        }

        AgentLiving* GetTargetAsAgentLiving()
        {
            Agent* a = GetTarget();
            return a ? a->GetAsAgentLiving() : nullptr;
        }

        bool InteractAgent(const Agent* agent, bool call_target) {
            if (!agent)
                return false;
            UI::UIPacket::kInteractAgent packet = { agent->agent_id, call_target };
            if (agent->GetIsItemType()) {
                return UI::SendUIMessage(UI::UIMessage::kSendInteractItem, &packet);
            }
            if (agent->GetIsGadgetType()) {
                return UI::SendUIMessage(UI::UIMessage::kSendInteractGadget, &packet);
            }
            const auto living = agent->GetAsAgentLiving();
            if (!living)
                return false;
            if (living->IsPlayer()) {
                if (!UI::SendUIMessage(UI::UIMessage::kSendInteractPlayer, (void*)agent->agent_id))
                    return false;
                if (!call_target)
                    return true;
                auto call_packet = UI::UIPacket::kSendCallTarget{ CallTargetType::Following, agent->agent_id };
                return UI::SendUIMessage(UI::UIMessage::kSendCallTarget, &call_packet);
            }
            if (living->allegiance == GW::Constants::Allegiance::Enemy) {
                return UI::SendUIMessage(UI::UIMessage::kSendInteractEnemy, &packet);
            }
            else {
                return UI::SendUIMessage(UI::UIMessage::kSendInteractNPC, &packet);
            }
        }

        wchar_t* GetPlayerNameByLoginNumber(uint32_t login_number) {
            return PlayerMgr::GetPlayerName(login_number);
        }

        uint32_t GetAgentIdByLoginNumber(uint32_t login_number) {
            auto* player = PlayerMgr::GetPlayerByID(login_number);
            return player ? player->agent_id : 0;
        }

        uint32_t GetHeroAgentID(uint32_t hero_index) {
            return PartyMgr::GetHeroAgentID(hero_index);
        }

        PlayerArray* GetPlayerArray() {
            auto* w = GetWorldContext();
            return w && w->players.valid() ? &w->players : nullptr;
        }

        NPCArray* GetNPCArray() {
            auto* w = GetWorldContext();
            return w && w->npcs.valid() ? &w->npcs : nullptr;
        }

        NPC* GetNPCByID(uint32_t npc_id) {
            auto* npcs = GetNPCArray();
            return npcs && npc_id < npcs->size() ? &npcs->at(npc_id) : nullptr;
        }

        wchar_t* GetAgentEncName(uint32_t agent_id) {
            const Agent* agent = GetAgentByID(agent_id);
            if (agent) {
                return GetAgentEncName(agent);
            }
            GW::AgentInfoArray& agent_infos = GetWorldContext()->agent_infos;
            if (!agent_infos.valid() || agent_id >= agent_infos.size()) {
                return nullptr;
            }
            return agent_infos[agent_id].name_enc;
        }

        wchar_t* GetAgentEncName(const Agent* agent) {
            if (!agent)
                return nullptr;
            if (agent->GetIsLivingType()) {
                const AgentLiving* ag = agent->GetAsAgentLiving();
                if (ag->login_number) {
                    PlayerArray* players = GetPlayerArray();
                    if (!players)
                        return nullptr;
                    Player* player = &players->at(ag->login_number);
                    if (player)
                        return player->name_enc;
                }
                // @Remark:
                // For living npcs it's not elegant, but the game does it as well. See arround GetLivingName(AgentID id)@007C2A00.
                // It first look in the AgentInfo arrays, if it doesn't find it, it does a bunch a shit and fallback on NPCArray.
                // If we only use NPCArray, we have a problem because 2 agents can share the same PlayerNumber.
                // In Isle of Nameless, few npcs (Zaischen Weapond Collector) share the PlayerNumber with "The Guide" so using NPCArray only won't work.
                // But, the dummies (Suit of xx Armor) don't have there NameString in AgentInfo array, so we need NPCArray.
                Array<AgentInfo>& agent_infos = GetWorldContext()->agent_infos;
                if (ag->agent_id >= agent_infos.size()) return nullptr;
                if (agent_infos[ag->agent_id].name_enc)
                    return agent_infos[ag->agent_id].name_enc;
                NPC* npc = GetNPCByID(ag->player_number);
                return npc ? npc->name_enc : nullptr;
            }
            if (agent->GetIsGadgetType()) {
                AgentContext* ctx = GetAgentContext();
                GadgetContext* gadget = GetGameContext()->gadget;
                if (!ctx || !gadget) return nullptr;
                auto* GadgetIds = ctx->agent_summary_info[agent->agent_id].extra_info_sub;
                if (!GadgetIds)
                    return nullptr;
                if (GadgetIds->gadget_name_enc)
                    return GadgetIds->gadget_name_enc;
                size_t id = GadgetIds->gadget_id;
                if (gadget->GadgetInfo.size() <= id) return nullptr;
                if (gadget->GadgetInfo[id].name_enc)
                    return gadget->GadgetInfo[id].name_enc;
                return nullptr;
            }
            if (agent->GetIsItemType()) {
                const AgentItem* ag = agent->GetAsAgentItem();
                Item* item = Items::GetItemById(ag->item_id);
                return item ? item->name_enc : nullptr;
            }
            return nullptr;
        }

        bool AsyncGetAgentName(const Agent* agent, std::wstring& res) {
            wchar_t* str = GetAgentEncName(agent);
            if (!str) return false;
            UI::AsyncDecodeStr(str, &res);
            return true;
        }
    }
} // namespace GW
