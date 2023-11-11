// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <d3d11.h>
#include <OVR_CAPI.h>
#include <OVR_CAPI_D3D.h>
#include <vector>

#if defined(OVRLAY_STANDALONE)
namespace OVRlay {
#define OVRLAY_DLLAPI
#elif defined(OVRLAY_EXPORTS)
extern "C" {
#define OVRLAY_DLLAPI __declspec(dllexport)
#else
extern "C" {
#define OVRLAY_DLLAPI __declspec(dllimport)
#endif

    struct ovrDispatchTable {
        decltype(ovr_GetTimeInSeconds)* ovr_GetTimeInSeconds;
        decltype(ovr_CreateTextureSwapChainDX)* ovr_CreateTextureSwapChainDX;
        decltype(ovr_DestroyTextureSwapChain)* ovr_DestroyTextureSwapChain;
        decltype(ovr_GetTextureSwapChainLength)* ovr_GetTextureSwapChainLength;
        decltype(ovr_GetTextureSwapChainCurrentIndex)* ovr_GetTextureSwapChainCurrentIndex;
        decltype(ovr_GetTextureSwapChainBufferDX)* ovr_GetTextureSwapChainBufferDX;
        decltype(ovr_CommitTextureSwapChain)* ovr_CommitTextureSwapChain;
        decltype(ovr_GetTrackingState)* ovr_GetTrackingState;
        decltype(ovr_GetInputState)* ovr_GetInputState;
        decltype(ovr_SetControllerVibration)* ovr_SetControllerVibration;
    };

    void OVRLAY_DLLAPI Initialize(ovrSession session, const ovrDispatchTable& dispatchTable, ID3D11Device* ovrDevice);
    void OVRLAY_DLLAPI GetLayers(double ovrTime, std::vector<const ovrLayerHeader*>& layers);
    void OVRLAY_DLLAPI GetLayers2(double ovrTime, std::vector<ovrLayer_Union>& layers);
    bool OVRLAY_DLLAPI HasFocus();
}
