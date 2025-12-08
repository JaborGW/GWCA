#include "stdafx.h"

#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Scanner.h>

#include <GWCA/GameEntities/Item.h>

#include <GWCA/Managers/ItemMgr.h>
#include <GWCA/Managers/Module.h>
#include <GWCA/Managers/TradeMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/TradeContext.h>

namespace {
    uint32_t GetTradeState() {
        const auto c = GW::GetTradeContext();
        return c ? c->flags : 0;
    }
    GW::UI::Frame* GetTradeWindow() {
        return GW::UI::GetFrameByLabel(L"DlgTrade");
    }
}

namespace GW {
    Module TradeModule = {
        "TradeModule",  // name
        NULL,           // param
        NULL,           // init_module
        NULL,           // exit_module
        NULL,           // enable_hooks
        NULL,           // disable_hooks
    };

    bool Trade::OpenTradeWindow(uint32_t agent_id) {
        return UI::SendUIMessage(GW::UI::UIMessage::kInitiateTrade, (void*)agent_id);
    }

    bool Trade::AcceptTrade() {
        return UI::ButtonClick(UI::GetChildFrame(GetTradeWindow(), 2));
    }

    bool Trade::CancelTrade() {
        const auto parent = GetTradeWindow();
        return UI::ButtonClick(UI::GetChildFrame(parent, 0xd)) || UI::ButtonClick(UI::GetChildFrame(parent, 1)); // Decline or Cancel
    }

    bool Trade::ChangeOffer() {
        return UI::ButtonClick(UI::GetChildFrame(GetTradeWindow(), 0));
    }

    bool Trade::SubmitOffer(uint32_t) {
        // TODO: Set gold amount
        return ChangeOffer();
    }
    bool Trade::RemoveItem(uint32_t item_id) {
        if (GetTradeState() != TradeContext::TRADE_INITIATED)
            return false;
        const auto frame = UI::GetFrameByLabel(L"CartPlayer");
        struct {
            uint32_t h0000 = 0;
            uint32_t h0004 = 9;
            uint32_t h0008 = 6;
            uint32_t h000c;
        } action;
        action.h000c = item_id;
        return frame && IsItemOffered(item_id) && UI::SendFrameUIMessage(frame, GW::UI::UIMessage::kMouseClick2, &action);
    }

    TradeItem* Trade::IsItemOffered(uint32_t item_id) {
        const auto ctx = GW::GetTradeContext();
        if (!ctx)
            return nullptr;
        auto& items = ctx->player.items;
        for (size_t i = 0; i < items.size();i++) {
            if (items[i].item_id != item_id)
                continue;
            return &items[i];
        }
        return nullptr;
    }
    bool Trade::OfferItem(uint32_t item_id, uint32_t quantity) {
        if (GetTradeState() != TradeContext::TRADE_INITIATED)
            return false;
        const auto frame = UI::GetFrameByLabel(L"CartPlayer");
        struct {
            uint32_t h0000 = 0;
            uint32_t h0004 = 2;
            uint32_t h0008 = 7;
            uint32_t* h000c;
        } action;

        uint32_t item_id_and_qty[] = { item_id, quantity };
        action.h000c = item_id_and_qty;
        action.h0000 = frame->frame_id;

        return frame && !IsItemOffered(item_id) && UI::SendFrameUIMessage(frame, GW::UI::UIMessage::kMouseClick2, &action);
    }
} // namespace GW
