#include "stdafx.h"

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Scanner.h>
#include <GWCA/GameEntities/Camera.h>

#include <GWCA/Managers/Module.h>

#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/RenderMgr.h>
#include <GWCA/Managers/CameraMgr.h>

namespace {
    using namespace GW;
    typedef Render::Mat4x3f*(__cdecl*GwGetTransform_pt)(int transform);
    GwGetTransform_pt GwGetTransform_func;

    struct gwdx {
        uint8_t h0000_1[0x128];
        /* +h0000 */
        uint8_t h0000[24];
        /* +h0018 */
        uint32_t h0018; // might not be a func pointer, seems like it tho lol
        /* +h001C */
        uint8_t h001C[44];
        /* +h0048 */
        wchar_t gpu_name[32];
        /* +h0088 */
        uint8_t h0088[8];
        /* +h0090 */
        IDirect3DDevice9* device;
        /* +h0094 */
        uint8_t h0094[12];
        /* +h00A0 */
        uint32_t framecount;
        /* +h00A4 */
        uint8_t h00A4[2936];
        /* +h0C1C */
        uint32_t viewport_width;
        /* +h0C20 */
        uint32_t viewport_height;
        /* +h0C24 */
        uint8_t h0C24[148];
        /* +h0CB8 */
        uint32_t window_width;
        /* +h0CBC */
        uint32_t window_height;
        /* +h0CC0 */
        uint8_t h0CC0[952];
    }; //Size: 0x1074
    gwdx* gwdx_ptr = nullptr;

    typedef bool (__cdecl *GwEndScene_pt)(gwdx* ctx, void* unk);
    GwEndScene_pt RetGwEndScene = 0, GwEndScene_Func = 0;
    GwEndScene_pt RetScreenCapture = 0, ScreenCapture_Func = 0;

    CRITICAL_SECTION mutex;
    bool in_render_loop = false;

    typedef bool (__cdecl *GwReset_pt)(gwdx* ctx);
    GwReset_pt RetGwReset = 0, GwReset_Func = 0;

    GW::Render::RenderCallback render_callback = nullptr;
    GW::Render::RenderCallback reset_callback = nullptr;

    bool __cdecl OnGwEndScene(gwdx* ctx, void* unk)
    {
        HookBase::EnterHook();
        EnterCriticalSection(&mutex);
        in_render_loop = true;
        gwdx_ptr = ctx;
        if (render_callback)
            render_callback(ctx->device);
        bool retval = RetGwEndScene(ctx, unk);
        in_render_loop = false;
        LeaveCriticalSection(&mutex);
        HookBase::LeaveHook();
        return retval;
    }
    bool __cdecl OnGwReset(gwdx* ctx)
    {
        HookBase::EnterHook();
        gwdx_ptr = ctx;
        if (reset_callback)
            reset_callback(ctx->device);
        bool retval = RetGwReset(ctx);
        HookBase::LeaveHook();
        return retval;
    }

    bool __cdecl OnScreenCapture(gwdx* ctx, void* unk)
    {
        HookBase::EnterHook();
        // @Enhancement: This should probably be an option.
        if (!GW::UI::GetIsShiftScreenShot() && render_callback) {
            render_callback(ctx->device);
        }
        bool retval = RetScreenCapture(ctx, unk);
        HookBase::LeaveHook();
        return retval;
    }

