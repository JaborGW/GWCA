#include "stdafx.h"

#include <GWCA/Constants/Constants.h>

#include <GWCA/Context/GameContext.h>
#include <GWCA/Context/TextParser.h>

#include <GWCA/Utilities/Debug.h>
#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Utilities/Macros.h>
#include <GWCA/Utilities/Scanner.h>

#include <GWCA/GameContainers/Array.h>

#include <GWCA/Managers/Module.h>

#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/RenderMgr.h>

namespace {
    using namespace GW;

    typedef void (__cdecl *SendUIMessage_pt)(UI::UIMessage msgid, void *wParam, void *lParam);
    SendUIMessage_pt SendUIMessage_Func = 0;
    SendUIMessage_pt RetSendUIMessage = 0;

    struct TooltipObj {
        UI::TooltipInfo* tooltip;
    };

    typedef void(__cdecl* SetTooltip_pt)(UI::TooltipInfo** tooltip);
    SetTooltip_pt SetTooltip_Func = 0;
    SetTooltip_pt RetSetTooltip = 0;

    typedef uint32_t(__cdecl* CreateHashFromWchar_pt)(const wchar_t* wcs, int seed);
    CreateHashFromWchar_pt CreateHashFromWchar_Func = 0;
    CreateHashFromWchar_pt CreateHashFromWchar_Ret = 0;

    typedef uint32_t(__cdecl* GetChildFrameId_pt)(uint32_t, uint32_t);
    GetChildFrameId_pt GetChildFrameId_Func = 0;

    // Create a uint hash from a wide char array; used for hashing frame ids
    uint32_t __cdecl OnCreateHashFromWchar(wchar_t* wcs, int seed) {
        GW::Hook::EnterHook();
        uint32_t out = CreateHashFromWchar_Ret(wcs, seed);
        GW::Hook::LeaveHook();
        return out;
    }

    uintptr_t* UiFrames_Addr = nullptr;

    typedef uint32_t(__cdecl* CreateUIComponent_pt)(uint32_t frame_id, uint32_t component_flags, uint32_t tab_index, void* event_callback, wchar_t* name_enc, wchar_t* component_label);
    CreateUIComponent_pt CreateUIComponent_Func = 0;
    CreateUIComponent_pt CreateUIComponent_Ret = 0;

    struct CreateUIComponentCallbackEntry {
        int altitude;
        HookEntry* entry;
        UI::CreateUIComponentCallback callback;
    };
    std::vector<CreateUIComponentCallbackEntry> OnCreateUIComponent_callbacks;


    uint32_t __cdecl OnCreateUIComponent(uint32_t frame_id, uint32_t component_flags, uint32_t tab_index, void* event_callback, wchar_t* name_enc, wchar_t* component_label) {
        GW::Hook::EnterHook();
        UI::CreateUIComponentPacket packet = {frame_id,component_flags, tab_index, event_callback, name_enc, component_label};

        HookStatus status;
        size_t i = 0;
        // Pre callbacks
        for (; i < OnCreateUIComponent_callbacks.size(); i++) {
            const auto& it = OnCreateUIComponent_callbacks[i];
            if (it.altitude > 0)
                break;
            it.callback(&packet);
            ++status.altitude;
        }

        uint32_t out = CreateUIComponent_Ret(packet.frame_id,packet.component_flags,packet.tab_index,packet.event_callback,packet.name_enc,packet.component_label);

        // Post callbacks
        for (; i < OnCreateUIComponent_callbacks.size(); i++) {
            const auto& it = OnCreateUIComponent_callbacks[i];
            it.callback(&packet);
            ++status.altitude;
        }
        GW::Hook::LeaveHook();
        return out;
    }

    typedef void(__cdecl* SetWindowVisible_pt)(uint32_t window_id, uint32_t is_visible, void* wParam, void* lParam);
    SetWindowVisible_pt SetWindowVisible_Func = 0;

    typedef void(__cdecl* SetVolume_pt)(uint32_t volume_id, float amount); // NB: amount is actually a float but we use uint32_t, avoid the cast.
    SetVolume_pt SetVolume_Func = 0;

    typedef void(__cdecl* SetMasterVolume_pt)(float amount); // NB: amount is actually a float but we use uint32_t, avoid the cast.
    SetMasterVolume_pt SetMasterVolume_Func = 0;

    typedef void(__cdecl* SetWindowPosition_pt)(uint32_t window_id, UI::WindowPosition* info, void* wParam, void* lParam);
    SetWindowPosition_pt SetWindowPosition_Func = 0;

    typedef void(__fastcall* SendFrameUIMessage_pt)(Array<UI::UIInteractionCallback>* callbacks, void* edx, UI::UIMessage message_id, void* arg1, void* arg2);
    SendFrameUIMessage_pt SendFrameUIMessage_Func = 0;
    SendFrameUIMessage_pt SendFrameUIMessage_Ret = 0;

    typedef void(__cdecl* SendFrameUIMessageById_pt)(uint32_t frame_id, UI::UIMessage message_id, void* arg1, void* arg2);
    SendFrameUIMessageById_pt SendFrameUIMessageById_Func = 0, SendFrameUIMessageById_Ret = 0;

    

    typedef void(__cdecl* DrawOnCompass_pt)(uint32_t session_id, uint32_t pt_count, UI::CompassPoint* pts);
    DrawOnCompass_pt DrawOnCompass_Func = 0, DrawOnCompass_Ret = 0;

    void __cdecl OnDrawOnCompass(uint32_t session_id, uint32_t pt_count, UI::CompassPoint* pts) {
        GW::Hook::EnterHook();
        DrawOnCompass_Ret(session_id, pt_count, pts);
        GW::Hook::LeaveHook();
    }

    // Global array of every frame drawn in the game atm
    GW::Array<UI::Frame*>* s_FrameArray = nullptr;

    typedef void (__cdecl *LoadSettings_pt)(uint32_t size, uint8_t *data);
    LoadSettings_pt LoadSettings_Func = 0;

    struct EnumPreferenceInfo {
        wchar_t* name;
        uint32_t options_count;
        uint32_t* options;
        uint32_t unk;
        uint32_t pref_type; // Used to perform other logic we don't care about
    };
    // Used to ensure preference values are within range for GW to avoid assertion errors.
    EnumPreferenceInfo* EnumPreferenceOptions_Addr = 0; 

    typedef uint32_t (__cdecl *EnumClampValue_pt)(uint32_t pref_id, uint32_t original_value);
    struct NumberPreferenceInfo {
        wchar_t* name;
        uint32_t flags; // & 0x1 if we have to clamp the value
        uint32_t h000C;
        uint32_t h0010;
        EnumClampValue_pt clampProc; // Clamps upper/lower bounds for this value; GW will assert an error if this actually clamped the value
        void* mappingProc; // Used to update other UI elments when changed
    };
    // Used to ensure preference values are clamped if applicable to avoid assertion errors.
    NumberPreferenceInfo* NumberPreferenceOptions_Addr = 0;

    typedef void(__cdecl* ValidateAsyncDecodeStr_pt)(const wchar_t* s, GW::UI::DecodeStr_Callback cb, void* wParam);
    typedef uint32_t(__fastcall* DoAsyncDecodeStr_pt)(void* ecx, void* edx, wchar_t* encoded_str, GW::UI::DecodeStr_Callback cb, void* wParam);
    ValidateAsyncDecodeStr_pt ValidateAsyncDecodeStr = 0;
    // NB: This is a __thiscall, but the function that calls it is a __cdecl - we can't hook it because theres not enough room but would be nice.
    DoAsyncDecodeStr_pt AsyncDecodeStringPtr = 0;
    DoAsyncDecodeStr_pt RetAsyncDecodeStr = 0;


    bool open_links = false;
    HookEntry open_template_hook;

    uintptr_t CommandAction_Addr = 0;
    uintptr_t GameSettings_Addr = 0;
    uintptr_t ui_drawn_addr = 0;
    uintptr_t shift_screen_addr = 0;
    uintptr_t WorldMapState_Addr = 0;
    uintptr_t PreferencesInitialised_Addr = 0;

    UI::TooltipInfo*** CurrentTooltipPtr = 0;

    // Get in-game preferences assigd during gameplay
    typedef bool (__cdecl *GetFlagPreference_pt)(uint32_t flag_pref_id);
    GetFlagPreference_pt GetFlagPreference_Func = 0;
    typedef void (__cdecl *SetFlagPreference_pt)(uint32_t flag_pref_id, bool value);
    SetFlagPreference_pt SetFlagPreference_Func = 0;

    typedef wchar_t* (__cdecl *GetStringPreference_pt)(uint32_t string_pref_id);
    GetStringPreference_pt GetStringPreference_Func = 0;
    typedef void (__cdecl *SetStringPreference_pt)(uint32_t string_pref_id, wchar_t* value);
    SetStringPreference_pt SetStringPreference_Func = 0;

    typedef uint32_t (__cdecl *GetEnumPreference_pt)(uint32_t choice_pref_id);
    GetEnumPreference_pt GetEnumPreference_Func = 0;
    typedef void (__cdecl *SetEnumPreference_pt)(uint32_t choice_pref_id, uint32_t value);
    SetEnumPreference_pt SetEnumPreference_Func = 0;

    typedef uint32_t (__cdecl *GetNumberPreference_pt)(uint32_t number_pref_id);
    GetNumberPreference_pt GetNumberPreference_Func = 0;
    typedef void (__cdecl *SetNumberPreference_pt)(uint32_t number_pref_id, uint32_t value);
    SetNumberPreference_pt SetNumberPreference_Func = 0;

