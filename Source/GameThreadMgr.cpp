#include "stdafx.h"

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Scanner.h>

#include <GWCA/Managers/Module.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Utilities/Hooker.h>

namespace {
    using namespace GW;

    CRITICAL_SECTION mutex;

    typedef void(__cdecl *Render_t)(void*);
    Render_t LeaveGameThread_Func = 0;
    Render_t LeaveGameThread_Ret = 0;

    bool initialised = false;

    bool in_gamethread = false;

    std::vector<std::function<void()>> singleshot_callbacks;

    // Callbacks are triggered by weighting
    struct CallbackEntry {
        int altitude;
        HookEntry* entry;
        GameThread::GameThreadCallback callback;
    };
    std::vector<CallbackEntry> GameThread_callbacks;

    void CallFunctions()
    {
        if (!initialised)
            return;
        EnterCriticalSection(&mutex);
        in_gamethread = true;
        if (!singleshot_callbacks.empty()) {
            for (const auto& call : singleshot_callbacks) {
                call();
            }

            singleshot_callbacks.clear();
        }

        HookStatus status;
        for (auto& it : GameThread_callbacks) {
            it.callback(&status);
            ++status.altitude;
        }
        in_gamethread = false;
        LeaveCriticalSection(&mutex);
    }


    void __cdecl OnLeaveGameThread(void* unk) {
        HookBase::EnterHook();
        CallFunctions();
        LeaveGameThread_Ret(unk);
        HookBase::LeaveHook();
    }

    void Init()
    {
        InitializeCriticalSection(&mutex);

        uintptr_t address = Scanner::Find("\x8b\x75\x08\x57\x8b\x7d\x0c\xdd","xxxxxxxx",-0x20);
        LeaveGameThread_Func = (Render_t)address;


#ifdef _DEBUG
        GWCA_ASSERT(LeaveGameThread_Func);
#endif

        GW::HookBase::CreateHook((void**)&LeaveGameThread_Func, OnLeaveGameThread, (void **)&LeaveGameThread_Ret);
        /*
                uintptr_t address = Scanner::Find("\x2B\xCE\x8B\x15\x00\x00\x00\x00\xF7\xD9\x1B\xC9", "xxxx????xxxx", +4);
        GWCA_INFO("[SCAN] BasePointerLocation = %p", (void *)address);

#if _DEBUG
        GWCA_ASSERT(address);
#endif

        if (Verify(address)) {
            address = *(uintptr_t *)(address);
            address = *(uintptr_t *)(address + 0);
            address = *(uintptr_t *)(address + 0x3C);

            g__thingy = (uintptr_t *)(address + 4);
            g__thingyret = (Render_t)*g__thingy;
        }
        */



        initialised = true;
    }

    void EnableHooks()
    {
        if (!initialised)
            return;
        EnterCriticalSection(&mutex);
        HookBase::EnableHooks(LeaveGameThread_Func);
        LeaveCriticalSection(&mutex);
    }

    void DisableHooks()
    {
        if (!initialised)
            return;
        EnterCriticalSection(&mutex);
        HookBase::DisableHooks(LeaveGameThread_Func);
        LeaveCriticalSection(&mutex);
    }
    void Exit()
    {
        if (!initialised)
            return;
        DisableHooks();
        GameThread::ClearCalls();
        HookBase::RemoveHook(LeaveGameThread_Func);
        DeleteCriticalSection(&mutex);
    }
}

namespace GW {
    Module GameThreadModule = {
        "GameThreadModule", // name
        NULL,               // param
        ::Init,             // init_module
        ::Exit,             // exit_module
        ::EnableHooks,      // enable_hooks
        ::DisableHooks,     // disable_hooks
    };

    void GameThread::ClearCalls()
    {
        if (!initialised)
            return;
        EnterCriticalSection(&mutex);
        singleshot_callbacks.clear();
        GameThread_callbacks.clear();
        LeaveCriticalSection(&mutex);
    }

    void GameThread::Enqueue(std::function<void()> f)
    {
        if (!initialised)
            return;
        EnterCriticalSection(&mutex);
        if (in_gamethread) {
            f();
        }
        else {
            singleshot_callbacks.push_back(std::move(f));
        }
        LeaveCriticalSection(&mutex);
    }

    bool GameThread::IsInGameThread()
    {
        if (!initialised)
            return false;
        EnterCriticalSection(&mutex);
        const bool ret = in_gamethread;
        LeaveCriticalSection(&mutex);
        return ret;
    }

    void GameThread::RegisterGameThreadCallback(
        HookEntry *entry,
        const GameThreadCallback& callback,
        int altitude)
    {
        EnterCriticalSection(&mutex);
        RemoveGameThreadCallback(entry);

        auto it = GameThread_callbacks.begin();
        while (it != GameThread_callbacks.end()) {
            if (it->altitude > altitude)
                break;
            it++;
        }
        GameThread_callbacks.insert(it, { altitude,entry, callback });
        LeaveCriticalSection(&mutex);
    }

    void GameThread::RemoveGameThreadCallback(
        HookEntry *entry)
    {
        auto it = GameThread_callbacks.begin();
        while (it != GameThread_callbacks.end()) {
            if (it->entry == entry) {
                GameThread_callbacks.erase(it);
                break;
            }
            it++;
        }
    }
} // namespace GW