    void Init()
    {
        InitializeCriticalSection(&mutex);

        uintptr_t address = Scanner::Find("\x75\x14\x68\xca\x03\x00\x00", "xxxxxxx");
        if (address)
            address = Scanner::FindInRange("\x55\x8b\xec", "xxx", 0, address, address - 0x64);
        if (address)
            GwReset_Func = (GwReset_pt)address;
        
        address = Scanner::Find("\x75\x28\x68\x8c\x08\x00\x00", "xxxxxxx");
        if (address)
            address = Scanner::FindInRange("\x55\x8b\xec", "xxx", 0, address, address - 0x64);
        if (address)
            GwEndScene_Func = (GwEndScene_pt)address;
        
        address = Scanner::Find("\x7c\x14\x68\xdb\x02\x00\x00", "xxxxxxx");
        if (address)
            address = Scanner::FindInRange("\x55\x8b\xec", "xxx", 0, address, address - 0x64);
        if (address)
            GwGetTransform_func = (GwGetTransform_pt)address;

        address = Scanner::FindAssertion("Dx9Dev.cpp","No valid case for switch variable 'mode.Format'");
        if (address)
            address = Scanner::FindInRange("\x55\x8b\xec", "xxx", 0, address, address - 0x400);
        if (address)
            ScreenCapture_Func = (GwEndScene_pt)address;

        GWCA_INFO("[SCAN] GwGetTransform = %p", GwGetTransform_func);
        GWCA_INFO("[SCAN] GwReset = %p", GwReset_Func);
        GWCA_INFO("[SCAN] GwEndScene = %p", GwEndScene_Func);
        GWCA_INFO("[SCAN] GwScreenCapture = %p", ScreenCapture_Func);

#ifdef _DEBUG
        GWCA_ASSERT(GwGetTransform_func); // remove in the future
        GWCA_ASSERT(GwReset_Func);
        GWCA_ASSERT(GwEndScene_Func);
        GWCA_ASSERT(ScreenCapture_Func);
#endif
        if(GwEndScene_Func)
            HookBase::CreateHook((void**)&GwEndScene_Func, OnGwEndScene, (void**)&RetGwEndScene);
        if(ScreenCapture_Func)
            HookBase::CreateHook((void**)&ScreenCapture_Func, OnScreenCapture, (void**)&RetScreenCapture);
        if(GwReset_Func)
            HookBase::CreateHook((void**)&GwReset_Func, OnGwReset, (void**)&RetGwReset);
    }

    void EnableHooks()
    {
        if (GwEndScene_Func)
            HookBase::EnableHooks(GwEndScene_Func);
        if (ScreenCapture_Func)
            HookBase::EnableHooks(ScreenCapture_Func);
        if (GwReset_Func)
            HookBase::EnableHooks(GwReset_Func);
    }

    void DisableHooks()
    {
        if (GwEndScene_Func)
            HookBase::DisableHooks(GwEndScene_Func);
        if (ScreenCapture_Func)
            HookBase::DisableHooks(ScreenCapture_Func);
        if (GwReset_Func)
            HookBase::DisableHooks(GwReset_Func);
    }

    void Exit()
    {
        HookBase::RemoveHook(GwEndScene_Func);
        HookBase::RemoveHook(ScreenCapture_Func);
        HookBase::RemoveHook(GwReset_Func);
        DeleteCriticalSection(&mutex);
    }
}

namespace GW {
    Module RenderModule = {
        "RenderModule", // name
        NULL,           // param
        ::Init,         // init_module
        ::Exit,         // exit_module
        ::EnableHooks,  // enable_hooks
        ::DisableHooks, // disable_hooks
    };

    bool Render::GetIsInRenderLoop()
    {
        EnterCriticalSection(&mutex);
        bool ret = in_render_loop;
        LeaveCriticalSection(&mutex);
        return ret;
    }

    IDirect3DDevice9* Render::GetDevice() {
        return gwdx_ptr ? gwdx_ptr->device : nullptr;
    }


    int Render::GetIsFullscreen()
    {
        // this is hacky and might be unreliable
        if (gwdx_ptr == nullptr) return -1;
        if (gwdx_ptr->viewport_height == gwdx_ptr->window_height
            && gwdx_ptr->viewport_width == gwdx_ptr->window_width)
            return 1;
        return 0;
    }

    uint32_t Render::GetViewportWidth()
    {
        return gwdx_ptr ? gwdx_ptr->viewport_width : 0;
    }

    uint32_t Render::GetViewportHeight()
    {
        return gwdx_ptr ? gwdx_ptr->viewport_height : 0;
    }

    Render::Mat4x3f* Render::GetTransform(Transform transform)
    {
        GWCA_ASSERT(GwGetTransform_func != nullptr);
        return GwGetTransform_func(transform);
    }

    float Render::GetFieldOfView()
    {
        const Camera* cam = CameraMgr::GetCamera();
        if (!cam) return 0.f;
        constexpr float dividend = 2.f/3.f + 1.f;
        return atan2(1.0f, dividend / tan(cam->GetFieldOfView() * 0.5f)) * 2.0f;
    }

    Render::RenderCallback Render::GetRenderCallback()
    {
        return render_callback;
    }

    void Render::SetRenderCallback(RenderCallback callback)
    {
        render_callback = callback;
    }

    void Render::SetResetCallback(RenderCallback callback)
    {
        reset_callback = callback;
    }
} // namespace GW