    // Get command line parameters that were assigned when GW started
    GetFlagPreference_pt GetCommandLineFlag_Func = 0;
    GetNumberPreference_pt GetCommandLineNumber_Func = 0;
    uint32_t* CommandLineNumber_Buffer = 0;
    GetStringPreference_pt GetCommandLineString_Func = 0; // NB: Plus 0x27 when calling

    typedef uint32_t (__cdecl *GetGraphicsRendererValue_pt)(void* graphics_renderer_ptr, uint32_t metric_id); 
    GetGraphicsRendererValue_pt GetGraphicsRendererValue_Func = 0; // Can be used to get info about the graphics device e.g. vsync state
    typedef void (__cdecl *SetGraphicsRendererValue_pt)(void* graphics_renderer, uint32_t renderer_mode, uint32_t metric_id, uint32_t value); 
    SetGraphicsRendererValue_pt SetGraphicsRendererValue_Func = 0; // Triggers the graphics device to use the metric given e.g. anti aliasing level

    typedef uint32_t(__cdecl* GetGameRendererMode_pt)(uint32_t game_renderer_context);
    GetGameRendererMode_pt GetGameRendererMode_Func = 0;
    typedef void(__cdecl* SetGameRendererMode_pt)(uint32_t game_renderer_context, uint32_t game_renderer_mode);
    SetGameRendererMode_pt SetGameRendererMode_Func = 0;

    typedef uint32_t(__cdecl* GetGameRendererMetric_pt)(uint32_t game_renderer_context, uint32_t game_renderer_mode, uint32_t metric_key);
    GetGameRendererMetric_pt GetGameRendererMetric_Func = 0;

    typedef void (__cdecl *SetInGameShadowQuality_pt)(uint32_t value); 
    SetInGameShadowQuality_pt SetInGameShadowQuality_Func = 0; // Triggers the game to actually use the shadow quality given.

    typedef void (__cdecl *SetInGameStaticPreference_pt)(uint32_t static_preference_id, uint32_t value);
    // There are a bunch of static variables used at run time which are directly associated with some preferences. This function will sort those variables out.
    SetInGameStaticPreference_pt SetInGameStaticPreference_Func = 0;

    typedef void (__cdecl *TriggerTerrainRerender_pt)(); 
    // After we've updated some game world related preferences, this function triggers the actual rerender.
    TriggerTerrainRerender_pt TriggerTerrainRerender_Func = 0;

    typedef void (__cdecl *SetInGameUIScale_pt)(uint32_t value); 
    SetInGameUIScale_pt SetInGameUIScale_Func = 0; // Triggers the game to actually use the ui scale chosen.

    typedef GW::UI::Frame* (__cdecl* GetRootFrame_pt)();
    GetRootFrame_pt GetRootFrame_Func = 0;


    UI::WindowPosition* window_positions_array = 0;

    void OnOpenTemplate_UIMessage(HookStatus *hook_status, UI::UIMessage msgid, void *wParam, void *)
    {
        GWCA_ASSERT(msgid == UI::UIMessage::kOpenTemplate && wParam);
        UI::ChatTemplate *info = static_cast<UI::ChatTemplate *>(wParam);
        if (!(open_links && info && info->code.valid() && info->name))
            return;
        if (!wcsncmp(info->name, L"http://", 7) || !wcsncmp(info->name, L"https://", 8)) {
            hook_status->blocked = true;
            ShellExecuteW(NULL, L"open", info->name, NULL, NULL, SW_SHOWNORMAL);
        }
    }
    // Callbacks are triggered by weighting
    struct CallbackEntry {
        int altitude;
        HookEntry* entry;
        UI::UIMessageCallback callback;
    };
    std::unordered_map<UI::UIMessage,std::vector<CallbackEntry>> UIMessage_callbacks;

    void __cdecl OnSendUIMessage(UI::UIMessage msgid, void *wParam, void *lParam)
    {
        HookBase::EnterHook();
        UI::SendUIMessage(msgid, wParam, lParam);
        HookBase::LeaveHook();
    }

    struct FrameCallbackEntry {
        int altitude;
        HookEntry* entry;
        UI::FrameUIMessageCallback callback;
    };

    std::unordered_map<UI::UIMessage,std::vector<FrameCallbackEntry>> FrameUIMessage_callbacks;

    void __cdecl OnSendFrameUIMessageById(uint32_t frame_id, UI::UIMessage message_id, void* wParam, void* lParam) {
        HookBase::EnterHook();
        if (frame_id)
            SendFrameUIMessageById_Ret(frame_id, message_id, wParam, lParam);
        HookBase::LeaveHook();
    }

    void __fastcall OnSendFrameUIMessage(Array<UI::UIInteractionCallback>* frame_callbacks, void*, UI::UIMessage message_id, void* wParam, void* lParam) {
        HookBase::EnterHook();
        const auto frame = (UI::Frame*)(((uintptr_t)frame_callbacks) - 0xA0);
        GWCA_ASSERT(&frame->frame_callbacks == frame_callbacks);
        UI::SendFrameUIMessage(frame, message_id, wParam, lParam);
        HookBase::LeaveHook();
    }

    struct AsyncBuffer {
        void *buffer;
        size_t size;
    };

    void __cdecl __callback_copy_char(void *param, const wchar_t *s) {
        GWCA_ASSERT(param && s);
        AsyncBuffer *abuf = (AsyncBuffer *)param;
        char *outstr = (char *)abuf->buffer;
        for (size_t i = 0; i < abuf->size; i++) {
            outstr[i] = s[i] & 0x7F;
            if (!s[i]) break;
        }
        delete abuf;
    }

    void __cdecl __callback_copy_wchar(void *param, const wchar_t *s) {
        GWCA_ASSERT(param && s);
        AsyncBuffer *abuf = (AsyncBuffer *)param;
        wcsncpy((wchar_t *)abuf->buffer, s, abuf->size);
        delete abuf;
    }

    void __cdecl __calback_copy_wstring(void *param, const wchar_t *s) {
        GWCA_ASSERT(param && s);
        std::wstring *str = (std::wstring *)param;
        *str = s;
    }



