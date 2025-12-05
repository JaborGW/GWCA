#include "stdafx.h"

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Scanner.h>

#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Attribute.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Hero.h>

#include <GWCA/Context/PartyContext.h>
#include <GWCA/Context/WorldContext.h>

#include <GWCA/Managers/Module.h>

#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/MapMgr.h>

namespace {
    using namespace GW;

    UI::UIInteractionCallback TickButtonUICallback = 0;
    UI::UIInteractionCallback TickButtonUICallback_Ret = 0;

    typedef void(__cdecl* PartySearchSeek_pt)(uint32_t search_type, const wchar_t* advertisement, uint32_t unk);
    PartySearchSeek_pt PartySearchSeek_Func = 0;

    typedef void(__fastcall* PartySearchButtonCallback_pt)(void* context, uint32_t edx, uint32_t* wparam);
    PartySearchButtonCallback_pt PartySearchButtonCallback_Func = 0;
    PartySearchButtonCallback_pt PartyWindowButtonCallback_Func = 0;

    typedef void(__cdecl* DoAction_pt)(uint32_t identifier);

    DoAction_pt SetReadyStatus_Func = 0;
    DoAction_pt SetDifficulty_Func = 0;

    DoAction_pt PartyAcceptInvite_Func = 0;
    DoAction_pt PartyRejectInvite_Func = 0;

    typedef void(__cdecl* FlagHeroAgent_pt)(uint32_t agent_id,GW::GamePos* pos);
    FlagHeroAgent_pt FlagHeroAgent_Func = 0;

    typedef void(__cdecl* FlagAll_pt)(GW::GamePos* pos);
    FlagAll_pt FlagAll_Func = 0;
    typedef void(__cdecl* SetHeroBehavior_pt)(uint32_t agent_id, HeroBehavior behavior);
    SetHeroBehavior_pt SetHeroBehavior_Func = 0;
    typedef bool(__cdecl* LockPetTarget_pt)(uint32_t pet_agent_id, uint32_t target_id);
    LockPetTarget_pt LockPetTarget_Func = 0;

    bool tick_work_as_toggle = false;

    UI::UIInteractionCallback OnTickButtonUICallback(UI::InteractionMessage* message, void* wParam, void* lParam) {
        HookBase::EnterHook();
        bool blocked = false;
        if (!tick_work_as_toggle)
            goto finish;
        switch ((int)message->message_id) {
        case 0x22:  // Ready state icon clicked
            PartyMgr::Tick(!PartyMgr::GetIsPlayerTicked());
            blocked = true;
            break;
        case 0x2c:  // Show ready state dropdown
            blocked = true;
            break;
        }
    finish:
        if (!blocked) {
            TickButtonUICallback_Ret(message, wParam, lParam);
        }
        HookBase::LeaveHook();
        return 0;
    }

