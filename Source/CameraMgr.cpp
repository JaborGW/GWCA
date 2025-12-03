#include "stdafx.h"

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Macros.h>
#include <GWCA/Utilities/Scanner.h>
#include <GWCA/Utilities/MemoryPatcher.h>

#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Camera.h>

#include <GWCA/Managers/Module.h>
#include <GWCA/Managers/CameraMgr.h>

namespace {
    using namespace GW;

    MemoryPatcher patch_max_dist;
    MemoryPatcher patch_cam_update;
    MemoryPatcher patch_fog;
    MemoryPatcher patch_fov;

    Camera* scan_cam_class = nullptr;

    void Init() {

        uintptr_t address = Scanner::FindAssertion("GmCam.cpp", "fov", 0, 0xf);
        if (address)
            address = Scanner::FindInRange("\xd9\xee\xb9", "xxx", 3, address, address + 0xf);
        if (address && Scanner::IsValidPtr(*(uintptr_t*)address))
            scan_cam_class = *(Camera**)address;


        // @TODO: This patch is actually to stop the dx9 device setting a render setting; we could do this better by hooking the dx9 vtable directly - maybe render mgr?
        address = Scanner::Find("\x83\xe0\x01\x50\x6a\x1c\xff\x75", "xxxxxxxx", +2);
        if(address)
            patch_fog.SetPatch(address, "\x00", 1);

#if 0
        patch_max_dist_addr = Scanner::Find(
            "\xD8\xD9\xDF\xE0\xF6\xC4\x41\x75\x26\xD9\x46", "xxxxxxxxxxx", +0x9B);
        GWCA_INFO("[SCAN] patch_max_dist_addr = %p\n", (void *)patch_max_dist_addr);
#endif
        address = Scanner::Find("\x89\x0E\xDD\xD9\x89\x56\x04\xDD", "xxxxxxxx", 0);
        if (address) {
            // vs 2017
            patch_cam_update.SetPatch(address, "\xEB\x0C", 2);
        }
        else {
            // vs 2022
            address = Scanner::Find("\x8b\x45\xe8\x8b\x4d\xec\x8b\x55\xf0", "xxxxxxxxx", 0);
            if (address) {
                patch_cam_update.SetPatch(address, "\xEB\x0F", 2);
            }
        }

        GWCA_INFO("[SCAN] patch_cam_update_addr = %p", patch_cam_update.GetAddress());
        GWCA_INFO("[SCAN] patch_fog_addr = %p", patch_fog.GetAddress());
        GWCA_INFO("[SCAN] scan_cam_class = %p", scan_cam_class);

#ifdef _DEBUG
        GWCA_ASSERT(patch_fog.GetAddress());
        GWCA_ASSERT(patch_cam_update.GetAddress());
        GWCA_ASSERT(scan_cam_class);
#endif
    }

    void Exit() {
        patch_max_dist.Reset();
        patch_cam_update.Reset();
        patch_fog.Reset();
    }

    void EnableHooks() {

    }

    void DisableHooks() {
        if (patch_max_dist.IsValid())
            patch_max_dist.TogglePatch(false);
        if (patch_cam_update.IsValid())
            patch_cam_update.TogglePatch(false);
        if (patch_fog.IsValid())
            patch_fog.TogglePatch(false);
    }
}

namespace GW {

    Module CameraModule = {
        "CameraModule",     // name
        NULL,               // param
        ::Init,             // init_module
        ::Exit,             // exit_module
        ::EnableHooks,      // enable_hooks
        ::DisableHooks,     // disable_hooks
    };

    Camera *CameraMgr::GetCamera() {
        return scan_cam_class;
    }

    bool CameraMgr::SetMaxDist(float dist) {
        const auto cam = GetCamera();
        return cam ? cam->max_distance2 = dist, true : false;
    }

    bool CameraMgr::SetFieldOfView(float fov) {
        const auto cam = GetCamera();
        return cam ? cam->field_of_view = fov, true : false;
    }

    bool CameraMgr::UnlockCam(bool flag) {
        return patch_cam_update.IsValid() ? patch_cam_update.TogglePatch(flag) : false;
    }

    bool CameraMgr::GetCameraUnlock() {
        return patch_cam_update.GetIsActive();
    }

    bool CameraMgr::SetFog(bool flag) {
        return patch_fog.IsValid() ? patch_fog.TogglePatch(flag) : false;
    }

    bool CameraMgr::ForwardMovement(float amount, bool true_forward) {
        const auto cam = GetCamera();
        if (!(cam && amount != .0f))
            return false;
        if (true_forward) {
            float pitchX = sqrtf(1.f - cam->pitch * cam->pitch);
            cam->look_at_target.x += amount * pitchX * cosf(cam->yaw);
            cam->look_at_target.y += amount * pitchX * sinf(cam->yaw);
            cam->look_at_target.z += amount * cam->pitch;
        } else {
            cam->look_at_target.x += amount * cosf(cam->yaw);
            cam->look_at_target.y += amount * sinf(cam->yaw);
        }
        return true;
    }

    bool CameraMgr::VerticalMovement(float amount) {
        const auto cam = GetCamera();
        return cam ? cam->look_at_target.z += amount, true : false;
    }

    bool CameraMgr::SideMovement(float amount) {
        const auto cam = GetCamera();
        if (!(cam && amount != .0f))
            return false;
        cam->look_at_target.x += amount * -sinf(cam->yaw);
        cam->look_at_target.y += amount *  cosf(cam->yaw);
        return true;
    }

    bool CameraMgr::RotateMovement(float angle) {
        if (angle == 0.f) return false;
        // rotation with fixed z (vertical axe)
        const auto cam = GetCamera();
        if (!cam) return false;
        float pos_x = cam->position.x;
        float pos_y = cam->position.y;
        float px = cam->look_at_target.x - pos_x;
        float py = cam->look_at_target.y - pos_y;

        Vec3f newPos;
        newPos.x = pos_x + (cosf(angle) * px - sinf(angle) * py);
        newPos.y = pos_y + (sinf(angle) * px + cosf(angle) * py);
        newPos.z = cam->look_at_target.z;

        cam->SetYaw(cam->yaw + angle);
        cam->look_at_target = newPos;
        return true;
    }

    Vec3f CameraMgr::ComputeCamPos(float dist) {
        const auto cam = GetCamera();
        if (!cam) return {};
        if (dist == 0) dist = cam->GetCameraZoom();

        Vec3f newPos = cam->GetLookAtTarget();

        float pitchX = sqrtf(1.f - GetCamera()->pitch*GetCamera()->pitch);
        newPos.x -= dist * pitchX * cosf(GetCamera()->yaw);
        newPos.y -= dist * pitchX * sinf(GetCamera()->yaw);
        newPos.z -= dist * 0.95f * GetCamera()->pitch; // 0.95 is the max pitch, not 1.0

        return newPos;
    }

    bool CameraMgr::UpdateCameraPos() {
        const auto cam = GetCamera();
        return cam ? cam->SetCameraPos(ComputeCamPos()), true : false;
    }

    float CameraMgr::GetFieldOfView() {
        const auto cam = GetCamera();
        return cam ? cam->GetFieldOfView() : .0f;
    }

    float CameraMgr::GetYaw() {
        const auto cam = GetCamera();
        return cam ? cam->GetYaw() : .0f;
    }
} // namespace GW