    void Init() {
        uintptr_t address;

        address = Scanner::FindAssertion("\\Code\\Engine\\Frame\\FrMsg.cpp", "frame", 0, -0x14);
        if (address)
            s_FrameArray = *(GW::Array<UI::Frame*>**)address;
        


        address = Scanner::Find("\x81\x0D\xFF\xFF\xFF\xFF\x00\x00\x08\x00", "xx????xxxx", 2);
        if(address && Scanner::IsValidPtr(*(uintptr_t*)address))
            WorldMapState_Addr = *(uintptr_t*)address;


        address = Scanner::Find("\x83\xfb\x47\x73\x14", "xxxxx", -0x34);
        if (address) {
            SendFrameUIMessageById_Func = (SendFrameUIMessageById_pt)address;
            // SendFrameUIMessage_Func = (SendFrameUIMessage_pt)Scanner::Find("\x8D\x46\x04\xFF\x75\x10", "xxxxx", -0x5A);
            SendFrameUIMessage_Func = (SendFrameUIMessage_pt)Scanner::FunctionFromNearCall(address + 0x67);
        }


        // @TODO: Grab the seeding context from memory, write this ourselves!
        address = Scanner::Find("\x85\xc0\x74\x0d\x6a\xff\x50","xxxxxxx",0x7);
        CreateHashFromWchar_Func = (CreateHashFromWchar_pt)GW::Scanner::FunctionFromNearCall(address);
        


        // @TODO: Grab the relationship array from memory, write this ourselves!
        address = Scanner::FindAssertion("\\Code\\Engine\\Controls\\CtlView.cpp", "pageId", 0, 0x19);
        GetChildFrameId_Func = (GetChildFrameId_pt)GW::Scanner::FunctionFromNearCall(address);
        

        GetRootFrame_Func = (GetRootFrame_pt)Scanner::Find("\x05\xe0\xfe\xff\xff\xc3", "xxxxxx", -0x3c);
        

        SendUIMessage_Func = (SendUIMessage_pt)Scanner::ToFunctionStart(Scanner::Find("\xE8\x00\x00\x00\x00\x5D\xC3\x89\x45\x08\x5D\xE9", "x????xxxxxxx"));
        

        LoadSettings_Func = (LoadSettings_pt)Scanner::ToFunctionStart(Scanner::Find("\xE8\x00\x00\x00\x00\xFF\x75\x0C\xFF\x75\x08\x6A\x00", "x????xxxxxxxx"));
        

        address = Scanner::FindAssertion("\\Code\\Gw\\Ui\\UiRoot.cpp", "!s_count++", 0, -0xD);
        if (Verify(address))
            ui_drawn_addr = *(uintptr_t*)address - 0x10;
        

        address = Scanner::Find(
            "\x75\x19\x6A\x00\xC7\x05\x00\x00\x00\x00\x01\x00", "xxxxxx????xx", +6);
        if (address && Scanner::IsValidPtr(*(uintptr_t*)address))
            shift_screen_addr = *(uintptr_t *)address;
        


        address = Scanner::FindAssertion("\\Code\\Gw\\Pref\\PrApi.cpp", "location < arrsize(s_flushDelay)", 0, -0x12);
        if (address && Scanner::IsValidPtr(*(uintptr_t*)address))
            PreferencesInitialised_Addr = *(uintptr_t*)address;
        

        address = GW::Scanner::Find("\x8d\x85\x78\xf7\xff\xff\x50", "xxxxxxx",0x7);
        address = GW::Scanner::FunctionFromNearCall(address); // BuildLoginStruct
        if (address) {
            GetCommandLineFlag_Func = (GetFlagPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0xf);
            GetCommandLineString_Func = (GetStringPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0x32);

            GetStringPreference_Func = (GetStringPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0x5c);
            GetFlagPreference_Func = (GetFlagPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0x10b);
            GetEnumPreference_Func = (GetEnumPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0x118);
            GetNumberPreference_Func = (GetNumberPreference_pt)GW::Scanner::FunctionFromNearCall(address + 0x13f);
        }


        
        address = Scanner::ToFunctionStart(GW::Scanner::FindAssertion("\\Code\\Gw\\Param\\Param.cpp","value - PARAM_VALUE_FIRST < (sizeof(s_values) / sizeof((s_values)[0]))",0, 0));
        if (address) {
            GetCommandLineNumber_Func = (GetNumberPreference_pt)address;
            CommandLineNumber_Buffer = *(uint32_t**)(address + 0x29);
            CommandLineNumber_Buffer += 0x30; // Offset for command line values
        }


        SetInGameShadowQuality_Func = (SetInGameShadowQuality_pt)Scanner::ToFunctionStart(Scanner::FindAssertion("AvShadow.cpp","No valid case for switch variable 'value'",0,0));
        


        address = GW::Scanner::Find("\x83\xc4\x1c\x81\xfe\x20\x03\x00\x00","xxxxxxxxx", 0x31);
        SetInGameUIScale_Func = (SetInGameUIScale_pt)GW::Scanner::FunctionFromNearCall(address);
        

        address = GW::Scanner::FindAssertion("\\Code\\Gw\\Ui\\Game\\CharCreate\\CharCreate.cpp", "msg.summaryBytes <= NET_CHARACTER_SUMMARY_MAX",0, -0x62);
        SetStringPreference_Func = (SetStringPreference_pt)GW::Scanner::FunctionFromNearCall(address);
        

        address = GW::Scanner::FindAssertion("\\Code\\Gw\\Ui\\Dialog\\DlgOptGr.cpp", "No valid case for switch variable 'quality'",0,0);
        if (address) {
            SetEnumPreference_Func = (SetEnumPreference_pt)GW::Scanner::FunctionFromNearCall(address - 0x84);
            SetFlagPreference_Func = (SetFlagPreference_pt)GW::Scanner::FunctionFromNearCall(address - 0x3b);
            SetNumberPreference_Func = (SetNumberPreference_pt)GW::Scanner::FunctionFromNearCall(address - 0x61);
            SetInGameStaticPreference_Func = (SetInGameStaticPreference_pt)GW::Scanner::FunctionFromNearCall(address - 0xf6);
            TriggerTerrainRerender_Func = (TriggerTerrainRerender_pt)GW::Scanner::FunctionFromNearCall(address - 0x36);
        }



        address = GW::Scanner::FindAssertion("\\Code\\Gw\\Pref\\PrConst.cpp", "pref < arrsize(s_enumInfo)", 0, 0x15);
        if (address && GW::Scanner::IsValidPtr(address, GW::ScannerSection::Section_TEXT))
            EnumPreferenceOptions_Addr = *(EnumPreferenceInfo**)address;
        address = GW::Scanner::FindAssertion("\\Code\\Gw\\Pref\\PrConst.cpp", "pref < arrsize(s_valueInfo)", 0, 0x15);
        if (address && GW::Scanner::IsValidPtr(address, GW::ScannerSection::Section_TEXT))
            NumberPreferenceOptions_Addr = *(NumberPreferenceInfo**)address;


        SetTooltip_Func = (SetTooltip_pt)Scanner::ToFunctionStart(GW::Scanner::FindAssertion("\\Code\\Engine\\Frame\\FrTip.cpp", "CMsg::Validate(id)",0,0));
        if (SetTooltip_Func) {
            address = (uintptr_t)SetTooltip_Func;
            address += 0x9;
            CurrentTooltipPtr = (UI::TooltipInfo***)(*(uintptr_t*)address);
        }



        address = Scanner::Find("\x8D\x4B\x28\x89\x73\x24\x8B\xD7", "xxxxxxx", +0x10);
        if (address && GW::Scanner::IsValidPtr(address, GW::ScannerSection::Section_TEXT))
            GameSettings_Addr = *(uintptr_t*)address;

        

        // NB: 0x66 is the size of the window info array
        SetWindowVisible_Func = (SetWindowVisible_pt)Scanner::ToFunctionStart(Scanner::Find("\x8B\x75\x08\x83\xFE\x66\x7C\x19\x68", "xxxxxxxxx"));
        if (SetWindowVisible_Func) {
            SetWindowPosition_Func = reinterpret_cast<SetWindowPosition_pt>((uintptr_t)SetWindowVisible_Func - 0xE0);
            address = (uintptr_t)SetWindowVisible_Func + 0x49;
            if (Verify(address)) {
                window_positions_array = *(UI::WindowPosition**)address;
            }
        }


        ValidateAsyncDecodeStr = (ValidateAsyncDecodeStr_pt)Scanner::ToFunctionStart(Scanner::Find("\x83\xC4\x10\x3B\xC6\x5E\x74\x14", "xxxxxxxx"));
        AsyncDecodeStringPtr = (DoAsyncDecodeStr_pt)Scanner::ToFunctionStart(Scanner::Find("\x8b\x47\x14\x8d\x9f\x80\xfe\xff\xff", "xxxxxxxxx"));

        // NB: "p:\\code\\engine\\sound\\sndmain.cpp", "(unsigned)type < arrsize(s_volume)" works, but also matches SetVolume()
        SetVolume_Func = (SetVolume_pt)Scanner::ToFunctionStart(GW::Scanner::Find("\x8b\x75\x08\x83\xfe\x05\x72\x14\x68\x5b\x04\x00\x00\xba", "xxxxxxxxxxxxxx"));

        SetMasterVolume_Func = (SetMasterVolume_pt)Scanner::ToFunctionStart(Scanner::Find("\xd9\x45\x08\x83\xc6\x1c\x83\xef\x01\x75\xea\x5f\xdd\xd8\x5e\x5d", "xxxxxxxxxxxxxxxx"));
        DrawOnCompass_Func = (DrawOnCompass_pt)Scanner::ToFunctionStart(Scanner::FindAssertion("\\Code\\Gw\\Char\\CharMsg.cpp", "knotCount <= arrsize(message.knotData)",0,0));

        CreateUIComponent_Func = (CreateUIComponent_pt)Scanner::ToFunctionStart(GW::Scanner::Find("\x33\xd2\x89\x45\x08\xb9\xac\x01\x00\x00", "xxxxxxxxxx"));


        // Graphics renderer related

        address = GW::Scanner::Find("\x74\x12\x6a\x16\x6a\x00", "xxxxxx", 0x6);
        GetGraphicsRendererValue_Func = (GetGraphicsRendererValue_pt)GW::Scanner::FunctionFromNearCall(address);
        SetGraphicsRendererValue_Func = (SetGraphicsRendererValue_pt)Scanner::ToFunctionStart(Scanner::Find("\x68\x75\x0a\x00\x00", "xxxxx"));


        address = GW::Scanner::FindAssertion("\\Code\\Gw\\Ui\\Dialog\\DlgOptGr.cpp", "multiSampleIndex != CTL_DROPLIST_INDEX_NULL", 0, -0x46);
        SetGameRendererMode_Func = (SetGameRendererMode_pt)GW::Scanner::FunctionFromNearCall(address);

        address = GW::Scanner::Find("\x83\xc4\x1c\x81\xfe\x20\x03\x00\x00", "xxxxxxxxx");
        if (address) {
            GetGameRendererMode_Func = (GetGameRendererMode_pt)GW::Scanner::FunctionFromNearCall(address - 0x1d);
            GetGameRendererMetric_Func = (GetGameRendererMetric_pt)GW::Scanner::FunctionFromNearCall(address - 0x5);
        }


        GWCA_INFO("[SCAN] WorldMapState_Addr = %p", WorldMapState_Addr);
        GWCA_INFO("[SCAN] SendFrameUIMessage_Func = %p", SendFrameUIMessage_Func);
        GWCA_INFO("[SCAN] SendUIMessage = %p", SendUIMessage_Func);
        GWCA_INFO("[SCAN] LoadSettings = %p", LoadSettings_Func);
        GWCA_INFO("[SCAN] ui_drawn_addr = %p", ui_drawn_addr);
        GWCA_INFO("[SCAN] shift_screen_addr = %p", shift_screen_addr);
        GWCA_INFO("[SCAN] GetStringPreference_Func = %p", GetStringPreference_Func);
        GWCA_INFO("[SCAN] GetEnumPreference_Func = %p", GetEnumPreference_Func);
        GWCA_INFO("[SCAN] GetNumberPreference_Func = %p", GetNumberPreference_Func);
        GWCA_INFO("[SCAN] GetFlagPreference_Func = %p", GetFlagPreference_Func);
        GWCA_INFO("[SCAN] SetStringPreference_Func = %p", SetStringPreference_Func);
        GWCA_INFO("[SCAN] SetEnumPreference_Func = %p", SetEnumPreference_Func);
        GWCA_INFO("[SCAN] SetNumberPreference_Func = %p", SetNumberPreference_Func);
        GWCA_INFO("[SCAN] SetFlagPreference_Func = %p", SetFlagPreference_Func);
        GWCA_INFO("[SCAN] SetTooltip_Func = %p", SetTooltip_Func);
        GWCA_INFO("[SCAN] CurrentTooltipPtr = %p", CurrentTooltipPtr);
        GWCA_INFO("[SCAN] GameSettings = %p", GameSettings_Addr);
        GWCA_INFO("[SCAN] SetWindowVisible_Func = %p", SetWindowVisible_Func);
        GWCA_INFO("[SCAN] SetWindowPosition_Func = %p", SetWindowPosition_Func);
        GWCA_INFO("[SCAN] window_positions_array = %p", window_positions_array);
        GWCA_INFO("[SCAN] ValidateAsyncDecodeStr = %p", ValidateAsyncDecodeStr);
        GWCA_INFO("[SCAN] AsyncDecodeStringPtr = %p", AsyncDecodeStringPtr);
        GWCA_INFO("[SCAN] SetVolume_Func = %p", SetVolume_Func);
        GWCA_INFO("[SCAN] SetMasterVolume_Func = %p", SetMasterVolume_Func);
        GWCA_INFO("[SCAN] DrawOnCompass_Func = %p", DrawOnCompass_Func);
        GWCA_INFO("[SCAN] CreateUIComponent_Func = %p", CreateUIComponent_Func);
        GWCA_INFO("[SCAN] CommandLineNumber_Buffer = %p", CommandLineNumber_Buffer);
        GWCA_INFO("[SCAN] EnumPreferenceOptions_Addr = %p", EnumPreferenceOptions_Addr);
        GWCA_INFO("[SCAN] NumberPreferenceOptions_Addr = %p", NumberPreferenceOptions_Addr);
        GWCA_INFO("[SCAN] SetInGameStaticPreference_Func = %p", SetInGameStaticPreference_Func);
        GWCA_INFO("[SCAN] SetInGameUIScale_Func = %p", SetInGameUIScale_Func);
        GWCA_INFO("[SCAN] PreferencesInitialised_Addr = %p", PreferencesInitialised_Addr);
        GWCA_INFO("[SCAN] GetRootFrame_Func %p", GetRootFrame_Func);

        GWCA_INFO("[SCAN] s_FrameArray = %p", s_FrameArray);
        GWCA_INFO("[SCAN] SendFrameUIMessageById_Func = %p", SendFrameUIMessageById_Func);
        GWCA_INFO("[SCAN] SendFrameUIMessage_Func = %p", SendFrameUIMessage_Func);
        GWCA_INFO("[SCAN] CreateHashFromWchar_Func = %p", CreateHashFromWchar_Func);
        GWCA_INFO("[SCAN] GetChildFrameId_Func = %p", GetChildFrameId_Func);
        GWCA_INFO("[SCAN] GetRootFrame_Func = %p", GetRootFrame_Func);
        GWCA_INFO("[SCAN] GetGraphicsRendererValue_Func = %p", GetGraphicsRendererValue_Func);
        GWCA_INFO("[SCAN] SetGraphicsRendererValue_Func = %p", SetGraphicsRendererValue_Func);
        GWCA_INFO("[SCAN] SetGameRendererMode_Func = %p", SetGameRendererMode_Func);
        GWCA_INFO("[SCAN] GetGameRendererMetric_Func = %p", GetGameRendererMetric_Func);
#ifdef _DEBUG
        GWCA_ASSERT(s_FrameArray);
        GWCA_ASSERT(WorldMapState_Addr);
        GWCA_ASSERT(SendFrameUIMessageById_Func);
        GWCA_ASSERT(SendFrameUIMessage_Func);
        GWCA_ASSERT(CreateHashFromWchar_Func);
        GWCA_ASSERT(GetChildFrameId_Func);
        GWCA_ASSERT(GetRootFrame_Func);
        GWCA_ASSERT(SendUIMessage_Func);
        GWCA_ASSERT(LoadSettings_Func);
        GWCA_ASSERT(ui_drawn_addr);
        GWCA_ASSERT(shift_screen_addr);
        GWCA_ASSERT(PreferencesInitialised_Addr);
        GWCA_ASSERT(GetCommandLineFlag_Func);
        GWCA_ASSERT(GetCommandLineString_Func);
        GWCA_ASSERT(GetStringPreference_Func);
        GWCA_ASSERT(GetFlagPreference_Func);
        GWCA_ASSERT(GetEnumPreference_Func);
        GWCA_ASSERT(GetNumberPreference_Func);
        GWCA_ASSERT(GetCommandLineNumber_Func);
        GWCA_ASSERT(CommandLineNumber_Buffer);
        GWCA_ASSERT(SetInGameShadowQuality_Func);
        GWCA_ASSERT(SetInGameUIScale_Func);
        GWCA_ASSERT(SetStringPreference_Func);
        GWCA_ASSERT(SetEnumPreference_Func);
        GWCA_ASSERT(SetFlagPreference_Func);
        GWCA_ASSERT(SetNumberPreference_Func);
        GWCA_ASSERT(SetInGameStaticPreference_Func);
        GWCA_ASSERT(TriggerTerrainRerender_Func);
        GWCA_ASSERT(EnumPreferenceOptions_Addr);
        GWCA_ASSERT(NumberPreferenceOptions_Addr);
        GWCA_ASSERT(SetTooltip_Func);
        GWCA_ASSERT(CurrentTooltipPtr);
        GWCA_ASSERT(GameSettings_Addr);
        GWCA_ASSERT(SetWindowVisible_Func);
        GWCA_ASSERT(SetWindowPosition_Func);
        GWCA_ASSERT(window_positions_array);

        GWCA_ASSERT(GetStringPreference_Func);
        GWCA_ASSERT(GetEnumPreference_Func);
        GWCA_ASSERT(GetNumberPreference_Func);
        GWCA_ASSERT(GetFlagPreference_Func);
        GWCA_ASSERT(SetStringPreference_Func);
        GWCA_ASSERT(SetEnumPreference_Func);
        GWCA_ASSERT(SetNumberPreference_Func);
        GWCA_ASSERT(SetFlagPreference_Func);
        GWCA_ASSERT(WorldMapState_Addr);
        GWCA_ASSERT(SendFrameUIMessage_Func);
        GWCA_ASSERT(SendUIMessage_Func);
        GWCA_ASSERT(LoadSettings_Func);
        GWCA_ASSERT(ui_drawn_addr);
        GWCA_ASSERT(shift_screen_addr);
        GWCA_ASSERT(SetTooltip_Func);
        GWCA_ASSERT(CurrentTooltipPtr);
        GWCA_ASSERT(GameSettings_Addr);
        GWCA_ASSERT(SetWindowVisible_Func);
        GWCA_ASSERT(SetWindowPosition_Func);
        GWCA_ASSERT(window_positions_array);
        GWCA_ASSERT(ValidateAsyncDecodeStr);
        GWCA_ASSERT(AsyncDecodeStringPtr);
        GWCA_ASSERT(SetVolume_Func);
        GWCA_ASSERT(SetMasterVolume_Func);
        GWCA_ASSERT(DrawOnCompass_Func);
        GWCA_ASSERT(CreateUIComponent_Func);
        GWCA_ASSERT(EnumPreferenceOptions_Addr);
        GWCA_ASSERT(NumberPreferenceOptions_Addr);
        GWCA_ASSERT(SetInGameStaticPreference_Func);
        GWCA_ASSERT(SetInGameUIScale_Func);
        GWCA_ASSERT(PreferencesInitialised_Addr);
        GWCA_ASSERT(GetRootFrame_Func);

        GWCA_ASSERT(GetGraphicsRendererValue_Func);
        GWCA_ASSERT(SetGraphicsRendererValue_Func);
        GWCA_ASSERT(SetGameRendererMode_Func);
        GWCA_ASSERT(GetGameRendererMetric_Func);
#endif

        if(SendUIMessage_Func)
            HookBase::CreateHook((void**)&SendUIMessage_Func, OnSendUIMessage, (void **)&RetSendUIMessage);
        if(CreateUIComponent_Func)
            HookBase::CreateHook((void**)&CreateUIComponent_Func, OnCreateUIComponent, (void**)&CreateUIComponent_Ret);
        if(SendFrameUIMessage_Func)
            HookBase::CreateHook((void**)&SendFrameUIMessage_Func, OnSendFrameUIMessage, (void**)&SendFrameUIMessage_Ret);
        if(SendFrameUIMessageById_Func)
            HookBase::CreateHook((void**)&SendFrameUIMessageById_Func, OnSendFrameUIMessageById, (void**)&SendFrameUIMessageById_Ret);
        if(DrawOnCompass_Func)
            HookBase::CreateHook((void**)&DrawOnCompass_Func, OnDrawOnCompass, (void**)&DrawOnCompass_Ret);
    }