    void Init() {
        // This function runs every time an update is made to the party window

        uintptr_t address = Scanner::Find("\x68\xfb\x0b\x01\x00", "xxxxx", 0x16);
        if (address)
            TickButtonUICallback = (UI::UIInteractionCallback)Scanner::FunctionFromNearCall(*(uintptr_t*)address);

        address = Scanner::Find("\x89\x46\x20\xF7", "xxxx", 0xBB); // NB: UI Message 0x10000128 lands within hard mode button ui callback
        if (address) 
            SetDifficulty_Func = (DoAction_pt)Scanner::FunctionFromNearCall(address);

        address = Scanner::Find("\x8b\x78\x4c\x8d\x8f\x9c\x00\x00\x00", "xxxxxxxxx", -0xc);
        if (Scanner::IsValidPtr(address, ScannerSection::Section_TEXT))
            PartySearchSeek_Func = (PartySearchSeek_pt)address;

        // Party Search Window Button Callback functions
        PartySearchButtonCallback_Func = (PartySearchButtonCallback_pt)Scanner::ToFunctionStart(Scanner::FindAssertion("\\Code\\Gw\\Ui\\Game\\Party\\PtSearch.cpp", "m_activeList == LIST_HEROES",0,0));

        // Party Window Button Callback functions
        PartyWindowButtonCallback_Func = (PartySearchButtonCallback_pt)Scanner::ToFunctionStart(Scanner::Find("\x8D\x77\x24\x56", "xxxx"));
        
        address = Scanner::FindAssertion("\\Code\\Gw\\Ui\\Game\\Party\\PtPlayer.cpp", "No valid case for switch variable '\"\"'", 0, 0x27);
        SetReadyStatus_Func = (DoAction_pt)Scanner::FunctionFromNearCall(address);

        address = Scanner::Find("\x8d\x45\x10\x50\x56\x6a\x5b\x57","xxxxxxxx");
        if (Scanner::IsValidPtr(address, ScannerSection::Section_TEXT)) {
            address = Scanner::FindInRange("\x8d\x4d\xe0\x51\x50\xe8", "xxxxxx", 5, address, address + 0x64);
            FlagHeroAgent_Func = (FlagHeroAgent_pt)Scanner::FunctionFromNearCall(address);
            if(address) address = Scanner::FindInRange("\xc7\x45\xdc", "xxx", 7, address, address + 0x64);
            FlagAll_Func = (FlagAll_pt)Scanner::FunctionFromNearCall(address);
        }

        address = Scanner::Find("\x83\xc4\x10\x83\xff\x03\x75\x17", "xxxxxxxx",0x38);
        if (Scanner::IsValidPtr(address, ScannerSection::Section_TEXT)) {

            LockPetTarget_Func = (LockPetTarget_pt)Scanner::FunctionFromNearCall(address);
            SetHeroBehavior_Func = (SetHeroBehavior_pt)Scanner::FunctionFromNearCall(address + 0x7);
        }

        address = Scanner::Find("\x6a\x00\x68\x00\x02\x02\x00\xff\x77\x04", "xxxxxxxxxx");
        if (Scanner::IsValidPtr(address, ScannerSection::Section_TEXT)) {
            PartyRejectInvite_Func = (DoAction_pt)Scanner::FunctionFromNearCall(address + 0xb6);
            PartyAcceptInvite_Func = (DoAction_pt)Scanner::FunctionFromNearCall(address + 0xcf);
        }

        GWCA_INFO("[SCAN] TickButtonUICallback Function = %p", TickButtonUICallback);
        GWCA_INFO("[SCAN] SetDifficulty_Func = %p", SetDifficulty_Func);
        GWCA_INFO("[SCAN] PartySearchSeek_Func = %p", PartySearchSeek_Func);
        GWCA_INFO("[SCAN] SetReadyStatus_Func = %p", SetReadyStatus_Func);
        GWCA_INFO("[SCAN] FlagHeroAgent_Func = %p", FlagHeroAgent_Func);
        GWCA_INFO("[SCAN] FlagAll_Func = %p", FlagAll_Func);
        GWCA_INFO("[SCAN] SetHeroBehavior_Func = %p", SetHeroBehavior_Func);
        GWCA_INFO("[SCAN] LockPetTarget_Func = %p", LockPetTarget_Func);
        GWCA_INFO("[SCAN] PartyWindowButtonCallback_Func = %p", PartyWindowButtonCallback_Func);
        GWCA_INFO("[SCAN] PartySearchButtonCallback_Func = %p", PartySearchButtonCallback_Func);

#ifdef _DEBUG
        GWCA_ASSERT(PartyWindowButtonCallback_Func);
        GWCA_ASSERT(PartySearchButtonCallback_Func);
        GWCA_ASSERT(TickButtonUICallback);
        GWCA_ASSERT(SetReadyStatus_Func);
        GWCA_ASSERT(FlagHeroAgent_Func);
        GWCA_ASSERT(FlagAll_Func);
        GWCA_ASSERT(SetHeroBehavior_Func);
        GWCA_ASSERT(LockPetTarget_Func);
#endif
        HookBase::CreateHook((void**)&TickButtonUICallback, OnTickButtonUICallback, (void**)&TickButtonUICallback_Ret);
    }

    void EnableHooks() {
        if (TickButtonUICallback)
            HookBase::EnableHooks(TickButtonUICallback);
    }
    void DisableHooks() {
        if (TickButtonUICallback)
            HookBase::DisableHooks(TickButtonUICallback);
    }
    void Exit() {
        if (TickButtonUICallback)
            HookBase::RemoveHook(TickButtonUICallback);

    }
}

namespace GW {

    Module PartyModule = {
        "PartyModule",  // name
        NULL,           // param
        ::Init,         // init_module
        ::Exit,         // exit_module
        ::EnableHooks,           // enable_hooks
        ::DisableHooks,           // disable_hooks
    };
    namespace PartyMgr {
        bool Tick(bool flag) {
            if (!(SetReadyStatus_Func && GetPartyInfo() && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost))
                return false;
            if (flag == GetIsPlayerTicked())
                return true;
            SetReadyStatus_Func(flag);
            return true;
        }

