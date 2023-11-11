#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// The "WithHooks" version of the library uses Detour to inject itself upon loading.
#ifdef WITH_HOOKS

#include <wil/resource.h>
#include <wrl.h>

#include <detours.h>

#include "OVRlay.h"

namespace {

#pragma region "Detours utilities"
    template <typename TMethod>
    void DetourDllFunctionAttach(HMODULE dll, const char* target, TMethod hooked, TMethod& original) {
        if (original) {
            // Already hooked.
            return;
        }

        original = (TMethod)GetProcAddress(dll, target);
        if (!original) {
            return;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((PVOID*)&original, hooked);
        DetourTransactionCommit();
    }

    template <typename TMethod>
    void DetourFunctionDetach(TMethod hooked, TMethod& original) {
        if (!original) {
            // Not hooked.
            return;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach((PVOID*)&original, hooked);
        DetourTransactionCommit();
        original = nullptr;
    }
#pragma endregion

    wil::unique_hmodule g_libOVR;
    ovrSession g_ovrSession{nullptr};
    ID3D11Device* g_ovrDevice{nullptr};
    ovrDispatchTable g_dispatchTable{};
    decltype(ovr_GetPredictedDisplayTime)* g_GetPredictedDisplayTime{nullptr};

    decltype(ovr_CreateTextureSwapChainDX)* g_original_CreateTextureSwapChainDX{nullptr};
    ovrResult __cdecl hook_CreateTextureSwapChainDX(ovrSession session,
                                                    IUnknown* d3dPtr,
                                                    const ovrTextureSwapChainDesc* desc,
                                                    ovrTextureSwapChain* out_TextureSwapChain) {
        const ovrResult result = g_original_CreateTextureSwapChainDX(session, d3dPtr, desc, out_TextureSwapChain);
        if (OVR_FAILURE(result)) {
            return result;
        }

        Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
        if (d3dPtr) {
            d3dPtr->QueryInterface(d3dDevice.GetAddressOf());
        }

        // (Re)Initialize OVRlay.
        if (session && session != g_ovrSession && d3dDevice && d3dDevice.Get() != g_ovrDevice) {
            Initialize(session, g_dispatchTable, d3dDevice.Get());
            g_ovrSession = session;
            g_ovrDevice = d3dDevice.Get();
        }

        return result;
    }

    decltype(ovr_GetInputState)* g_original_GetInputState{nullptr};
    ovrResult __cdecl hook_GetInputState(ovrSession session,
                                         ovrControllerType controllerType,
                                         ovrInputState* inputState) {
        const ovrResult result = g_original_GetInputState(session, controllerType, inputState);
        if (OVR_FAILURE(result)) {
            return result;
        }

        if (g_ovrSession) {
            // Block inputs when overlays are focused.
            if (HasFocus()) {
                const ovrControllerType controllerType = inputState->ControllerType;
                const double timeInSeconds = inputState->TimeInSeconds;
                *inputState = {};
                inputState->ControllerType = controllerType;
                inputState->TimeInSeconds = timeInSeconds;
            }
        }

        return result;
    }

    decltype(ovr_EndFrame)* g_original_EndFrame{nullptr};
    ovrResult __cdecl hook_EndFrame(ovrSession session,
                                    long long frameIndex,
                                    const ovrViewScaleDesc* viewScaleDesc,
                                    ovrLayerHeader const* const* layerPtrList,
                                    unsigned int layerCount) {
        std::vector<const ovrLayerHeader*> layers(layerPtrList, layerPtrList + layerCount);

        if (g_ovrSession) {
            // Append the overlays.
            GetLayers(g_GetPredictedDisplayTime(session, frameIndex), layers);
        }

        return g_original_EndFrame(session, frameIndex, viewScaleDesc, layers.data(), (unsigned int)layers.size());
    }

    decltype(ovr_SubmitFrame)* g_original_SubmitFrame{nullptr};
    ovrResult __cdecl hook_SubmitFrame(ovrSession session,
                                       long long frameIndex,
                                       const ovrViewScaleDesc* viewScaleDesc,
                                       ovrLayerHeader const* const* layerPtrList,
                                       unsigned int layerCount) {
        std::vector<const ovrLayerHeader*> layers(layerPtrList, layerPtrList + layerCount);

        if (g_ovrSession) {
            // Append the overlays.
            GetLayers(g_GetPredictedDisplayTime(session, frameIndex), layers);
        }

        return g_original_SubmitFrame(session, frameIndex, viewScaleDesc, layers.data(), (unsigned int)layers.size());
    }

    void InstallOVRlayHooks() {
        *g_libOVR.put() = GetModuleHandleW(
#ifdef _WIN64
            L"LibOVRRT64_1.dll"
#else
            L"LibOVRRT32_1.dll"
#endif
        );
        if (!g_libOVR) {
            *g_libOVR.put() = GetModuleHandleW(
#ifdef _WIN64
                L"VirtualDesktop.LibOVRRT64_1.dll"
#else
                L"VirtualDesktop.LibOVRRT32_1.dll"
#endif
            );
        }

        if (!g_libOVR) {
            return;
        }

        DetourDllFunctionAttach(g_libOVR.get(),
                                "ovr_CreateTextureSwapChainDX",
                                hook_CreateTextureSwapChainDX,
                                g_original_CreateTextureSwapChainDX);
        DetourDllFunctionAttach(g_libOVR.get(), "ovr_GetInputState", hook_GetInputState, g_original_GetInputState);
        DetourDllFunctionAttach(g_libOVR.get(), "ovr_EndFrame", hook_EndFrame, g_original_EndFrame);
        DetourDllFunctionAttach(g_libOVR.get(), "ovr_SubmitFrame", hook_SubmitFrame, g_original_SubmitFrame);

#define GET_OVR_PROC_ADDRESS(proc)                                                                                     \
    reinterpret_cast<decltype(proc)*>(GetProcAddress(g_libOVR.get(), DETOURS_STRINGIFY(proc)));
        g_GetPredictedDisplayTime = GET_OVR_PROC_ADDRESS(ovr_GetPredictedDisplayTime);
        g_dispatchTable.ovr_GetTimeInSeconds = GET_OVR_PROC_ADDRESS(ovr_GetTimeInSeconds);
        g_dispatchTable.ovr_CreateTextureSwapChainDX = g_original_CreateTextureSwapChainDX;
        g_dispatchTable.ovr_DestroyTextureSwapChain = GET_OVR_PROC_ADDRESS(ovr_DestroyTextureSwapChain);
        g_dispatchTable.ovr_GetTextureSwapChainLength = GET_OVR_PROC_ADDRESS(ovr_GetTextureSwapChainLength);
        g_dispatchTable.ovr_GetTextureSwapChainCurrentIndex = GET_OVR_PROC_ADDRESS(ovr_GetTextureSwapChainCurrentIndex);
        g_dispatchTable.ovr_GetTextureSwapChainBufferDX = GET_OVR_PROC_ADDRESS(ovr_GetTextureSwapChainBufferDX);
        g_dispatchTable.ovr_CommitTextureSwapChain = GET_OVR_PROC_ADDRESS(ovr_CommitTextureSwapChain);
        g_dispatchTable.ovr_GetTrackingState = GET_OVR_PROC_ADDRESS(ovr_GetTrackingState);
        g_dispatchTable.ovr_GetInputState = g_original_GetInputState;
        g_dispatchTable.ovr_SetControllerVibration = GET_OVR_PROC_ADDRESS(ovr_SetControllerVibration);
#undef GET_OVR_PROC_ADDRESS
    }

    void UninstallOVRlayHooks() {
        DetourFunctionDetach(hook_CreateTextureSwapChainDX, g_original_CreateTextureSwapChainDX);
        DetourFunctionDetach(hook_GetInputState, g_original_GetInputState);
        DetourFunctionDetach(hook_EndFrame, g_original_EndFrame);
        DetourFunctionDetach(hook_SubmitFrame, g_original_SubmitFrame);
        g_libOVR.reset();
    }

} // namespace

#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
#ifdef WITH_HOOKS
        DetourRestoreAfterWith();
        InstallOVRlayHooks();
#endif
        break;

    case DLL_PROCESS_DETACH:
#ifdef WITH_HOOKS
        UninstallOVRlayHooks();
#endif
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