    void EnableHooks() {
        if (AsyncDecodeStringPtr)
            HookBase::EnableHooks(AsyncDecodeStringPtr);
        if (SetTooltip_Func)
            HookBase::EnableHooks(SetTooltip_Func);
        if (SendUIMessage_Func)
            HookBase::EnableHooks(SendUIMessage_Func);
        if (CreateUIComponent_Func)
            HookBase::EnableHooks(CreateUIComponent_Func);
        if (DrawOnCompass_Func)
            HookBase::EnableHooks(DrawOnCompass_Func);
        if (SendFrameUIMessage_Func)
            HookBase::EnableHooks(SendFrameUIMessage_Func);
        if (SendFrameUIMessageById_Func)
            HookBase::EnableHooks(SendFrameUIMessageById_Func);
        UI::RegisterUIMessageCallback(&open_template_hook, UI::UIMessage::kOpenTemplate, OnOpenTemplate_UIMessage);
    }
    void DisableHooks() {
        UI::RemoveUIMessageCallback(&open_template_hook);
        if (DrawOnCompass_Func)
            HookBase::DisableHooks(DrawOnCompass_Func);
        if (AsyncDecodeStringPtr)
            HookBase::DisableHooks(AsyncDecodeStringPtr);
        if (SetTooltip_Func)
            HookBase::DisableHooks(SetTooltip_Func);
        if (SendUIMessage_Func)
            HookBase::DisableHooks(SendUIMessage_Func);
        if (CreateUIComponent_Func)
            HookBase::DisableHooks(CreateUIComponent_Func);
        if (SendFrameUIMessage_Func)
            HookBase::DisableHooks(SendFrameUIMessage_Func);
        if (SendFrameUIMessageById_Func)
            HookBase::DisableHooks(SendFrameUIMessageById_Func);
    }