        Attribute* GetAgentAttributes(uint32_t agent_id) {
            auto* w = GetWorldContext();
            if (!(w && w->attributes.valid()))
                return nullptr;
            for (auto& agent_attributes : w->attributes) {
                if (agent_attributes.agent_id == agent_id)
                    return agent_attributes.attribute;
            }
            return nullptr;
        }

        PartySearch* GetPartySearch(uint32_t party_search_id) {
            const auto p = GW::GetPartyContext();
            if (!p) return nullptr;
            for (auto party : p->party_search) {
                if (party && party->party_search_id == party_search_id)
                    return party;
            }
            return nullptr;
        }

        PartyInfo* GetPartyInfo(uint32_t party_id) {
            GW::PartyContext* ctx = GW::GetPartyContext();
            if (!ctx || !ctx->parties.size()) return 0;
            if (!party_id) return ctx->player_party;
            if (party_id >= ctx->parties.size())
                return 0;
            return ctx->parties[party_id];
        }

        bool ReturnToOutpost() {
            return UI::ButtonClick(UI::GetChildFrame(UI::GetFrameByLabel(L"DlgRedirect"), 0));
        }

        bool GetIsPartyInHardMode() {
            auto* p = GetPartyContext();
            return p ? p->InHardMode() : false;
        }
        bool GetIsHardModeUnlocked() {
            auto* w = GetWorldContext();
            return w ? w->is_hard_mode_unlocked != 0 : false;
        }

        uint32_t GetPartySize() {
            PartyInfo* info = GetPartyInfo();
            return info ? info->players.size() + info->heroes.size() + info->henchmen.size() : 0;
        }

        uint32_t GetPartyPlayerCount() {
            PartyInfo* info = GetPartyInfo();
            return info ? info->players.size() : 0;
        }
        uint32_t GetPartyHeroCount() {
            PartyInfo* info = GetPartyInfo();
            return info ? info->heroes.size() : 0;
        }
        uint32_t GetPartyHenchmanCount() {
            PartyInfo* info = GetPartyInfo();
            return info ? info->henchmen.size() : 0;
        }

        bool GetIsPartyDefeated() {
            auto* p = GetPartyContext();
            return p ? p->IsDefeated() : false;
        }

        bool SetHardMode(bool flag) {
            auto* p = GetPartyContext();
            if (!(SetDifficulty_Func && p && p->player_party))
                return false;
            if (p->InHardMode() != flag) {
                SetDifficulty_Func(flag);
            }
            return true;
        }

        bool GetIsPartyTicked() {
            PartyInfo* info = GetPartyInfo();
            if (!(info && info->players.valid())) return false;
            for (const PlayerPartyMember& player : info->players) {
                if (!player.ticked()) return false;
            }
            return true;
        }

        bool GetIsPartyLoaded() {
            PartyInfo* info = GetPartyInfo();
            if (!(info && info->players.valid())) return false;
            for (const PlayerPartyMember& player : info->players) {
                if (!player.connected()) return false;
            }
            return true;
        }

        bool GetIsPlayerTicked(uint32_t player_index) {
            PartyInfo* info = GetPartyInfo();
            if (!(info && info->players.valid())) return false;
            if (player_index == -1) {
                // Player
                uint32_t player_id = GW::PlayerMgr::GetPlayerNumber();
                for (const PlayerPartyMember& player : info->players) {
                    if (player.login_number == player_id)
                        return player.ticked();
                }
                return false;
            }
            else {
                // Someone else
                if (player_index >= info->players.size()) return false;
                return (info->players[player_index].ticked());
            }
        }

        bool GetIsLeader() {
            PartyInfo* info = GetPartyInfo();
            if (!(info && info->players.valid())) return false;
            uint32_t player_id = GW::PlayerMgr::GetPlayerNumber();
            for (const PlayerPartyMember& player : info->players) {
                if (player.connected())
                    return player.login_number == player_id;
            }
            return false;
        }
        bool RespondToPartyRequest(uint32_t party_id, bool accept) {
            (party_id, accept);
            if (accept) {
                if (!PartyAcceptInvite_Func)
                    return false;
                PartyAcceptInvite_Func(party_id);
            }
            else {
                if (!PartyRejectInvite_Func)
                    return false;
                PartyRejectInvite_Func(party_id);
            }
            return true;
        }

