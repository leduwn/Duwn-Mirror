#pragma once

#include <windows.h>
#include <dshow.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include "capture/CaptureServer.h"

// CLSID_DuwnMirrorVirtualCam: {8B9F51B8-3232-4518-A7D9-4828E0D71B20}
inline constexpr GUID CLSID_DuwnMirrorVirtualCam =
    {0x8b9f51b8, 0x3232, 0x4518, {0xa7, 0xd9, 0x48, 0x28, 0xe0, 0xd7, 0x1b, 0x20}};

// AMPROPSETID_Pin: {9B00F101-1567-11d1-B3F1-00AA003761C5}
inline constexpr GUID AMPROPSETID_Pin_Local =
    {0x9b00f101, 0x1567, 0x11d1, {0xb3, 0xf1, 0x00, 0xaa, 0x00, 0x37, 0x61, 0xc5}};

// PIN_CATEGORY_CAPTURE: {FB6C4281-0353-11d1-905F-0000C0CC16BA}
inline constexpr GUID PIN_CATEGORY_CAPTURE_Local =
    {0xfb6c4281, 0x0353, 0x11d1, {0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba}};

namespace duwn::vcam {

class DuwnOutputPin;

class DuwnVirtualCamFilter final : public IBaseFilter {
public:
    DuwnVirtualCamFilter();
    ~DuwnVirtualCamFilter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID) override;

    // IMediaFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** ppClock) override;

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override;

private:
    std::atomic<LONG>   m_ref_count{1};
    FILTER_STATE        m_state{State_Stopped};
    IReferenceClock*    m_clock{nullptr};
    IFilterGraph*       m_graph{nullptr};
    std::wstring        m_filter_name{L"Duwn Mirror Video"};
    DuwnOutputPin*      m_pin{nullptr};

    friend class DuwnOutputPin;
};

class DuwnOutputPin final : public IPin, public IKsPropertySet, public IAMStreamConfig {
public:
    explicit DuwnOutputPin(DuwnVirtualCamFilter* parent);
    ~DuwnOutputPin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR* Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID PropSet, ULONG Id, void* InstanceData, ULONG InstanceLength,
                     void* PropertyData, ULONG DataLength) override;
    STDMETHODIMP Get(REFGUID PropSet, ULONG Id, void* InstanceData, ULONG InstanceLength,
                     void* PropertyData, ULONG DataLength, ULONG* BytesReturned) override;
    STDMETHODIMP QuerySupported(REFGUID PropSet, ULONG Id, ULONG* TypeSupport) override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // Streaming control
    HRESULT StartStreaming();
    HRESULT StopStreaming();

private:
    void WorkerLoop();
    void RenderDefaultPattern(uint8_t* dst, uint32_t width, uint32_t height, uint32_t pitch, uint64_t frame_seq);

    DuwnVirtualCamFilter*   m_parent{nullptr};
    std::atomic<LONG>       m_ref_count{1};
    IPin*                   m_connected_pin{nullptr};
    AM_MEDIA_TYPE           m_media_type{};
    IMemAllocator*          m_allocator{nullptr};
    IMemInputPin*           m_mem_input{nullptr};

    std::thread             m_worker;
    std::atomic<bool>       m_streaming{false};

    // D3D11 for shared texture sampling
    Microsoft::WRL::ComPtr<ID3D11Device>        m_d3d_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3d_context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     m_staging_tex;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     m_cached_shared_tex[4];
    HANDLE                                      m_cached_handles[4]{nullptr, nullptr, nullptr, nullptr};
    uint32_t                                    m_staging_w{0};
    uint32_t                                    m_staging_h{0};
    uint32_t                                    m_cached_gen{0};
    uint32_t                                    m_cached_res_gen{0};
};

} // namespace duwn::vcam