    void Exit()
    {
        HookBase::RemoveHook(DrawOnCompass_Func);
        HookBase::RemoveHook(AsyncDecodeStringPtr);
        HookBase::RemoveHook(SetTooltip_Func);
        HookBase::RemoveHook(SendUIMessage_Func);
        HookBase::RemoveHook(CreateUIComponent_Func);
        HookBase::RemoveHook(SendFrameUIMessage_Func);
        HookBase::RemoveHook(SendFrameUIMessageById_Func);
    }

    bool PrefsInitialised() {
        return PreferencesInitialised_Addr && *(uint32_t*)PreferencesInitialised_Addr == 1;
    }

    UI::Frame* GetButtonActionFrame() {
        return UI::GetChildFrame(UI::GetFrameByLabel(L"Game"),6);
    }

    bool IsFrameValid(UI::Frame* frame) {
        return frame && (int)frame != -1;
    }

#define TERM_FINAL          (0x0000)
#define TERM_INTERMEDIATE   (0x0001)
#define CONCAT_CODED        (0x0002)
#define CONCAT_LITERAL      (0x0003)
#define STRING_CHAR_FIRST   (0x0010)
#define WORD_VALUE_BASE     (0x0100)
#define WORD_BIT_MORE       (0x8000)
#define WORD_VALUE_RANGE    (WORD_BIT_MORE - WORD_VALUE_BASE)

    bool EncChr_IsControlCharacter(wchar_t c) {
        return c == TERM_FINAL || c == TERM_INTERMEDIATE || c == CONCAT_CODED || c == CONCAT_LITERAL;
    }

    bool EncChr_IsParam(wchar_t c) {
        return (c >= 0x101 && c <= 0x10f);
    }

    bool EncChr_IsParamSegment(wchar_t c) {
        return (c >= 0x10a && c <= 0x10c);
    }

    bool EncChr_IsParamLiteral(wchar_t c) {
        return (c >= 0x107 && c <= 0x109);
    }

    bool EncChr_IsParamNumeric(wchar_t c) {
        return EncChr_IsParam(c) && !EncChr_IsParamLiteral(c) && !EncChr_IsParamSegment(c);
    }

    // Accepts a sequence of literal string characters, terminated with TERM_INTERMEDIATE
    bool EncStr_ValidateTerminatedLiteral(const wchar_t*& data, const wchar_t* term) {
        while (data < term) {
            wchar_t c = *data++;

            // Skip until we reach a control character.  It terminates correctly if
            // that character is TERM_INTERMEDIATE, otherwise the string is invalid.
            if (c < STRING_CHAR_FIRST) {
                return (c == TERM_INTERMEDIATE);
            }
        }

        return false;
    }

    // Accepts a possibly-multibyte word
    bool EncStr_ValidateSingleWord(const wchar_t*& data, const wchar_t* term) {
        wchar_t c;

        do {
            c = *data++;
            if ((c & ~WORD_BIT_MORE) < WORD_VALUE_BASE) {
                return false;
            }
        } while (c & WORD_BIT_MORE);

        return (data < term);
    }

    // Accepts a possibly-multibyte word, optionally followed by a multibyte word
    bool EncStr_ValidateWord(const wchar_t*& data, const wchar_t* term) {
        if (!EncStr_ValidateSingleWord(data, term)) {
            return false;
        }

        // Lookahead - is there a multibyte word immediately after?
        // If not, exit now before consuming the character.
        if (!(*data & WORD_BIT_MORE)) {
            return true;
        }

        return EncStr_ValidateSingleWord(data, term);
    }

    bool EncStr_Validate(const wchar_t*& data, const wchar_t* term) {
        bool isFirstLoop = true;

        while (data < term) {
            wchar_t c;  // Do not increment here - the first control character is technically optional

            // Diversion from GW code.  GW's validator loop starts by accepts an EncStr starting with
            // a control character, but that later crashes string decoding.  As there is no control character
            // at the start of a string, but there should always be a control character following a word,
            // I have changed it to make it required, but skipped in the first loop iteration;
            if (!isFirstLoop) {
                c = *data++;

                if (c == TERM_FINAL) {
                    return (data == term);
                }
                if (c == TERM_INTERMEDIATE) {
                    // We should only reach here from a recursive call via an EncString parameter
                    // provided for a string substitution.
                    return (data < term);
                }
                if (c == CONCAT_LITERAL) {
                    if (EncStr_ValidateTerminatedLiteral(data, term)) {
                        continue;
                    }
                    else {
                        return false;
                    }
                }
                // if (c == CONCAT_CODED) { /* do nothing - we already consumed the control character */ }
            }

            if (!EncStr_ValidateWord(data, term)) {
                return false;
            }

            // At this point we want to lookahead so that we don't consume a potential CONCAT_LITERAL
            // control character, which should be consumed by the next loop iteration
            while (data < term && !EncChr_IsControlCharacter(*data)) {
                c = *data++;

                if (EncChr_IsParam(c)) {
                    if (EncChr_IsParamLiteral(c)) {
                        if (!EncStr_ValidateTerminatedLiteral(data, term))  {
                            return false;
                        }
                    }
                    else if(EncChr_IsParamSegment(c)) {
                        // EncStr parameter, recurse into this function
                        if (!EncStr_Validate(data, term)) {
                            return false;
                        }
                    }
                    else if(EncChr_IsParamNumeric(c)) {
                        if (!EncStr_ValidateSingleWord(data, term)) {
                            return false;
                        }

                        // Numeric parameters are "fixed length" (ish) and so
                        // are NOT terminated by TERM_INTERMEDIATE.
                    }
                    else {
                        GWCA_ASSERT("Invalid case reached: IsParam but not any IsParamType");
                        return false;
                    }
                }
            }

            // Here, the guild wars code also handles TERM_FINAL and TERM_INTERMEDIATE, but it
            // is identical to the start of the next loop so that is omitted in favour of fallthrough.

            isFirstLoop = false;
        }

        // If the loop exited by data going past the end of the EncStr, it overflowed and
        // validation should fail.
        return false;
    }
}

namespace GW {

    Module UIModule = {
        "UIModule",     // name
        NULL,           // param
        ::Init,         // init_module
        ::Exit,         // exit_module
        ::EnableHooks,           // enable_hooks
        ::DisableHooks,           // disable_hooks
    };
    namespace UI {
        GWCA_API GW::Constants::Language GetTextLanguage()
        {
            return (GW::Constants::Language)GW::UI::GetPreference(GW::UI::NumberPreference::TextLanguage);
        }
        GWCA_API bool ButtonClick(Frame* btn_frame)
        {
            if (!(btn_frame && btn_frame->IsCreated())) {
                return false; // Not yet created
            }

            const auto parent_frame = GW::UI::GetParentFrame(btn_frame);
            if (!(parent_frame && parent_frame->IsCreated())) { // frame->state.Test(FRAME_STATE_CREATED)
                return false; // Not yet created
            }

            GW::UI::UIPacket::kMouseAction action{};
            
            action.child_frame_id_dupe = action.child_frame_id = btn_frame->child_offset_id;
            struct button_param {
                uint32_t unk;
                uint32_t wparam;
                uint32_t lparam;
            };
            button_param wparam = { 0, btn_frame->field100_0x1a8,0 };
            action.wparam = &wparam;
            action.current_state = 0x6;

            return SendFrameUIMessage(parent_frame, GW::UI::UIMessage::kMouseClick2, &action);
        }
        Frame* FrameRelation::GetFrame() {
            const auto frame = (Frame*)((uintptr_t)this - offsetof(struct Frame, relation));
            GWCA_ASSERT(&frame->relation == this);
            return frame;
        }
        Frame* FrameRelation::GetParent() const
        {
            return parent ? parent->GetFrame() : nullptr;
        }