        bool AddHero(uint32_t heroid) {
            if (!PartySearchButtonCallback_Func)
                return false;

            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x6;
            wparam[1] = 0x1;

            uint32_t ctx[13] = { 0 };
            ctx[0xb] = 1;
            ctx[9] = heroid;

            PartySearchButtonCallback_Func(ctx, 2, wparam);
            return true;
        }

        bool KickHero(uint32_t heroid) {
            if (!PartySearchButtonCallback_Func)
                return false;

            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x6;
            wparam[1] = 0x6;

            uint32_t ctx[13] = { 0 };
            ctx[0xb] = 1;
            ctx[ctx[0xb] + 8] = heroid;

            PartySearchButtonCallback_Func(ctx, 0, wparam);
            return true;
        }
        bool KickAllHeroes() {
            return KickHero(0x26);
        }
        bool AddHenchman(uint32_t agent_id) {
            if (!PartySearchButtonCallback_Func)
                return false;

            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x6;
            wparam[1] = 0x2;

            uint32_t ctx[13] = { 0 };
            ctx[0xb] = 2;
            ctx[10] = agent_id;

            PartySearchButtonCallback_Func(ctx, 0, wparam);
            return true;
        }

        bool KickHenchman(uint32_t agent_id) {
            if (!PartySearchButtonCallback_Func)
                return false;

            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x6;
            wparam[1] = 0x6;

            uint32_t ctx[13] = { 0 };
            ctx[0xb] = 2;
            ctx[ctx[0xb] + 8] = agent_id;

            PartySearchButtonCallback_Func(ctx, 0, wparam);
            return true;
        }
        bool KickPlayer(uint32_t playerid) {
            if (!PartyWindowButtonCallback_Func)
                return false;

            uint32_t ctx[14] = { 0 };
            ctx[0x34] = 1; // Pointer to a frame, make sure its not null
            ctx[0xb] = playerid;
            ctx[0xd] = 0;
            ctx[10] = 1;
            ctx[9] = 9;

            PartyWindowButtonCallback_Func(ctx, 0, 0);
            return true;
        }
        bool InvitePlayer(const wchar_t* player_name) {
            // There is a specific CtoS packet for this, but just use chat command instead
            if (!(player_name && player_name[0]))
                return false;
            wchar_t buf[32];
            int len = swprintf(buf, 32, L"invite %s", player_name);
            if (len < 0)
                return false;
            Chat::SendChat('/', buf); // TODO: SendChat to be bool, return result.
            return true;
        };
        bool InvitePlayer(uint32_t player_id) {
            // There is a specific CtoS packet for this, but just use chat command instead
            auto player = PlayerMgr::GetPlayerByID(player_id);
            if (!(player && player->name))
                return false;
            return InvitePlayer(player->name);
        }

        bool LeaveParty() {
            if (!PartyWindowButtonCallback_Func)
                return false;
            if (!GetPartySize())
                return true;

            uint32_t ctx[14] = { 0 };
            ctx[0xd] = 1;

            PartyWindowButtonCallback_Func(ctx, 0, 0);
            return true;
        }

        bool FlagHero(uint32_t hero_index, GamePos pos) {
            return FlagHeroAgent(Agents::GetHeroAgentID(hero_index), pos);
        }

        bool FlagHeroAgent(AgentID agent_id, GamePos pos) {
            // @Robustness: Make sure player has control of hero agent
            if (!FlagHeroAgent_Func)
                return false;
            if (agent_id == 0) return false;
            if (agent_id == Agents::GetControlledCharacterId()) return false;
            FlagHeroAgent_Func(agent_id, &pos);
            return true;
        }

        bool FlagAll(GamePos pos) {
            // @Robustness: Make sure player has H/H and is in explorable
            return FlagAll_Func ? FlagAll_Func(&pos), true : false;
        }

        bool UnflagHero(uint32_t hero_index) {
            // @Robustness: Make sure flag is set
            return FlagHero(hero_index, GamePos(HUGE_VALF, HUGE_VALF, 0));
        }

        bool UnflagAll() {
            // @Robustness: Make sure flag is set
            return FlagAll(GamePos(HUGE_VALF, HUGE_VALF, 0));
        }

