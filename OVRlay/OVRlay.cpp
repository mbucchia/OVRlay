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

// A self-contained implementation of desktop window overlays for usage with LibOVR.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <wil/resource.h>
#include <filesystem>
#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <mutex>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <d3d11_4.h>
#pragma comment(lib, "d3d11.lib")
#include <dxgi1_2.h>
#pragma comment(lib, "dxgi.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <OVR_CAPI.h>
#include <OVR_CAPI_D3D.h>
#include <OVR_Math.h>

#include <DirectXCollision.h>

#include <winrt/base.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/windows.graphics.directx.direct3d11.h>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "OVRlay.h"

using Microsoft::WRL::ComPtr;

namespace {

#pragma region "Logging"
    std::ofstream s_logStream;

    void Log(const char* fmt, ...) {
        const std::time_t now = std::time(nullptr);

        char buf[1024];
        size_t offset = std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z: ", std::localtime(&now));
        va_list va;
        va_start(va, fmt);
        vsnprintf_s(buf + offset, sizeof(buf) - offset, _TRUNCATE, fmt, va);
        va_end(va);
        OutputDebugStringA(buf);
        if (s_logStream.is_open()) {
            s_logStream << buf;
            s_logStream.flush();
        }
    }

#define CHECK_HRCMD(cmd) _CheckHResult(cmd, #cmd, FILE_AND_LINE)
#define CHECK_OVRCMD(cmd) _CheckOVRResult(cmd, #cmd, FILE_AND_LINE)

#define CHK_STRINGIFY(x) #x
#define TOSTRING(x) CHK_STRINGIFY(x)
#define FILE_AND_LINE __FILE__ ":" TOSTRING(__LINE__)

    namespace {
        inline std::string _Fmt(const char* fmt, ...) {
            va_list vl;
            va_start(vl, fmt);
            int size = std::vsnprintf(nullptr, 0, fmt, vl);
            va_end(vl);

            if (size != -1) {
                std::unique_ptr<char[]> buffer(new char[size + 1]);

                va_start(vl, fmt);
                size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
                va_end(vl);
                if (size != -1) {
                    return std::string(buffer.get(), size);
                }
            }

            throw std::runtime_error("Unexpected vsnprintf failure");
        }

        [[noreturn]] inline void _Throw(std::string failureMessage,
                                        const char* originator = nullptr,
                                        const char* sourceLocation = nullptr) {
            if (originator != nullptr) {
                failureMessage += _Fmt("\n    Origin: %s", originator);
            }
            if (sourceLocation != nullptr) {
                failureMessage += _Fmt("\n    Source: %s", sourceLocation);
            }

            throw std::logic_error(failureMessage);
        }

        [[noreturn]] inline void _ThrowHResult(HRESULT hr,
                                               const char* originator = nullptr,
                                               const char* sourceLocation = nullptr) {
            _Throw(_Fmt("HRESULT failure [%x]", hr), originator, sourceLocation);
        }

        [[noreturn]] inline void _ThrowOVRResult(ovrResult ovr,
                                                 const char* originator = nullptr,
                                                 const char* sourceLocation = nullptr) {
            _Throw(_Fmt("ovrResult failure [%d]", ovr), originator, sourceLocation);
        }

    } // namespace

    inline HRESULT _CheckHResult(HRESULT hr, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        if (FAILED(hr)) {
            _ThrowHResult(hr, originator, sourceLocation);
        }

        return hr;
    }

    inline HRESULT _CheckOVRResult(ovrResult ovr,
                                   const char* originator = nullptr,
                                   const char* sourceLocation = nullptr) {
        if (OVR_FAILURE(ovr)) {
            _ThrowOVRResult(ovr, originator, sourceLocation);
        }

        return ovr;
    }
#pragma endregion

#pragma region "CaptureWindow"
    // Alternative to windows.graphics.directx.direct3d11.interop.h
    extern "C" {
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);

    HRESULT __stdcall CreateDirect3D11SurfaceFromDXGISurface(::IDXGISurface* dgxiSurface,
                                                             ::IInspectable** graphicsSurface);
    }

    // https://gist.github.com/kennykerr/15a62c8218254bc908de672e5ed405fa
    struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDXGIInterfaceAccess : ::IUnknown {
        virtual HRESULT __stdcall GetInterface(GUID const& id, void** object) = 0;
    };

    class CaptureWindow {
      public:
        CaptureWindow(ID3D11Device* device, HWND window) {
            auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                                                 IGraphicsCaptureItemInterop>();
            CHECK_HRCMD(interop_factory->CreateForWindow(
                window,
                winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                winrt::put_abi(m_item)));

            initialize(device);
        }

        CaptureWindow(ID3D11Device* device, HMONITOR monitor) {
            auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                                                 IGraphicsCaptureItemInterop>();
            CHECK_HRCMD(interop_factory->CreateForMonitor(
                monitor,
                winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                winrt::put_abi(m_item)));

            initialize(device);
        }

        ~CaptureWindow() {
            m_session.Close();
            m_framePool.Close();
        }

        ID3D11Texture2D* getSurface() const {
            auto frame = m_framePool.TryGetNextFrame();
            if (frame != nullptr) {
                ComPtr<ID3D11Texture2D> surface;
                auto access = frame.Surface().as<IDirect3DDXGIInterfaceAccess>();
                CHECK_HRCMD(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(),
                                                 reinterpret_cast<void**>(surface.ReleaseAndGetAddressOf())));

                m_lastCapturedFrame = frame;
                m_lastCapturedSurface = surface;
            }

            return m_lastCapturedSurface.Get();
        }

        ovrSizei getSize() const {
            return {m_item.Size().Width, m_item.Size().Height};
        }

      private:
        void initialize(ID3D11Device* device) {
            ComPtr<IDXGIDevice> dxgiDevice;
            CHECK_HRCMD(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.ReleaseAndGetAddressOf())));
            ComPtr<IInspectable> object;
            CHECK_HRCMD(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), object.GetAddressOf()));
            CHECK_HRCMD(
                object->QueryInterface(winrt::guid_of<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>(),
                                       winrt::put_abi(m_interopDevice)));

            m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                m_interopDevice,
                static_cast<winrt::Windows::Graphics::DirectX::DirectXPixelFormat>(DXGI_FORMAT_R8G8B8A8_UNORM),
                2,
                m_item.Size());
            m_session = m_framePool.CreateCaptureSession(m_item);
            m_session.StartCapture();
        }

        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_interopDevice;
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{nullptr};
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{nullptr};
        winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{nullptr};
        mutable winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame m_lastCapturedFrame{nullptr};
        mutable ComPtr<ID3D11Texture2D> m_lastCapturedSurface;
    };
#pragma endregion