        GW::Vec2f FramePosition::GetTopLeftOnScreen(const Frame* frame) const
        {
            const auto viewport_scale = GetViewportScale(frame);
            const auto height = frame ? frame->position.viewport_height : viewport_height;
            return {
                screen_left * viewport_scale.x,
                (height - screen_top) * viewport_scale.y
            };
        }
        GW::Vec2f FramePosition::GetBottomRightOnScreen(const Frame* frame) const
        {
            const auto viewport_scale = GetViewportScale(frame);
            const auto height = frame ? frame->position.viewport_height : viewport_height;
            return {
                screen_right * viewport_scale.x,
                (height - screen_bottom) * viewport_scale.y
            };
        }
        GW::Vec2f FramePosition::GetContentTopLeft(const Frame* frame) const
        {
            const auto viewport_scale = GetViewportScale(frame);
            const auto height = frame ? frame->position.viewport_height : viewport_height;
            return {
                content_left * viewport_scale.x,
                (height - content_top) * viewport_scale.y
            };
        }
        GW::Vec2f FramePosition::GetContentBottomRight(const Frame* frame) const
        {
            const auto viewport_scale = GetViewportScale(frame);
            const auto height = frame ? frame->position.viewport_height : viewport_height;
            return {
                content_right * viewport_scale.x,
                (height - content_bottom) * viewport_scale.y
            };
        }
        GW::Vec2f FramePosition::GetSizeOnScreen(const Frame* frame) const
        {
            const auto viewport_scale = GetViewportScale(frame);
            return {
                (screen_right - screen_left) * viewport_scale.x,
                (screen_top - screen_bottom) * viewport_scale.y,
            };
        }

        GW::Vec2f FramePosition::GetViewportScale(const Frame* frame) const
        {
            const auto screen_width = static_cast<float>(GW::Render::GetViewportWidth());
            const auto screen_height = static_cast<float>(GW::Render::GetViewportHeight());
            return {
                screen_width / (frame ? frame->position.viewport_width : viewport_width),
                screen_height / (frame ? frame->position.viewport_height :viewport_height)
             };
        }

        Frame* GetRootFrame() {
            return GetRootFrame_Func ? GetRootFrame_Func() : nullptr;
        }
        Frame* GetChildFrame(Frame* parent, uint32_t child_offset) {
            if (!(GetChildFrameId_Func && parent))
                return nullptr;
            const auto found_id = GetChildFrameId_Func(parent->frame_id, child_offset);
            return GetFrameById(found_id);
        }

        Frame* GetParentFrame(Frame* frame) {
            return frame ? frame->relation.GetParent() : nullptr;
        }

        Frame* GetChildFrame(Frame* parent, const std::initializer_list<uint32_t> child_offsets)
        {
            auto id = parent->frame_id;
            for (uint32_t child_offset : child_offsets) {
                id = GetChildFrameId_Func(id, child_offset);
            }
            return GetFrameById(id);
        }

        Frame* GetFrameById(uint32_t frame_id) {
            if (!(s_FrameArray && s_FrameArray->size() > frame_id))
                return nullptr;
            auto frame = (*s_FrameArray)[frame_id];
            return IsFrameValid(frame) ? frame : nullptr;
        }
        Frame* GetFrameByLabel(const wchar_t* frame_label) {
            if (!(CreateHashFromWchar_Func && s_FrameArray))
                return nullptr;
            const auto hash = CreateHashFromWchar_Func(frame_label, -1);
            for (auto frame : *s_FrameArray) {
                if (!IsFrameValid(frame))
                    continue;
                if (frame->relation.frame_hash_id == hash)
                    return frame;
            }
            return nullptr;
        }

        Vec2f WindowPosition::xAxis(float multiplier, const bool clamp_position) const {
            Vec2f x;
            const auto w = static_cast<float>(Render::GetViewportWidth());
            const auto middle = w / 2.f;
            switch (state ^ 0x1) {
                case 0x10:
                case 0x18:
                case 0x30:
                    x = { std::roundf(w - p1.x * multiplier), std::roundf(w - p2.x * multiplier) };
                    break;
                case 0x8:
                case 0x20:
                case 0x0:
                    x = { std::roundf(middle - p1.x * multiplier), std::roundf(middle + p2.x * multiplier) };
                    break;
                default:
                    x =  {std::roundf(p1.x * multiplier), std::roundf(p2.x * multiplier)};
                    break;
            }

            if (clamp_position) {
                x.x = std::max(0.0f, x.x);
                x.x = std::min(x.x, w - width(multiplier));
                x.y = std::min(w, x.y);
                x.y = std::max(x.y, width(multiplier));
            }
            return x;
        }

        Vec2f WindowPosition::yAxis(float multiplier, const bool clamp_position) const {
            const auto h = static_cast<float>(Render::GetViewportHeight());
            Vec2f y;
            switch (state ^ 0x1) {
                case 0x20:
                case 0x24:
                case 0x30:
                    y = { h - p1.y * multiplier, h - p2.y * multiplier };
                break;
                case 0x4:
                case 0x10:
                case 0x0:
                    y = { (h / 2.f) - p1.y * multiplier, (h / 2.f) + p2.y * multiplier };
                break;
                default:
                    y = { p1.y * multiplier, p2.y * multiplier };
                break;
            }

            if (clamp_position) {
                y.x = std::max(0.0f, y.x);
                y.x = std::min(y.x, h - height(multiplier));
                y.y = std::min(h, y.y);
                y.y = std::max(y.y, height(multiplier));
            }
            return y;
        }

        bool RawSendUIMessage(UIMessage msgid, void* wParam, void* lParam) {
            if (!RetSendUIMessage)
                return false;
            if (((uint32_t)msgid & 0x30000000) == 0x30000000)
                return true; // Internal GWCA UI Message, used for hooks
            HookBase::EnterHook();
            RetSendUIMessage(msgid, wParam, lParam);
            HookBase::LeaveHook();
            return true;
        }

        bool SendFrameUIMessage(Frame* frame, UIMessage message_id, void* wParam, void* lParam)
        {
            if (!(SendFrameUIMessage_Ret && frame && frame->frame_callbacks.size()))
                return false;

            const auto& found = FrameUIMessage_callbacks.find(message_id);
            if (found == FrameUIMessage_callbacks.end()) {
                HookBase::EnterHook();
                SendFrameUIMessage_Ret(&frame->frame_callbacks, nullptr, message_id, wParam, lParam);
                HookBase::LeaveHook();
                return true;
            }

            HookStatus status;
            auto it = found->second.begin();
            const auto& end = found->second.end();
            // Pre callbacks
            while (it != end) {
                if (it->altitude > 0)
                    break;
                it->callback(&status, frame, message_id, wParam, lParam);
                ++status.altitude;
                it++;
            }

            const bool result = !status.blocked;
            if (result) {
                HookBase::EnterHook();
                SendFrameUIMessage_Ret(&frame->frame_callbacks, nullptr, message_id, wParam, lParam);
                HookBase::LeaveHook();
            }

            // Post callbacks
            while (it != end) {
                it->callback(&status, frame, message_id, wParam, lParam);
                ++status.altitude;
                it++;
            }
            return result;

        }

        bool SendUIMessage(UIMessage msgid, void* wParam, void* lParam, bool skip_hooks)
        {
            HookStatus status;
            if (skip_hooks) {
                return RawSendUIMessage(msgid, wParam, lParam);
            }
            const auto& found = UIMessage_callbacks.find(msgid);
            if (found == UIMessage_callbacks.end()) {
                return RawSendUIMessage(msgid, wParam, lParam);
            }

            auto it = found->second.begin();
            auto end = found->second.end();
            // Pre callbacks
            while (it != end) {
                if (it->altitude > 0)
                    break;
                it->callback(&status, msgid, wParam, lParam);
                ++status.altitude;
                it++;
            }

            const bool result = !status.blocked && RawSendUIMessage(msgid, wParam, lParam);

            // Post callbacks
            while (it != end) {
                it->callback(&status, msgid, wParam, lParam);
                ++status.altitude;
                it++;
            }
            return result;
        }
        bool Keydown(ControlAction key, Frame* frame) {
            GW::UI::UIPacket::kKeyAction action;
            action.gw_key = key;
            return SendFrameUIMessage(frame ? frame : GetButtonActionFrame(), UI::UIMessage::kKeyDown, &action);
        }
        bool Keyup(ControlAction key, Frame* frame) {
            GW::UI::UIPacket::kKeyAction action;
            action.gw_key = key;
            return SendFrameUIMessage(frame ? frame : GetButtonActionFrame(), UI::UIMessage::kKeyUp, &action);
        }

        bool SetWindowVisible(WindowID window_id,bool is_visible) {
            if (!SetWindowVisible_Func || window_id >= WindowID::WindowID_Count)
                return false;
            SetWindowVisible_Func(window_id, is_visible ? 1u : 0u, 0, 0);
            return true;
        }
        bool SetWindowPosition(WindowID window_id, WindowPosition* info) {
            if (!SetWindowPosition_Func || window_id >= WindowID::WindowID_Count)
                return false;
            SetWindowPosition_Func(window_id, info, 0, 0);
            return true;
        }
        WindowPosition* GetWindowPosition(WindowID window_id) {
            if (!window_positions_array || window_id >= WindowID::WindowID_Count)
                return nullptr;
            return &window_positions_array[window_id];
        }

        bool Keypress(ControlAction key, Frame* frame) {
            if (!Keydown(key, frame))
                return false;
            GW::GameThread::Enqueue([key, frame] {
                Keyup(key, frame);
                });
            return true;
        }
        
        bool DrawOnCompass(unsigned session_id, unsigned pt_count, CompassPoint* pts)
        {
            if (!DrawOnCompass_Func)
                return false;
            DrawOnCompass_Func(session_id, pt_count, pts);
            return true;
        }