        bool SetHeroBehavior(uint32_t agent_id, HeroBehavior behavior) {
            auto w = GetWorldContext();
            if (!(w && SetHeroBehavior_Func && w->hero_flags.size()))
                return false;
            auto& flags = w->hero_flags;
            for (auto& flag : flags) {
                if (flag.agent_id == agent_id) {
                    if (flag.hero_behavior != behavior)
                        SetHeroBehavior_Func(agent_id, behavior);
                    return true;
                }
            }
            return false;
        }
        bool SetPetBehavior(HeroBehavior behavior, uint32_t lock_target_id) {
            auto w = GetWorldContext();
            if (!(w && SetHeroBehavior_Func && LockPetTarget_Func && w->pets.size()))
                return false;

            // Always need to lock target (current target if fight, otherwise 0)

            const auto pet_info = GetPetInfo();
            if (!pet_info)
                return false;
            uint32_t target_agent_id = 0;
            if (behavior == HeroBehavior::Fight) {
                // Setting fight mode without a valid target results in the same effect as guard mode.
                // Check and validate target
                const auto target = static_cast<AgentLiving*>(lock_target_id ? Agents::GetAgentByID(lock_target_id) : Agents::GetTarget());
                if (!(target && target->GetIsLivingType() && target->allegiance == Constants::Allegiance::Enemy))
                    return false; // Invalid target
                target_agent_id = target->agent_id;
            }
            if(pet_info->locked_target_id != target_agent_id)
                LockPetTarget_Func(pet_info->agent_id, target_agent_id);
            if(pet_info->behavior != behavior)
                SetHeroBehavior_Func(pet_info->agent_id, behavior);
            return true;
        }

        PetInfo* GetPetInfo(uint32_t owner_agent_id) {
            auto w = GetWorldContext();
            if (!(w && w->pets.size()))
                return nullptr;
            if (owner_agent_id == 0)
                owner_agent_id = Agents::GetControlledCharacterId();
            for (auto& pet : w->pets) {
                if (pet.owner_agent_id == owner_agent_id)
                    return &pet;
            }
            return nullptr;
        }

        void SetTickToggle(bool enable) {
            tick_work_as_toggle = enable;
        }
        uint32_t GetHeroAgentID(uint32_t hero_index) {
            if (hero_index == 0)
                return Agents::GetControlledCharacterId();
            hero_index--;
            PartyInfo* party = GetPartyInfo();
            if (!party)
                return 0;
            HeroPartyMemberArray& heroes = party->heroes;
            return heroes.valid() && hero_index < heroes.size() ? heroes[hero_index].agent_id : 0;
        }
        uint32_t GetAgentHeroID(AgentID agent_id) {
            if (agent_id == (AgentID)0)
                return 0;
            PartyInfo* party = GetPartyInfo();
            if (!party)
                return 0;
            HeroPartyMemberArray& heroes = party->heroes;
            for (size_t i = 0; i < heroes.size(); i++) {
                auto& hero = heroes[i];
                if (hero.agent_id == agent_id)
                    return i + 1;
            }
            return 0;
        }

        HeroInfo* GetHeroInfo(uint32_t hero_id)
        {
            const auto w = GetWorldContext();
            if (!(w && w->hero_info.size())) {
                return nullptr;
            }
            for (auto& a : w->hero_info) {
                if (a.hero_id == hero_id) {
                    return &a;
                }
            }
            return nullptr;
        }

        bool SearchParty(uint32_t search_type, const wchar_t* advertisement) {
            if (!PartySearchSeek_Func)
                return false;
            PartySearchSeek_Func(search_type, advertisement ? advertisement : L"", 0);
            return true;
        }
        bool SearchPartyCancel() {
            if (!PartySearchButtonCallback_Func)
                return false;
            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x8;
            uint32_t ctx[13] = { 0 };
            PartySearchButtonCallback_Func(ctx, 0, wparam);
            return true;
        }
        bool SearchPartyReply(bool accept) {
            if (!PartySearchButtonCallback_Func)
                return false;

            uint32_t wparam[4] = { 0 };
            wparam[2] = 0x6;
            wparam[1] = 0x3;

            uint32_t ctx[13] = { 0 };
            ctx[0xb] = 0;
            ctx[8] = accept;

            PartySearchButtonCallback_Func(ctx, 0, wparam);
            return true;
        }
    }

} // namespace GW