#pragma region "Utilities"
    ovrTextureFormat dxgiToOvrTextureFormat(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return OVR_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return OVR_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return OVR_FORMAT_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return OVR_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return OVR_FORMAT_B8G8R8X8_UNORM_SRGB;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return OVR_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_D16_UNORM:
            return OVR_FORMAT_D16_UNORM;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return OVR_FORMAT_D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D32_FLOAT:
            return OVR_FORMAT_D32_FLOAT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return OVR_FORMAT_D32_FLOAT_S8X24_UINT;
        default:
            return OVR_FORMAT_UNKNOWN;
        }
    }

    namespace geom {
        using namespace DirectX;

        inline DirectX::XMVECTOR XM_CALLCONV LoadOvrVector3(const ovrVector3f& vector) {
            return DirectX::XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&vector));
        }

        inline DirectX::XMVECTOR XM_CALLCONV LoadOvrQuaternion(const ovrQuatf& quaternion) {
            return DirectX::XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(&quaternion));
        }

        inline DirectX::XMMATRIX XM_CALLCONV LoadOvrPose(const ovrPosef& pose) {
            const DirectX::XMVECTOR orientation = LoadOvrQuaternion(pose.Orientation);
            const DirectX::XMVECTOR position = LoadOvrVector3(pose.Position);
            DirectX::XMMATRIX matrix = DirectX::XMMatrixRotationQuaternion(orientation);
            matrix.r[3] = DirectX::XMVectorAdd(matrix.r[3], position);
            return matrix;
        }

        inline void XM_CALLCONV StoreOvrVector3(ovrVector3f* outVec, FXMVECTOR inVec) {
            DirectX::XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(outVec), inVec);
        }

        inline void XM_CALLCONV StoreOvrQuaternion(ovrQuatf* outQuat, FXMVECTOR inQuat) {
            DirectX::XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(outQuat), inQuat);
        }

        inline bool XM_CALLCONV StoreOvrPose(ovrPosef* out, FXMMATRIX matrix) {
            DirectX::XMVECTOR position;
            DirectX::XMVECTOR orientation;
            DirectX::XMVECTOR scale;

            if (!DirectX::XMMatrixDecompose(&scale, &orientation, &position, matrix)) {
                return false; // Non-SRT matrix encountered
            }

            StoreOvrQuaternion(&out->Orientation, orientation);
            StoreOvrVector3(&out->Position, position);
            return true;
        }

        inline ovrQuatf RotationRollPitchYaw(const ovrVector3f& anglesInRadians) {
            ovrQuatf q;
            StoreOvrQuaternion(
                &q, DirectX::XMQuaternionRotationRollPitchYaw(anglesInRadians.x, anglesInRadians.y, anglesInRadians.z));
            return q;
        }

        void alignToGravity(ovrPosef& pose) {
            // Remove roll.
            float yaw, pitch, roll;
            OVR::Quatf(pose.Orientation).GetYawPitchRoll(&yaw, &pitch, &roll);
            pose.Orientation = RotationRollPitchYaw({pitch, yaw, 0});
        }

        void facingCamera(ovrPosef& pose, const ovrPosef& headPose) {
            const XMMATRIX virtualToGazeOrientation = DirectX::XMMatrixLookToRH(
                {
                    pose.Position.x,
                    pose.Position.y,
                    pose.Position.z,
                },
                {
                    pose.Position.x - headPose.Position.x,
                    pose.Position.y - headPose.Position.y,
                    pose.Position.z - headPose.Position.z,
                },
                {0, 1, 0});
            StoreOvrPose(&pose, XMMatrixInverse(nullptr, virtualToGazeOrientation));
        }

        // Taken from
        // https://github.com/microsoft/OpenXR-MixedReality/blob/main/samples/SceneUnderstandingUwp/Scene_Placement.cpp
        bool XM_CALLCONV rayIntersectQuad(DirectX::FXMVECTOR rayPosition,
                                          DirectX::FXMVECTOR rayDirection,
                                          DirectX::FXMVECTOR v0,
                                          DirectX::FXMVECTOR v1,
                                          DirectX::FXMVECTOR v2,
                                          DirectX::FXMVECTOR v3,
                                          ovrPosef* hitPose,
                                          float& distance) {
            // Not optimal. Should be possible to determine which triangle to test.
            bool hit = TriangleTests::Intersects(rayPosition, rayDirection, v0, v1, v2, distance);
            if (!hit) {
                hit = TriangleTests::Intersects(rayPosition, rayDirection, v3, v2, v0, distance);
            }
            if (hit && hitPose != nullptr) {
                const FXMVECTOR hitPosition = XMVectorAdd(rayPosition, XMVectorScale(rayDirection, distance));
                const FXMVECTOR plane = XMPlaneFromPoints(v0, v2, v1);

                // p' = p - (n . p + d) * n
                // Project the ray position onto the plane
                const float t = XMVectorGetX(XMVector3Dot(plane, rayPosition)) + XMVectorGetW(plane);
                const FXMVECTOR projPoint =
                    XMVectorSubtract(rayPosition, XMVectorMultiply(XMVectorSet(t, t, t, 0), plane));

                // From the projected ray position, look towards the hit position and make the plane's normal "up"
                const FXMVECTOR forward = XMVectorSubtract(hitPosition, projPoint);
                const XMMATRIX virtualToGazeOrientation = XMMatrixLookToRH(hitPosition, forward, plane);
                StoreOvrPose(hitPose, XMMatrixInverse(nullptr, virtualToGazeOrientation));
            }
            return hit;
        }

        bool hitTest(const ovrPosef& ray, const ovrPosef& quadCenter, const ovrVector2f& quadSize, ovrPosef& hitPose) {
            // Taken from
            // https://github.com/microsoft/OpenXR-MixedReality/blob/main/samples/SceneUnderstandingUwp/Scene_Placement.cpp

            // Clockwise order
            const float halfWidth = quadSize.x / 2.0f;
            const float halfHeight = quadSize.y / 2.0f;
            auto v0 = XMVectorSet(-halfWidth, -halfHeight, 0, 1);
            auto v1 = XMVectorSet(-halfWidth, halfHeight, 0, 1);
            auto v2 = XMVectorSet(halfWidth, halfHeight, 0, 1);
            auto v3 = XMVectorSet(halfWidth, -halfHeight, 0, 1);
            const auto matrix = LoadOvrPose(quadCenter);
            v0 = XMVector4Transform(v0, matrix);
            v1 = XMVector4Transform(v1, matrix);
            v2 = XMVector4Transform(v2, matrix);
            v3 = XMVector4Transform(v3, matrix);

            const XMVECTOR rayPosition = LoadOvrVector3(ray.Position);

            const auto forward = XMVectorSet(0, 0, -1, 0);
            const auto rotation = LoadOvrQuaternion(ray.Orientation);
            const XMVECTOR rayDirection = XMVector3Rotate(forward, rotation);

            float distance = 0.0f;
            return rayIntersectQuad(rayPosition, rayDirection, v0, v1, v2, v3, &hitPose, distance);
        }

        // https://gamedev.stackexchange.com/questions/136652/uv-world-mapping-in-shader-with-unity/136720#136720
        ovrVector2f getUVCoordinates(const ovrVector3f& point,
                                     const ovrPosef& quadCenter,
                                     const ovrVector2f& quadSize) {
            const OVR::Vector3f normal = OVR::Posef(quadCenter.Orientation, {}).Transform({0, 0, 1});

            OVR::Vector3f e1 = normal.Cross({1, 0, 0});
            e1.Normalize();
            if (e1.Length() < FLT_EPSILON) {
                e1 = normal.Cross({0, 0, 1});
                e1.Normalize();
            }

            OVR::Vector3f e2 = normal.Cross(e1);
            e2.Normalize();

            const OVR::Vector3f a(point), b(quadCenter.Position);
            const float u = (-e2.Dot(a - b) + (quadSize.x / 2.f)) / quadSize.x;
            const float v = (-e1.Dot(a - b) + (quadSize.y / 2.f)) / quadSize.y;

            return {u, v};
        }

        inline POINT getUVCoordinates(const ovrVector3f& point,
                                      const ovrPosef& quadCenter,
                                      const ovrVector2f& quadSize,
                                      const ovrSizei& quadPixelSize) {
            const ovrVector2f uv = getUVCoordinates(point, quadCenter, quadSize);
            return {static_cast<LONG>(uv.x * quadPixelSize.w), static_cast<LONG>(uv.y * quadPixelSize.h)};
        }

    } // namespace geom