        void LoadSettings(size_t size, uint8_t *data) {
            if (Verify(LoadSettings_Func))
                LoadSettings_Func(size, data);
        }

        ArrayByte* GetSettings() {
            return (ArrayByte *)GameSettings_Addr;
        }

        bool GetIsUIDrawn() {
            uint32_t *ui_drawn = (uint32_t *)ui_drawn_addr;
            if (Verify(ui_drawn))
                return (*ui_drawn == 0);
            else
                return true;
        }
        bool GetIsWorldMapShowing() {
            uint32_t* WorldMapState = (uint32_t*)WorldMapState_Addr;
            if (Verify(WorldMapState))
                return (*WorldMapState & 0x80000) != 0;
            else
                return false;
        }

        bool GetIsShiftScreenShot() {
            uint32_t *shift_screen = (uint32_t *)shift_screen_addr;
            if (Verify(shift_screen))
                return (*shift_screen != 0);
            else
                return false;
        }

        void AsyncDecodeStr(const wchar_t* enc_str, char* buffer, size_t size) {
            // @Enhancement: Should use a pool of this buffer, but w/e for now
            AsyncBuffer* abuf = new AsyncBuffer;
            abuf->buffer = buffer;
            abuf->size = size;
            return AsyncDecodeStr(enc_str, __callback_copy_char, abuf);
        }

        void AsyncDecodeStr(const wchar_t *enc_str, wchar_t *buffer, size_t size) {
            // @Enhancement: Should use a pool of this buffer, but w/e for now
            AsyncBuffer *abuf = new AsyncBuffer;
            abuf->buffer = buffer;
            abuf->size = size;
            return AsyncDecodeStr(enc_str, __callback_copy_wchar, abuf);
        }

        void AsyncDecodeStr(const wchar_t* enc_str, DecodeStr_Callback callback, void* callback_param, GW::Constants::Language language_id) {
            if (!(ValidateAsyncDecodeStr && enc_str)) {
                callback(callback_param, L"");
                return;
            }

            if (!IsValidEncStr(enc_str)) {
                std::string invalid_str = "Invalid enc str: ";
                char buf[8];
                for (size_t i = 0; i < wcslen(enc_str); i++) {
                    snprintf(buf, _countof(buf), " %#06x", enc_str[i]);
                    invalid_str += buf;
                }
                GWCA_WARN(invalid_str.c_str());
                callback(callback_param, L"!!!");
                return;
            }

            auto& textParser = GetGameContext()->text_parser;
            const auto prev_language_id = textParser->language_id;
            if (language_id != GW::Constants::Language::Unknown) {
                textParser->language_id = language_id;
            }
            ValidateAsyncDecodeStr((wchar_t*)enc_str, callback, callback_param);
            textParser->language_id = prev_language_id;
        }

        void AsyncDecodeStr(const wchar_t *enc_str, std::wstring *out, GW::Constants::Language language_id) {
            return AsyncDecodeStr(enc_str, __calback_copy_wstring, out, language_id);
        }

        bool IsValidEncStr(const wchar_t* enc_str) {
            if (!enc_str)
                return false;
            // The null terminator is considered part of the EncString, so include it in calculating the EncString end position
            const wchar_t* term = enc_str + wcslen(enc_str) + 1;
            const wchar_t* data = enc_str;

            if (!EncStr_Validate(data, term)) {
                return false;
            }

            return data == term;
        }

        bool UInt32ToEncStr(uint32_t value, wchar_t *buffer, size_t count) {
            // Each "case" in the array of wchar_t contains a value in the range [0, WORD_VALUE_RANGE)
            // This value is offseted by WORD_VALUE_BASE and if it take more than 1 "case" it set the bytes WORD_BIT_MORE
            const int case_required = static_cast<int>((value + WORD_VALUE_RANGE - 1) / WORD_VALUE_RANGE);
            if (case_required + 1 > static_cast<int>(count))
                return false;
            buffer[case_required] = 0;
            for (int i = case_required - 1; i >= 0; i--) {
                buffer[i] = WORD_VALUE_BASE + (value % WORD_VALUE_RANGE);
                value /= WORD_VALUE_RANGE;
                if (i != case_required - 1)
                    buffer[i] |= WORD_BIT_MORE;
            }
            return true;
        }

        uint32_t EncStrToUInt32(const wchar_t *enc_str) {
            uint32_t val = 0;
            do {
                GWCA_ASSERT(*enc_str >= WORD_VALUE_BASE);
                val *= WORD_VALUE_RANGE;
                val += (*enc_str & ~WORD_BIT_MORE) - WORD_VALUE_BASE;
            } while (*enc_str++ & WORD_BIT_MORE);
            return val;
        }

        void SetOpenLinks(bool toggle)
        {
            open_links = toggle;
        }

