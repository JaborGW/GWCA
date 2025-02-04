#include "stdafx.h"

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Macros.h>
#include <GWCA/Utilities/Scanner.h>

#include <GWCA/Managers/CtoSMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/RenderMgr.h>

namespace {
    using namespace GW;

    typedef void(__cdecl* SendPacket_pt)(uint32_t context, uint32_t size, void* packet);
    SendPacket_pt SendPacket_Func = 0;
    SendPacket_pt RetSendPacket = 0;
    
    uintptr_t game_srv_object_addr;
} // namespace


namespace GW::CtoS {
    bool SendPacket(uint32_t size, void* buffer)
    {
        if (!(Verify(SendPacket_Func && game_srv_object_addr))) return false;
        if (GameThread::IsInGameThread() || Render::GetIsInRenderLoop()) {
            // Already in game thread, don't need to worry about buffer lifecycle
            SendPacket_Func(*(uint32_t*)game_srv_object_addr, size, buffer);
            return true;
        }
        // Copy the packet and enqueue in the game thread
        void* buffer_cpy = malloc(size);
        GWCA_ASSERT(buffer_cpy != NULL);
        memcpy(buffer_cpy, buffer, size);
        GameThread::Enqueue([buffer_cpy, size]() {
            SendPacket_Func(*(uint32_t*)game_srv_object_addr, size, buffer_cpy);
            free(buffer_cpy);
        });
        return true;
    }

    bool SendPacket(uint32_t size, ...)
    {
        uint32_t* pak = &size + 1;
        return SendPacket(size, pak);
    }
    
    void Init()
    {
        SendPacket_Func = (SendPacket_pt)Scanner::FindAssertion("P:\\Code\\Net\\Msg\\MsgConn.cpp", "bytes >= sizeof(dword)", 0, -0x67);
        uintptr_t address = Scanner::FindAssertion("P:\\Code\\Gw\\Net\\Cli\\GcGameCmd.cpp", "No valid case for switch variable 'code'", 0, -0x32);
        if (Verify(address)) game_srv_object_addr = *(uintptr_t*)address;

        GWCA_INFO("[SCAN] SendPacket = %p", SendPacket_Func);
        GWCA_INFO("[SCAN] CtoGSObjectPtr = %p", game_srv_object_addr);

        GWCA_ASSERT(SendPacket_Func);
        GWCA_ASSERT(game_srv_object_addr);
    }
} // namespace GW