#pragma endregion

#pragma region Transparency Shader
    const std::string_view TransparencyShaderHlsl =
        R"_(
cbuffer config : register(b0) {
    float3 TransparentColor;
    float Alpha;
};
Texture2D in_texture : register(t0);
RWTexture2D<float4> out_texture : register(u0);

[numthreads(32, 32, 1)]
void main(uint2 pos : SV_DispatchThreadID)
{
    float a = 1.f;
    if (all(TransparentColor == float3(-1.f, -1.f, -1.f)) || all(in_texture[pos].rgb == TransparentColor)) {
        a = Alpha;
    }
    out_texture[pos] = float4(in_texture[pos].rgb, a);
}
    )_";

    struct TransparencyShaderConstants {
        ovrVector3f transparentColor;
        float alpha;
    };

#pragma endregion

    // Definitions for the memory-mapped file structures.
    namespace shared {

        struct Vector3 {
            float x, y, z;
        };

        struct Quaternion {
            float x, y, z, w;
        };

        struct Pose {
            Quaternion orientation;
            Vector3 position;
        };

        static constexpr int OverlayCount = 4;

        struct OverlayState {
            uint64_t handle;
            Pose pose;
            float scale;
            uint8_t isMonitor;
            uint8_t opacity;
            uint8_t placement;
            uint8_t isInteractable;
            uint8_t isFrozen;
            uint8_t isMinimized;
        };

    } // namespace shared

    class OverlayManager {
      private:
        static constexpr float MinimizedIconSize = 0.1f;

        enum class WindowPlacement {
            WorldLocked = 0,
            HeadLocked,
        };

        // The state of each window, including what is needed for interactions and display.
        struct Window {
            ~Window() {
                Clear();
            }

            void Initialize(ovrSession session, const ovrDispatchTable& dispatchTable) {
                Clear();
                m_ovrSession = session;
                m_dispatchTable = dispatchTable;
            }

            void Clear() {
                quad.Header.Type = ovrLayerType_Disabled;
                captureWindow.reset();
                swapchainImagesOnSubmissionDevice.clear();
                swapchainImagesOnCompositionDevice.clear();
                if (swapchain) {
                    m_dispatchTable.ovr_DestroyTextureSwapChain(m_ovrSession, swapchain);
                    swapchain = nullptr;
                }
            }

            bool IsValid() const {
                return quad.Header.Type == ovrLayerType_Quad && captureWindow;
            }

            HWND hwnd{nullptr};
            HMONITOR monitor{nullptr};

            std::unique_ptr<CaptureWindow> captureWindow;

            float scale{1.f};
            float opacity{1.f};
            WindowPlacement placement{WindowPlacement::WorldLocked};
            bool isInteractable{true};
            bool isFrozen{false};
            bool isMinimized{false};

            bool hasFocus{false};

            ovrTextureSwapChain swapchain{nullptr};
            ovrSizei swapchainSize{};
            std::vector<ComPtr<ID3D11Texture2D>> swapchainImagesOnCompositionDevice;
            std::vector<ComPtr<ID3D11Texture2D>> swapchainImagesOnSubmissionDevice;

            // TODO: Support cylinder as well.
            ovrLayerQuad quad{};

          private:
            ovrSession m_ovrSession{nullptr};
            ovrDispatchTable m_dispatchTable;
        };

      public:
        OverlayManager() {
            *m_overlayStateFile.put() =
                OpenFileMapping(FILE_MAP_READ | FILE_MAP_WRITE, false, L"VirtualDesktop.OverlayState");
            if (!m_overlayStateFile) {
                Log("Failed to open memory-mapped file.\n");
                return;
            }

            m_overlayState = reinterpret_cast<shared::OverlayState*>(MapViewOfFile(
                m_overlayStateFile.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(shared::OverlayState)));
            if (!m_overlayState) {
                Log("Failed to map memory-mapped file.\n");
                return;
            }

            Log("Hello!\n");
        }

        ~OverlayManager() {
            Log("Shutting down...\n");
            if (m_cursorSwapchain) {
                m_dispatchTable.ovr_DestroyTextureSwapChain(m_ovrSession, m_cursorSwapchain);
            }
            if (m_compositionDevice) {
                FlushCompositionDevice();
            }
            if (m_overlayState) {
                UnmapViewOfFile(m_overlayState);
            }
            Log("Bye!\n");
        }

        void SetSubmissionSession(ovrSession session, const ovrDispatchTable& dispatchTable, ID3D11Device* device) {
            if (!m_overlayState) {
                return;
            }

            Log("Acquiring new OVR session.\n");

            if (m_compositionDevice) {
                FlushCompositionDevice();
            }
            if (m_cursorSwapchain) {
                m_dispatchTable.ovr_DestroyTextureSwapChain(m_ovrSession, m_cursorSwapchain);
            }

            m_ovrSession = session;
            m_dispatchTable = dispatchTable;
            for (auto& window : m_windows) {
                window.Initialize(session, dispatchTable);
            }

            CHECK_HRCMD(device->QueryInterface(m_submissionDevice.ReleaseAndGetAddressOf()));
            ComPtr<ID3D11DeviceContext> context;
            m_submissionDevice->GetImmediateContext(context.ReleaseAndGetAddressOf());
            CHECK_HRCMD(context->QueryInterface(m_submissionContext.ReleaseAndGetAddressOf()));

            InitializeCompositionResources();
        }

        void Update(double ovrTime) {
            if (!m_overlayState) {
                return;
            }

            SortWindows();
            HandleInteractions(ovrTime);
            UpdateWindows();

            // Serialize composition work.
            m_submissionFenceValue++;
            CHECK_HRCMD(m_compositionContext->Signal(m_fenceOnCompositionDevice.Get(), m_submissionFenceValue));
            CHECK_HRCMD(m_submissionContext->Wait(m_fenceOnSubmissionDevice.Get(), m_submissionFenceValue));

            // Commit the state and swapchain images.
            for (uint32_t i = 0; i < m_windows.size(); i++) {
                auto& window = m_windows[i];

                if (!window.IsValid()) {
                    continue;
                }

                SyncWindow(i);

                if (window.swapchain) {
                    CHECK_OVRCMD(m_dispatchTable.ovr_CommitTextureSwapChain(m_ovrSession, window.swapchain));
                }
            }
        }

        void GetLayers(std::vector<const ovrLayerHeader*>& layers) {
            for (uint32_t index : m_sortedWindows) {
                layers.push_back(&m_windows[index].quad.Header);
            }

            // Append the cursor.
            if (m_cursorPose) {
                m_cursorQuad.QuadPoseCenter.Position = {m_cursorPose.value().Position.x + m_cursorQuad.QuadSize.x / 2.f,
                                                        m_cursorPose.value().Position.y - m_cursorQuad.QuadSize.y / 2.f,
                                                        m_cursorPose.value().Position.z};
                m_cursorQuad.QuadPoseCenter.Orientation = m_cursorPose.value().Orientation;

                switch (m_windows[m_windowHovered].placement) {
                case WindowPlacement::HeadLocked:
                    m_cursorQuad.Header.Flags |= ovrLayerFlag_HeadLocked;
                    break;

                default:
                    m_cursorQuad.Header.Flags &= ~ovrLayerFlag_HeadLocked;
                    break;
                }

                layers.push_back(&m_cursorQuad.Header);
            }
        }

        bool HasFocus() {
            return m_cursorPose.has_value();
        }

      private:
        void InitializeCompositionResources() {
            // Create our own device on the same adapter.
            ComPtr<IDXGIDevice> dxgiDevice;
            CHECK_HRCMD(m_submissionDevice->QueryInterface(dxgiDevice.ReleaseAndGetAddressOf()));
            ComPtr<IDXGIAdapter> dxgiAdapter;
            CHECK_HRCMD(dxgiDevice->GetAdapter(dxgiAdapter.ReleaseAndGetAddressOf()));

            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;

            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
            flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
            CHECK_HRCMD(D3D11CreateDevice(dxgiAdapter.Get(),
                                          D3D_DRIVER_TYPE_UNKNOWN,
                                          0,
                                          flags,
                                          &featureLevel,
                                          1,
                                          D3D11_SDK_VERSION,
                                          device.ReleaseAndGetAddressOf(),
                                          nullptr,
                                          context.ReleaseAndGetAddressOf()));
            CHECK_HRCMD(device->QueryInterface(m_compositionDevice.ReleaseAndGetAddressOf()));
            CHECK_HRCMD(context->QueryInterface(m_compositionContext.ReleaseAndGetAddressOf()));

            // Create serialization fence.
            CHECK_HRCMD(m_compositionDevice->CreateFence(
                0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(m_fenceOnCompositionDevice.ReleaseAndGetAddressOf())));
            wil::unique_handle fenceHandle;
            CHECK_HRCMD(
                m_fenceOnCompositionDevice->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, fenceHandle.put()));
            CHECK_HRCMD(m_submissionDevice->OpenSharedFence(
                fenceHandle.get(), IID_PPV_ARGS(m_fenceOnSubmissionDevice.ReleaseAndGetAddressOf())));

            const auto compileShader =
                [&](const std::string_view& code, const std::string_view& entry, ComPtr<ID3DBlob>& shaderBytes) {
                    ComPtr<ID3DBlob> errMsgs;
                    DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef _DEBUG
                    flags |= D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
                    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

                    const HRESULT hr = D3DCompile(code.data(),
                                                  code.size(),
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  entry.data(),
                                                  "cs_5_0",
                                                  flags,
                                                  0,
                                                  shaderBytes.ReleaseAndGetAddressOf(),
                                                  errMsgs.ReleaseAndGetAddressOf());
                    if (FAILED(hr)) {
                        std::string errMsg((const char*)errMsgs->GetBufferPointer(), errMsgs->GetBufferSize());
                        Log("D3DCompile failed %X: %s\n", hr, errMsg.c_str());
                        CHECK_HRCMD(hr);
                    }
                };

            // Create the resources for the transparency shader
            {
                ComPtr<ID3DBlob> shaderBytes;
                compileShader(TransparencyShaderHlsl, "main", shaderBytes);
                CHECK_HRCMD(m_compositionDevice->CreateComputeShader(shaderBytes->GetBufferPointer(),
                                                                     shaderBytes->GetBufferSize(),
                                                                     nullptr,
                                                                     m_transparencyShader.ReleaseAndGetAddressOf()));

                D3D11_BUFFER_DESC desc{};
                desc.ByteWidth = sizeof(TransparencyShaderConstants);
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                CHECK_HRCMD(m_compositionDevice->CreateBuffer(
                    &desc, nullptr, m_transparencyConstants.ReleaseAndGetAddressOf()));
            }

            // Create cursor graphics.
            {
                ovrTextureSwapChainDesc swapchainDesc{};
                swapchainDesc.Type = ovrTexture_2D;
                swapchainDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM;
                swapchainDesc.Width = 32;
                swapchainDesc.Height = 32;
                swapchainDesc.StaticImage = ovrTrue;
                swapchainDesc.ArraySize = swapchainDesc.MipLevels = swapchainDesc.SampleCount = 1;
                swapchainDesc.MiscFlags = ovrTextureMisc_DX_Typeless;
                CHECK_OVRCMD(m_dispatchTable.ovr_CreateTextureSwapChainDX(
                    m_ovrSession, m_submissionDevice.Get(), &swapchainDesc, &m_cursorSwapchain));

                std::vector<uint32_t> cursorBitmap(swapchainDesc.Width * swapchainDesc.Height, 0xffffffff);

                D3D11_TEXTURE2D_DESC textureDesc{};
                textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                textureDesc.Width = swapchainDesc.Width;
                textureDesc.Height = swapchainDesc.Height;
                textureDesc.ArraySize = textureDesc.MipLevels = textureDesc.SampleDesc.Count = 1;
                textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                D3D11_SUBRESOURCE_DATA initialData{};
                initialData.SysMemPitch = textureDesc.Width;
                initialData.pSysMem = cursorBitmap.data();
                ComPtr<ID3D11Texture2D> stagingTexture;
                CHECK_HRCMD(m_submissionDevice->CreateTexture2D(
                    &textureDesc, &initialData, stagingTexture.ReleaseAndGetAddressOf()));

                ComPtr<ID3D11Texture2D> swapchainTexture;
                CHECK_OVRCMD(m_dispatchTable.ovr_GetTextureSwapChainBufferDX(
                    m_ovrSession, m_cursorSwapchain, 0, IID_PPV_ARGS(swapchainTexture.ReleaseAndGetAddressOf())));
                m_submissionContext->CopyResource(swapchainTexture.Get(), stagingTexture.Get());
                CHECK_OVRCMD(m_dispatchTable.ovr_CommitTextureSwapChain(m_ovrSession, m_cursorSwapchain));

                m_cursorQuad.Header.Type = ovrLayerType_Quad;
                m_cursorQuad.ColorTexture = m_cursorSwapchain;
                m_cursorQuad.QuadSize = {0.01f, 0.01f};
                m_cursorQuad.Viewport.Size = {swapchainDesc.Width, swapchainDesc.Height};
            }
        }

        // Flush all commands on the composition device (prepare for destruction).
        void FlushCompositionDevice() {
            m_submissionFenceValue++;
            CHECK_HRCMD(m_compositionContext->Signal(m_fenceOnCompositionDevice.Get(), m_submissionFenceValue));
            wil::unique_handle eventHandle;
            *eventHandle.put() = CreateEventEx(nullptr, L"Flush Fence", 0, EVENT_ALL_ACCESS);
            CHECK_HRCMD(m_fenceOnCompositionDevice->SetEventOnCompletion(m_submissionFenceValue, eventHandle.get()));
            WaitForSingleObject(eventHandle.get(), INFINITE);
        }

        // Determine visible windows and drawing order.
        void SortWindows() {
            std::vector<std::pair<float, uint32_t>> distances;
            for (uint32_t i = 0; i < m_windows.size(); i++) {
                auto& window = m_windows[i];

                // Detect window added/removed.
                if (!window.IsValid()) {
                    if (!m_overlayState[i].handle) {
                        continue;
                    }
                    OpenWindow(i);
                } else {
                    if (!m_overlayState[i].handle) {
                        CloseWindow(i);
                        continue;
                    }
                }

                // TODO: Cull windows completely out of the view.
                const OVR::Vector3f vector =
                    OVR::Vector3f(window.quad.QuadPoseCenter.Position) - m_lastHeadPose.Translation;
                distances.push_back(std::make_pair(vector.Length(), i));
            }

            // Sort from back to front.
            std::sort(distances.begin(),
                      distances.end(),
                      [](std::pair<float, uint64_t> a, std::pair<float, uint64_t> b) { return a.first > b.first; });
            m_sortedWindows.clear();
            for (const auto& entry : distances) {
                m_sortedWindows.push_back(entry.second);
            }
        }

        // Handle interactions with the windows.
        void HandleInteractions(double ovrTime) {
            // Get the head pose.
            ovrTrackingState tracking = m_dispatchTable.ovr_GetTrackingState(m_ovrSession, ovrTime, false);
            const OVR::Posef headPose = tracking.HeadPose.ThePose;

            // Get the aim from each hand.
            std::optional<OVR::Posef> aimPoseInLocalSpace[2];
            std::optional<OVR::Posef> aimPoseInViewSpace[2];
            for (uint32_t side = 0; side < 2; side++) {
                if (!(tracking.HandStatusFlags[side] & (ovrStatus_PositionValid | ovrStatus_OrientationValid))) {
                    continue;
                }

                aimPoseInLocalSpace[side] = tracking.HandPoses[side].ThePose;
                aimPoseInViewSpace[side] = aimPoseInLocalSpace[side].value() * headPose.Inverted();
            }

            // Perform hittesting to find a focused window. We perform the test from back (closest window) to front
            // (fartherest window).
            m_lastCursorPosition = m_cursorPose ? m_cursorPose.value().Position : ovrVector3f{0, 0, 0};
            m_cursorPose.reset();
            decltype(m_sortedWindows)::reverse_iterator it;
            bool isHoveringOnWindow = false;
            for (it = m_sortedWindows.rbegin(); !m_cursorPose && it != m_sortedWindows.rend(); ++it) {
                Window& window = m_windows.at(*it);

                const bool wasHoveringOnWindow = isHoveringOnWindow;
                if (!isHoveringOnWindow) {
                    // We will draw the cursor if and only if the controller aim hits close to the overlay (up
                    // to 50px on each corner) outside.
                    const int32_t margin = 50;
                    ovrSizei sizeInPixels = window.captureWindow->getSize();
                    const ovrVector2f& windowSize = window.quad.QuadSize;
                    const ovrVector2f pixelsPerMeter = {sizeInPixels.w / windowSize.x, sizeInPixels.h / windowSize.y};

                    // When both hands are focusing on a window, always "continue" interacting with the same hand as
                    // previously.
                    uint32_t side = m_lastSideToInteract;
                    bool hovering = false;
                    for (uint32_t i = 0; i < 2; i++) {
                        if (aimPoseInLocalSpace[side]) {
                            // Use the aim pose relative to the space the window pose refers to.
                            OVR::Posef aimPose;
                            switch (window.placement) {
                            case WindowPlacement::WorldLocked:
                                aimPose = aimPoseInLocalSpace[side].value();
                                break;
                            case WindowPlacement::HeadLocked:
                                aimPose = aimPoseInViewSpace[side].value();
                                break;
                            };

                            ovrPosef hitPose;
                            if (geom::hitTest(aimPose,
                                              window.quad.QuadPoseCenter,
                                              {(sizeInPixels.w + margin * 2) / pixelsPerMeter.x,
                                               (sizeInPixels.h + margin * 2) / pixelsPerMeter.y},
                                              hitPose)) {
                                // Handle interactions for the focused window.
                                const OVR::Posef controllerPoses[2] = {
                                    aimPoseInLocalSpace[0].value_or(OVR::Posef::Identity()),
                                    aimPoseInLocalSpace[1].value_or(OVR::Posef::Identity())};
                                HandleWindowInteractions(window, side, headPose, controllerPoses, hitPose);

                                m_cursorPose =
                                    OVR::Posef::Pose(window.quad.QuadPoseCenter.Orientation, hitPose.Position);
                                m_lastSideToInteract = side;
                                isHoveringOnWindow = true;
                                break;
                            }
                        }
                        side ^= 1;
                    }
                }

                if (wasHoveringOnWindow == isHoveringOnWindow) {
                    window.hasFocus = false;
                }
            }

            for (uint32_t side = 0; side < 2; side++) {
                if (aimPoseInLocalSpace[side]) {
                    m_lastControllerPoses[side] = aimPoseInLocalSpace[side].value();
                }
            }
            m_lastHeadPose = headPose;
        }

        void HandleWindowInteractions(Window& window,
                                      uint32_t side,
                                      const OVR::Posef& headPose,
                                      const OVR::Posef* controllerPoses,
                                      const OVR::Posef& hitPose) {
            // Read the buttons state.
            constexpr float ClickThreshold = 0.75f;
            ovrInputState input{};
            m_dispatchTable.ovr_GetInputState(m_ovrSession, ovrControllerType_Touch, &input);

            const bool isDraggingWindow = m_isDraggingWindow;
            m_isDraggingWindow = false;
            const bool isResizingWindow = m_isResizingWindow;
            m_isResizingWindow = false;

            if (!window.isFrozen) {
                const bool wasThumbstickPressed = m_isThumbstickPressed;
                m_isThumbstickPressed = input.Buttons & (!side ? ovrButton_LThumb : ovrButton_RThumb);
                if (!window.isMinimized && input.HandTrigger[side] > ClickThreshold) {
                    if (input.HandTrigger[side ^ 1] <= ClickThreshold) {
                        if (m_isThumbstickPressed && !wasThumbstickPressed) {
                            // Reorient the window to face the camera.
                            geom::facingCamera(window.quad.QuadPoseCenter, headPose);
                        } else {
                            // One handed grab: drag the window.
                            if (input.IndexTrigger[side] > ClickThreshold) {
                                if (isDraggingWindow) {
                                    // Move along the cursor (lateral + height).
                                    OVR::Vector3f delta = hitPose.Translation - m_lastCursorPosition;

                                    // Move along the forward axis.
                                    constexpr float Sensitivity = 0.25f;
                                    const float lastDistance =
                                        (m_lastHeadPose.Translation - m_lastControllerPoses[side].Translation).Length();
                                    const float distance =
                                        (headPose.Translation - controllerPoses[side].Translation).Length();
                                    delta = delta + OVR::Posef(window.quad.QuadPoseCenter.Orientation, {})
                                                        .Transform({0, 0, (lastDistance - distance) * Sensitivity});

                                    // Clamp to avoid too large motion.
                                    // TODO: Need temporal component - frame rate isn't stable.
                                    delta.x = std::clamp(delta.x, -0.02f, 0.02f);
                                    delta.y = std::clamp(delta.y, -0.02f, 0.02f);
                                    delta.z = std::clamp(delta.z, -0.01f, 0.01f);

                                    const OVR::Vector3f newPosition =
                                        OVR::Vector3f(window.quad.QuadPoseCenter.Position) + delta;

                                    // Avoid sending a window too far from the camera.
                                    constexpr float MaxDistance = 10.f;
                                    if ((newPosition - headPose.Translation).Length() < MaxDistance) {
                                        window.quad.QuadPoseCenter.Position = newPosition;
                                    }
                                }
                                m_isDraggingWindow = true;
                            } else {
                                const ovrVector2f& thumbstick = input.Thumbstick[side];
                                float yaw, pitch, roll;
                                OVR::Quatf(window.quad.QuadPoseCenter.Orientation).GetYawPitchRoll(&yaw, &pitch, &roll);
                                // TODO: Need temporal component - frame rate isn't stable.
                                yaw += thumbstick.x * (float)MATH_DOUBLE_TWOPI / 360;
                                pitch += -thumbstick.y * (float)MATH_DOUBLE_TWOPI / 360;
                                window.quad.QuadPoseCenter.Orientation = geom::RotationRollPitchYaw({pitch, yaw, 0});
                            }
                        }
                        geom::alignToGravity(window.quad.QuadPoseCenter);
                    } else {
                        // Two handed grab: resize.
                        if (isResizingWindow) {
                            const float lastLength =
                                (m_lastControllerPoses[0].Translation - m_lastControllerPoses[1].Translation).Length();
                            const float currentLength =
                                (controllerPoses[0].Translation - controllerPoses[1].Translation).Length();
                            const float delta = currentLength - lastLength;

                            window.scale += delta;
                        }
                        m_isResizingWindow = true;
                    }

                    // No further interactions to be handled this frame.
                    return;
                } else if (m_isThumbstickPressed && !wasThumbstickPressed) {
                    window.isMinimized = !window.isMinimized;
                    if (!window.isMinimized) {
                        geom::facingCamera(window.quad.QuadPoseCenter, headPose);
                        geom::alignToGravity(window.quad.QuadPoseCenter);
                    }

                    // No further interactions to be handled this frame.
                    return;
                }
            }

            const bool isInteractable = !window.isMinimized && window.isInteractable;
            if (isInteractable) {
                // Relocate our hit to be relative to the top-left corner of the window.
                const ovrSizei sizeInPixels = window.captureWindow->getSize();
                const POINT cursorPosition = geom::getUVCoordinates(
                    hitPose.Translation, window.quad.QuadPoseCenter, window.quad.QuadSize, sizeInPixels);

                // Check the window boundaries (remember: we offered a little margin when rendering the cursor).
                if (cursorPosition.x > 0 && cursorPosition.x < sizeInPixels.w && cursorPosition.y > 0 &&
                    cursorPosition.y < sizeInPixels.h) {
                    if (window.hasFocus) {
                        // Update the cursor position.
                        // TODO: Why are coordinates off!?
                        POINT clickPosition = cursorPosition;
                        ClientToScreen(window.hwnd, &clickPosition);
                        SetCursorPos(clickPosition.x, clickPosition.y);
                    }

                    const bool wasTriggerPressed = m_isTriggerPressed;
                    m_isTriggerPressed = input.IndexTrigger[side] > ClickThreshold;

                    if (m_isTriggerPressed && !wasTriggerPressed) {
                        const HWND oldForegroundWindow = GetForegroundWindow();
                        POINT oldCursorPos{};
                        GetCursorPos(&oldCursorPos);

                        // Make sure the window can receive clicks.
                        SetForegroundWindow(window.hwnd);
                        if (!window.hasFocus) {
                            // Move the cursor to the destination window.
                            POINT clickPosition = cursorPosition;
                            ClientToScreen(window.hwnd, &clickPosition);
                            SetCursorPos(clickPosition.x, clickPosition.y);
                        }
                        window.hasFocus = true;

                        // Simulate a left click.
                        INPUT events[2]{};
                        events[0].type = INPUT_MOUSE;
                        events[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                        events[1].type = INPUT_MOUSE;
                        events[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                        SendInput(2, events, sizeof(INPUT));

                        // TODO: Move the cursor and focus back.
                        // SetCursorPos(oldCursorPos.x, oldCursorPos.y);
                        // SetForegroundWindow(oldForegroundWindow);

                        // No further interactions to be handled this frame.
                        return;
                    }
                }
            }
        }

        // Refresh the content of all windows.
        void UpdateWindows() {
            for (auto& window : m_windows) {
                if (!window.IsValid()) {
                    continue;
                }

                ID3D11Texture2D* windowSurface = window.captureWindow->getSurface();
                if (!windowSurface) {
                    continue;
                }

                D3D11_TEXTURE2D_DESC windowSurfaceDesc;
                windowSurface->GetDesc(&windowSurfaceDesc);
                if (!window.swapchain || window.swapchainSize.w != windowSurfaceDesc.Width ||
                    window.swapchainSize.h != windowSurfaceDesc.Height) {
                    // Create the swapchain and other resources appropriate for the window.
                    if (window.swapchain) {
                        m_dispatchTable.ovr_DestroyTextureSwapChain(m_ovrSession, window.swapchain);
                        window.swapchain = nullptr;
                    }
                    window.swapchainImagesOnSubmissionDevice.clear();
                    window.swapchainImagesOnCompositionDevice.clear();

                    ovrTextureSwapChainDesc swapchainDesc{};
                    swapchainDesc.Type = ovrTexture_2D;
                    swapchainDesc.Format = dxgiToOvrTextureFormat(windowSurfaceDesc.Format);
                    swapchainDesc.Width = windowSurfaceDesc.Width;
                    swapchainDesc.Height = windowSurfaceDesc.Height;
                    swapchainDesc.ArraySize = swapchainDesc.MipLevels = swapchainDesc.SampleCount = 1;
                    swapchainDesc.MiscFlags = ovrTextureMisc_DX_Typeless;
                    // For the purposes of our transparency shader.
                    swapchainDesc.BindFlags = ovrTextureBind_DX_UnorderedAccess;
                    CHECK_OVRCMD(m_dispatchTable.ovr_CreateTextureSwapChainDX(
                        m_ovrSession, m_submissionDevice.Get(), &swapchainDesc, &window.swapchain));

                    // Share the textures with the composition device.
                    int length = 0;
                    CHECK_OVRCMD(
                        m_dispatchTable.ovr_GetTextureSwapChainLength(m_ovrSession, window.swapchain, &length));
                    for (int j = 0; j < length; j++) {
                        ComPtr<ID3D11Texture2D> swapchainTexture;
                        CHECK_OVRCMD(m_dispatchTable.ovr_GetTextureSwapChainBufferDX(
                            m_ovrSession,
                            window.swapchain,
                            j,
                            IID_PPV_ARGS(swapchainTexture.ReleaseAndGetAddressOf())));
                        window.swapchainImagesOnSubmissionDevice.push_back(swapchainTexture.Get());

                        ComPtr<IDXGIResource1> dxgiResource;
                        CHECK_HRCMD(
                            swapchainTexture->QueryInterface(IID_PPV_ARGS(dxgiResource.ReleaseAndGetAddressOf())));

                        HANDLE textureHandle;
                        CHECK_HRCMD(dxgiResource->GetSharedHandle(&textureHandle));

                        ComPtr<ID3D11Texture2D> compositionTexture;
                        CHECK_HRCMD(m_compositionDevice->OpenSharedResource(
                            textureHandle, IID_PPV_ARGS(compositionTexture.ReleaseAndGetAddressOf())));
                        window.swapchainImagesOnCompositionDevice.push_back(compositionTexture.Get());
                    }

                    window.quad.ColorTexture = window.swapchain;
                    window.swapchainSize.w = windowSurfaceDesc.Width;
                    window.swapchainSize.h = windowSurfaceDesc.Height;
                }

                RECT rc{};
                CHECK_HRCMD(DwmGetWindowAttribute(window.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)));
                D3D11_BOX box{};
                box.right = rc.right - rc.left;
                box.bottom = rc.bottom - rc.top;
                box.back = 1;

                int imageIndex = 0;
                CHECK_OVRCMD(
                    m_dispatchTable.ovr_GetTextureSwapChainCurrentIndex(m_ovrSession, window.swapchain, &imageIndex));
                ID3D11Texture2D* swapchainImage = window.swapchainImagesOnCompositionDevice[imageIndex].Get();
                if (window.opacity >= 0.9999f) {
                    // Copy without transparency.
                    m_compositionContext->CopySubresourceRegion(swapchainImage, 0, 0, 0, 0, windowSurface, 0, &box);
                } else {
                    // Create ephemeral resources to run our transparency shader.
                    ComPtr<ID3D11ShaderResourceView> srv;
                    {
                        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
                        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        desc.Format = windowSurfaceDesc.Format;
                        desc.Texture2D.MipLevels = 1;
                        CHECK_HRCMD(m_compositionDevice->CreateShaderResourceView(
                            windowSurface, &desc, srv.ReleaseAndGetAddressOf()));
                    }
                    ComPtr<ID3D11UnorderedAccessView> uav;
                    {
                        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
                        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        desc.Texture2D.MipSlice = 0;
                        CHECK_HRCMD(m_compositionDevice->CreateUnorderedAccessView(
                            swapchainImage, &desc, uav.ReleaseAndGetAddressOf()));
                    }

                    // Setup the transparency.
                    D3D11_MAPPED_SUBRESOURCE mappedResources;
                    CHECK_HRCMD(m_compositionContext->Map(
                        m_transparencyConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResources));
                    TransparencyShaderConstants transparency{};
                    transparency.transparentColor = {-1, -1, -1};
                    transparency.alpha = window.opacity;
                    memcpy(mappedResources.pData, &transparency, sizeof(transparency));
                    m_compositionContext->Unmap(m_transparencyConstants.Get(), 0);

                    // Copy while doing transparency.
                    m_compositionContext->CSSetShader(m_transparencyShader.Get(), nullptr, 0);
                    m_compositionContext->CSSetShaderResources(0, 1, srv.GetAddressOf());
                    m_compositionContext->CSSetConstantBuffers(0, 1, m_transparencyConstants.GetAddressOf());
                    m_compositionContext->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
                    m_compositionContext->Dispatch((unsigned int)std::ceil(windowSurfaceDesc.Width / 8),
                                                   (unsigned int)std::ceil(windowSurfaceDesc.Height / 8),
                                                   1);

                    // Unbind all resources to avoid D3D validation errors.
                    {
                        m_compositionContext->CSSetShader(nullptr, nullptr, 0);
                        ID3D11ShaderResourceView* nullSRV[] = {nullptr};
                        m_compositionContext->CSSetShaderResources(0, 1, nullSRV);
                        ID3D11Buffer* nullCBV[] = {nullptr};
                        m_compositionContext->CSSetConstantBuffers(0, 1, nullCBV);
                        ID3D11UnorderedAccessView* nullUAV[] = {nullptr};
                        m_compositionContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
                    }
                }

                window.quad.Viewport.Pos = {0, 0};
                window.quad.Viewport.Size = {(int)box.right, (int)box.bottom};
                if (!window.isMinimized) {
                    window.quad.QuadSize.x = window.scale;
                    window.quad.QuadSize.y = (window.scale * window.quad.Viewport.Size.h) / window.quad.Viewport.Size.w;
                } else {
                    window.quad.QuadSize = {MinimizedIconSize, MinimizedIconSize};
                }

                // We will commit the swapchain image after the fence signaling.

                switch (window.placement) {
                case WindowPlacement::HeadLocked:
                    window.quad.Header.Flags |= ovrLayerFlag_HeadLocked;
                    break;

                default:
                    window.quad.Header.Flags &= ~ovrLayerFlag_HeadLocked;
                    break;
                }

                // Handle billboarding.
                if (window.isMinimized && window.placement != WindowPlacement::HeadLocked) {
                    // Minimized window always faces us.
                    geom::facingCamera(window.quad.QuadPoseCenter, m_lastHeadPose);
                    geom::alignToGravity(window.quad.QuadPoseCenter);
                }
            }
        }

        // Pull the state from the memory mapped file to create the resources for the window.
        void OpenWindow(uint32_t slot) {
            const auto& state = m_overlayState[slot];
            auto& window = m_windows[slot];

            if (!state.isMonitor) {
                window.hwnd = (HWND)state.handle;
                window.monitor = nullptr;
            } else {
                window.monitor = (HMONITOR)state.handle;
                window.hwnd = nullptr;
            }
            if (!window.hwnd && !window.monitor) {
                return;
            }

            window.quad.Header.Type = ovrLayerType_Quad;
            window.captureWindow = window.hwnd
                                       ? std::make_unique<CaptureWindow>(m_compositionDevice.Get(), window.hwnd)
                                       : std::make_unique<CaptureWindow>(m_compositionDevice.Get(), window.monitor);
            window.quad.QuadPoseCenter = {{state.pose.orientation.x,
                                           state.pose.orientation.y,
                                           state.pose.orientation.z,
                                           state.pose.orientation.w},
                                          {state.pose.position.x, state.pose.position.y, state.pose.position.z}};
            window.scale = state.scale;
            window.opacity = state.opacity / 100.f;
            window.placement = (WindowPlacement)state.placement;
            window.isInteractable = state.isInteractable;
            window.isFrozen = state.isFrozen;
            window.isMinimized = state.isMinimized;

            // Swapchain is created lazily.
            window.swapchainSize = {0, 0};
            window.swapchain = nullptr;

            window.hasFocus = false;

            // If the window is new, we spawn it in front of the user.
            if (OVR::Posef(window.quad.QuadPoseCenter).IsNan()) {
                OVR::Posef front = OVR::Posef::Pose(OVR::Quatf::Identity(), {0.f, 0.f, -1.f});

                switch (window.placement) {
                case WindowPlacement::WorldLocked:
                    window.quad.QuadPoseCenter = front * m_lastHeadPose;
                    geom::alignToGravity(window.quad.QuadPoseCenter);
                    break;

                case WindowPlacement::HeadLocked:
                    window.quad.QuadPoseCenter = front;
                    break;

                default:
                    window.quad.QuadPoseCenter = OVR::Posef::Identity();
                    break;
                }
            }
        }

        // Synchronize state with the memory mapped file.
        void SyncWindow(uint32_t slot) {
            auto& state = m_overlayState[slot];
            auto& window = m_windows[slot];

            // Push.
            state.pose = {{window.quad.QuadPoseCenter.Orientation.x,
                           window.quad.QuadPoseCenter.Orientation.y,
                           window.quad.QuadPoseCenter.Orientation.z,
                           window.quad.QuadPoseCenter.Orientation.w},
                          {window.quad.QuadPoseCenter.Position.x,
                           window.quad.QuadPoseCenter.Position.y,
                           window.quad.QuadPoseCenter.Position.z}};
            state.scale = window.scale;
            state.isMinimized = window.isMinimized;

            // Pull.
            window.opacity = state.opacity / 100.f;
            window.placement = (WindowPlacement)state.placement;
            window.isInteractable = state.isInteractable;
            window.isFrozen = state.isFrozen;
        }

        // Cleanup all resources associated with a window.
        void CloseWindow(uint32_t slot) {
            auto& window = m_windows[slot];

            window.Clear();
        }

        // Resources for rendering.
        ovrSession m_ovrSession{nullptr};
        ovrDispatchTable m_dispatchTable{};
        ComPtr<ID3D11Device5> m_submissionDevice;
        ComPtr<ID3D11DeviceContext4> m_submissionContext;
        ComPtr<ID3D11Device5> m_compositionDevice;
        ComPtr<ID3D11DeviceContext4> m_compositionContext;
        ComPtr<ID3D11Fence> m_fenceOnSubmissionDevice;
        ComPtr<ID3D11Fence> m_fenceOnCompositionDevice;
        uint64_t m_submissionFenceValue{0};

        ComPtr<ID3D11ComputeShader> m_transparencyShader;
        ComPtr<ID3D11Buffer> m_transparencyConstants;
        ovrTextureSwapChain m_cursorSwapchain{nullptr};

        // State sharing.
        wil::unique_handle m_overlayStateFile;
        shared::OverlayState* m_overlayState{nullptr};

        // Frame/layers state.
        std::array<Window, shared::OverlayCount> m_windows;
        std::vector<uint32_t> m_sortedWindows;
        ovrLayerQuad m_cursorQuad{};

        // Interactions state.
        OVR::Posef m_lastHeadPose{OVR::Posef::Identity()};
        uint32_t m_lastSideToInteract{0};
        OVR::Posef m_lastControllerPoses[2]{OVR::Posef::Identity(), OVR::Posef::Identity()};
        std::optional<ovrPosef> m_cursorPose{};
        OVR::Vector3f m_lastCursorPosition{};
        uint32_t m_windowHovered{};

        bool m_isMenuPressed{false};
        bool m_isTriggerPressed{false};
        bool m_isThumbstickPressed{false};
        bool m_isDraggingWindow{false};
        bool m_isResizingWindow{false};

      public:
        static OverlayManager* GetInstance() {
            static std::once_flag once;
            static OverlayManager* instance = nullptr;
            std::call_once(once, [&]() {
                const auto programData = std::filesystem::path(getenv("PROGRAMDATA")) / L"Virtual Desktop";
                CreateDirectoryW(programData.wstring().c_str(), nullptr);

                // Start logging to file.
                if (!s_logStream.is_open()) {
                    std::wstring logFile = (programData / ("OVRlay.log")).wstring();
                    s_logStream.open(logFile, std::ios_base::ate);
                }

                Log("Starting up...\n");
                instance = new OverlayManager();
            });
            return instance;
        }
    };

} // namespace