        uint32_t GetPreference(EnumPreference pref)
        {
            return GetEnumPreference_Func && PrefsInitialised() && pref < EnumPreference::Count ? GetEnumPreference_Func((uint32_t)pref) : 0;
        }
        uint32_t GetPreferenceOptions(EnumPreference pref, uint32_t** options_out)
        {
            if (!(EnumPreferenceOptions_Addr && pref < EnumPreference::Count))
                return 0;
            const auto& info = EnumPreferenceOptions_Addr[(uint32_t)pref];
            if (options_out)
                *options_out = info.options;
            return info.options_count;
        }
        uint32_t ClampPreference(NumberPreference pref, uint32_t value) {
            if (!(NumberPreferenceOptions_Addr && PrefsInitialised() && pref < NumberPreference::Count))
                return value;
            const auto& info = NumberPreferenceOptions_Addr[(uint32_t)pref];
            if ((info.flags & 0x1) != 0 && info.clampProc)
                return info.clampProc((uint32_t)pref,value);
            return value;
        }
        uint32_t GetPreference(NumberPreference pref)
        {
            return GetNumberPreference_Func && PrefsInitialised() && pref < NumberPreference::Count ? GetNumberPreference_Func((uint32_t)pref) : 0;
        }
        wchar_t* GetPreference(StringPreference pref)
        {
            return GetStringPreference_Func && PrefsInitialised() && pref < StringPreference::Count ? GetStringPreference_Func((uint32_t)pref) : 0;
        }
        bool GetPreference(FlagPreference pref)
        {
            return GetFlagPreference_Func && PrefsInitialised() && pref < FlagPreference::Count ? GetFlagPreference_Func((uint32_t)pref) : 0;
        }
        bool SetPreference(EnumPreference pref, uint32_t value)
        {
            if (!(SetEnumPreference_Func && PrefsInitialised() && GetEnumPreference_Func && pref < EnumPreference::Count))
                return false;
            if (!GameThread::IsInGameThread()) {
                // NB: Setting preferences triggers UI message 0x10000013f - make sure its run on the game thread!
                GameThread::Enqueue([pref, value] { SetPreference(pref, value);});
                return true;
            }
            uint32_t* opts = 0;
            uint32_t opts_count = GetPreferenceOptions(pref, &opts);
            size_t i = 0;
            while (i < opts_count) {
                if (opts[i] == value)
                    break;
                i++;
            }
            if(i == opts_count)
                return false; // Invalid enum value
           
            // Extra validation; technically these options are available but aren't valid for these enums.
            // Also triggers renderer update if applicable.
            
            switch (pref) {
            case EnumPreference::AntiAliasing:
                if (value == 2)
                    value = 1;
                break;
            case EnumPreference::TerrainQuality:
            case EnumPreference::ShaderQuality:
                if (value == 0)
                    value = 1;
                break;
            }

            SetEnumPreference_Func((uint32_t)pref, value);

            // Post preference re rendering etc. Run on render loop to avoid issues.
            GameThread::Enqueue([pref] {
                uint32_t value = GetPreference(pref);
                switch (pref) {
                case EnumPreference::AntiAliasing:
                    SetGraphicsRendererValue_Func(0, 2, 5, value);
                    SetGraphicsRendererValue_Func(0, 0, 5, value);
                    break;
                case EnumPreference::ShaderQuality:
                    SetGraphicsRendererValue_Func(0, 2, 9, value);
                    SetGraphicsRendererValue_Func(0, 0, 9, value);
                    break;
                case EnumPreference::ShadowQuality:
                    SetInGameShadowQuality_Func(value);
                    break;
                case EnumPreference::TerrainQuality:
                    SetInGameStaticPreference_Func(2, value);
                    TriggerTerrainRerender_Func();
                    break;
                case EnumPreference::Reflections:
                    SetInGameStaticPreference_Func(1, value);
                    break;
                case EnumPreference::InterfaceSize:
                    SetInGameUIScale_Func(value);
                    break;
                }
                });

            
            return true;
        }
        bool SetPreference(NumberPreference pref, uint32_t value)
        {
            if (!PrefsInitialised())
                return false;
            if (!GameThread::IsInGameThread()) {
                // NB: Setting preferences triggers UI message 0x10000013f - make sure its run on the game thread!
                GameThread::Enqueue([pref, value] { SetPreference(pref, value);});
                return true;
            }
            value = ClampPreference(pref, value); // Clamp here to avoid assertion error later.
            bool ok = SetNumberPreference_Func && pref < NumberPreference::Count ? SetNumberPreference_Func((uint32_t)pref, value), true : false;
            if (!ok)
                return ok;
            // Post preference re rendering etc. Run on render loop to avoid issues.
            GameThread::Enqueue([pref]() {
                uint32_t value = GetPreference(pref);
                switch (pref) {
                case NumberPreference::EffectsVolume:
                    if (SetVolume_Func) SetVolume_Func(0, (float)value / 100.f);
                    break;
                case NumberPreference::DialogVolume:
                    if (SetVolume_Func) SetVolume_Func(4, (float)value / 100.f);
                    break;
                case NumberPreference::BackgroundVolume:
                    if (SetVolume_Func) SetVolume_Func(1, (float)value / 100.f);
                    break;
                case NumberPreference::MusicVolume:
                    if (SetVolume_Func) SetVolume_Func(3, (float)value / 100.f);
                    break;
                case NumberPreference::UIVolume:
                    if (SetVolume_Func) SetVolume_Func(2, (float)value / 100.f);
                    break;
                case NumberPreference::MasterVolume:
                    if (SetMasterVolume_Func) SetMasterVolume_Func((float)value / 100.f);
                    break;
                case NumberPreference::FullscreenGamma:
                    SetGraphicsRendererValue_Func(0, 2, 0x4, value);
                    SetGraphicsRendererValue_Func(0, 0, 0x4, value);
                    break;
                case NumberPreference::TextureQuality:
                    SetGraphicsRendererValue_Func(0, 2, 0xd, value);
                    SetGraphicsRendererValue_Func(0, 0, 0xd, value);
                    break;
                case NumberPreference::RefreshRate:
                    SetGraphicsRendererValue_Func(0, 2, 0x8, value);
                    SetGraphicsRendererValue_Func(0, 0, 0x8, value);
                    break;
                case NumberPreference::UseBestTextureFiltering:
                    SetGraphicsRendererValue_Func(0, 2, 0xc, value);
                    SetGraphicsRendererValue_Func(0, 0, 0xc, value);
                    break;
                case NumberPreference::ScreenBorderless:
                    SetGraphicsRendererValue_Func(0, 2, 0x10, value);
                    break;
                case NumberPreference::WindowPosX:
                    SetGraphicsRendererValue_Func(0, 2, 6, value);
                    break;
                case NumberPreference::WindowPosY:
                    SetGraphicsRendererValue_Func(0, 2, 7, value);
                    break;
                case NumberPreference::WindowSizeX:
                    SetGraphicsRendererValue_Func(0, 2, 0xa, value);
                    break;
                case NumberPreference::WindowSizeY:
                    SetGraphicsRendererValue_Func(0, 2, 0xb, value);
                    break;
                case NumberPreference::ScreenSizeX:
                    SetGraphicsRendererValue_Func(0, 0, 0xa, value);
                    break;
                case NumberPreference::ScreenSizeY:
                    SetGraphicsRendererValue_Func(0, 0, 0xb, value);
                    break;
                default:
                    break;
                }
                });

            return ok;
        }
        bool SetPreference(StringPreference pref, wchar_t* value)
        {
            if (!(SetStringPreference_Func && PrefsInitialised() && pref < StringPreference::Count))
                return false;
            if (!GameThread::IsInGameThread()) {
                // NB: Setting preferences triggers UI message 0x10000013f - make sure its run on the game thread!
                GameThread::Enqueue([pref, value] { SetPreference(pref, value);});
                return true;
            }
            SetStringPreference_Func((uint32_t)pref, value);
            return true;
        }
        bool SetPreference(FlagPreference pref, bool value)
        {
            if (!(SetFlagPreference_Func && PrefsInitialised() && pref < FlagPreference::Count))
                return false;
            if (!GameThread::IsInGameThread()) {
                // NB: Setting preferences triggers UI message 0x10000013f - make sure its run on the game thread!
                GameThread::Enqueue([pref, value] { SetPreference(pref, value); });
                return true;
            }
            SetFlagPreference_Func((uint32_t)pref, value);
            switch (pref) {
                case UI::FlagPreference::IsWindowed: {
                    uint32_t pref_value = value ? 2 : 0;
                    uint32_t renderer_value = GetGameRendererMode_Func(0);
                    if (pref_value != renderer_value)
                        SetGameRendererMode_Func(0, pref_value);
                }

            }

            return true;
        }
        uint32_t GetFrameLimit() {
            uint32_t frame_limit = CommandLineNumber_Buffer ? CommandLineNumber_Buffer[(uint32_t)NumberCommandLineParameter::FPS] : 0;
            uint32_t vsync_enabled = GetGraphicsRendererValue_Func(0, 0xf);
            uint32_t monitor_refresh_rate = GetGraphicsRendererValue_Func(0, 0x16);
            if (!frame_limit) {
                switch (GetPreference(EnumPreference::FrameLimiter)) {
                case 1: // 30 fps
                    frame_limit = 30;
                    break;
                case 2: // 60 fps
                    frame_limit = 60;
                    break;
                case 3: // monitor refresh rate
                    frame_limit = monitor_refresh_rate;
                    break;
                }
            }
            if (vsync_enabled && monitor_refresh_rate && frame_limit > monitor_refresh_rate)
                frame_limit = monitor_refresh_rate; // Can't have higher fps than the monitor refresh rate with vsync
            return frame_limit;
        }
        bool SetFrameLimit(uint32_t value) {
            return CommandLineNumber_Buffer ? CommandLineNumber_Buffer[(uint32_t)NumberCommandLineParameter::FPS] = value, true : false;
        }

        void RegisterKeyupCallback(HookEntry* entry, const KeyCallback& callback) {
            RegisterFrameUIMessageCallback(entry, UIMessage::kKeyUp, [callback](GW::HookStatus* status, const Frame*, UIMessage, void* wParam, void*) {
                callback(status, *(uint32_t*)wParam);
                });
        }
        void RemoveKeyupCallback(HookEntry* entry) {
            RemoveFrameUIMessageCallback(entry);
        }

        void RegisterKeydownCallback(HookEntry* entry, const KeyCallback& callback) {
            RegisterFrameUIMessageCallback(entry, UIMessage::kKeyDown, [callback](GW::HookStatus* status, const Frame*, UIMessage, void* wParam, void*) {
                callback(status, *(uint32_t*)wParam);
                });
        }
        void RemoveKeydownCallback(HookEntry* entry) {
            RemoveFrameUIMessageCallback(entry);
        }

        void RegisterFrameUIMessageCallback(
            HookEntry *entry,
            UIMessage message_id,
            const FrameUIMessageCallback& callback,
            int altitude)
        {
            if (FrameUIMessage_callbacks.find(message_id) == FrameUIMessage_callbacks.end()) {
                FrameUIMessage_callbacks[message_id] = std::vector<FrameCallbackEntry>();
            }
            auto it = FrameUIMessage_callbacks[message_id].begin();
            while (it != FrameUIMessage_callbacks[message_id].end()) {
                if (it->altitude > altitude)
                    break;
                it++;
            }
            FrameUIMessage_callbacks[message_id].insert(it, { altitude, entry, callback});
        }
        void RemoveFrameUIMessageCallback(
            HookEntry *entry)
        {
            for (auto& it : FrameUIMessage_callbacks) {
                auto it2 = it.second.begin();
                while (it2 != it.second.end()) {
                    if (it2->entry == entry) {
                        it.second.erase(it2);
                        break;
                    }
                    it2++;
                }
            }
        }
        void RegisterUIMessageCallback(
            HookEntry *entry,
            UIMessage message_id,
            const UIMessageCallback& callback,
            int altitude)
        {
            RemoveUIMessageCallback(entry, message_id);
            if (UIMessage_callbacks.find(message_id) == UIMessage_callbacks.end()) {
                UIMessage_callbacks[message_id] = std::vector<CallbackEntry>();
            }
            auto it = UIMessage_callbacks[message_id].begin();
            while (it != UIMessage_callbacks[message_id].end()) {
                if (it->altitude > altitude)
                    break;
                it++;
            }
            UIMessage_callbacks[message_id].insert(it, { altitude, entry, callback});
        }

        void RemoveUIMessageCallback(HookEntry *entry, UIMessage message_id)
        {
            if (message_id == UIMessage::kNone) {
                for (auto& it : UIMessage_callbacks) {
                    RemoveUIMessageCallback(entry, it.first);
                }
            }
            else {
                auto found = UIMessage_callbacks.find(message_id);
                if (found == UIMessage_callbacks.end())
                    return;
                clear_entry:
                auto it2 = found->second.begin();
                while (it2 != found->second.end()) {
                    if (it2->entry == entry) {
                        found->second.erase(it2);
                        goto clear_entry;
                    }
                    it2++;
                }
            }

        }

        TooltipInfo* GetCurrentTooltip() {
            return CurrentTooltipPtr && *CurrentTooltipPtr ? **CurrentTooltipPtr : 0;
        }

        void RegisterCreateUIComponentCallback(HookEntry* entry, const CreateUIComponentCallback& callback, int altitude)
        {
            RemoveCreateUIComponentCallback(entry);
            auto it = OnCreateUIComponent_callbacks.begin();
            while (it != OnCreateUIComponent_callbacks.end()) {
                if (it->altitude > altitude)
                    break;
                it++;
            }
            OnCreateUIComponent_callbacks.insert(it, { altitude, entry, callback});
        }
        void RemoveCreateUIComponentCallback(HookEntry* entry)
        {
            for (auto it = OnCreateUIComponent_callbacks.begin(), end = OnCreateUIComponent_callbacks.end(); it != end; it++) {
                if (it->entry == entry) {
                    OnCreateUIComponent_callbacks.erase(it);
                    return;
                }  
            }
        }
    }

} // namespace GW
