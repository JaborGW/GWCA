#pragma once

#include <GWCA/Utilities/Export.h>
#include <GWCA/Utilities/Hook.h>

#include <GWCA/Managers/Module.h>

#ifndef GWCA_CTOS_ENABLED
#define GWCA_CTOS_ENABLED 1
#endif

namespace GW {
    struct Module;
    extern Module CtoSModule;

    namespace CtoS {
        typedef HookCallback<void*> PacketCallback;
#if GWCA_CTOS_ENABLED
        // Send packet that uses only dword parameters, can copypaste most gwa2 sendpackets :D. Returns true if enqueued.
        GWCA_API bool SendPacket(uint32_t size, ...);
#endif

        GWCA_API void RegisterPacketCallback(
            HookEntry* entry,
            uint32_t header,
            const PacketCallback& callback);

        GWCA_API void RemoveCallback(uint32_t header, HookEntry* entry);
        // Send a packet with a specific struct alignment, used for more complex packets. Returns true if enqueued.
#if GWCA_CTOS_ENABLED
        GWCA_API bool SendPacket(uint32_t size, void* buffer);

        template <class T>
        bool SendPacket(T *packet) {
            return SendPacket(sizeof(T), packet);
        }
#endif
    };
}