#ifndef OVRLAY_EXPORTS
namespace OVRlay {
#endif

    void Initialize(ovrSession session, const ovrDispatchTable& dispatchTable, ID3D11Device* ovrDevice) {
        OverlayManager::GetInstance()->SetSubmissionSession(session, dispatchTable, ovrDevice);
    }

    void GetLayers(double ovrTime, std::vector<const ovrLayerHeader*>& layers) {
        OverlayManager::GetInstance()->Update(ovrTime);
        OverlayManager::GetInstance()->GetLayers(layers);
    }

    void GetLayers2(double ovrTime, std::vector<ovrLayer_Union>& layers) {
        std::vector<const ovrLayerHeader*> layersPtr;
        GetLayers(ovrTime, layersPtr);
        for (auto& layer : layersPtr) {
            ovrLayer_Union u{};
            switch (layer->Type) {
            case ovrLayerType_Quad:
                u.Quad = *reinterpret_cast<const ovrLayerQuad*>(layer);
                break;
            case ovrLayerType_Cylinder:
                u.Cylinder = *reinterpret_cast<const ovrLayerCylinder*>(layer);
                break;
            default:
                continue;
            }
            layers.push_back(u);
        }
    }

    bool HasFocus() {
        return OverlayManager::GetInstance()->HasFocus();
    }

#ifndef OVRLAY_EXPORTS
} // namespace OVRlay
#endif
