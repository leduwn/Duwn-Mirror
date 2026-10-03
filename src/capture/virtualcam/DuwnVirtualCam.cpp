#include "DuwnVirtualCam.h"
#include <initguid.h>
#include <cassert>
#include <chrono>

namespace duwn::vcam {

static inline uint32_t x_pos_dummy(uint32_t y, uint64_t f) {
    return static_cast<uint32_t>(y + (f * 2));
}

// Media type initialization helper
static void SetupMediaType(AM_MEDIA_TYPE* pmt, LONG width, LONG height, LONG avg_time_per_frame = 166666) {
    ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));
    pmt->majortype = MEDIATYPE_Video;
    pmt->subtype = MEDIASUBTYPE_RGB32;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->lSampleSize = width * height * 4;
    pmt->formattype = FORMAT_VideoInfo;

    VIDEOINFOHEADER* vih = static_cast<VIDEOINFOHEADER*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
    vih->rcSource = RECT{0, 0, width, height};
    vih->rcTarget = RECT{0, 0, width, height};
    vih->AvgTimePerFrame = avg_time_per_frame; // 60 FPS default
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = width;
    vih->bmiHeader.biHeight = height;
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = 32;
    vih->bmiHeader.biCompression = BI_RGB;
    vih->bmiHeader.biSizeImage = width * height * 4;

    pmt->cbFormat = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat = reinterpret_cast<BYTE*>(vih);
}

// ---------------------------------------------------------------------------
// EnumPins Implementation
// ---------------------------------------------------------------------------
class EnumPinsImpl final : public IEnumPins {
public:
    explicit EnumPinsImpl(IPin* pin) : m_pin(pin) {
        if (m_pin) m_pin->AddRef();
    }
    ~EnumPinsImpl() {
        if (m_pin) m_pin->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumPins) {
            *ppv = static_cast<IEnumPins*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override {
        if (!ppPins) return E_POINTER;
        if (cPins > 0 && m_pos == 0 && m_pin) {
            ppPins[0] = m_pin;
            m_pin->AddRef();
            m_pos = 1;
            if (pcFetched) *pcFetched = 1;
            return (cPins == 1) ? S_OK : S_FALSE;
        }
        if (pcFetched) *pcFetched = 0;
        return S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cPins) override {
        m_pos += cPins;
        return (m_pos <= 1) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        m_pos = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new EnumPinsImpl(m_pin);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{1};
    IPin*              m_pin{nullptr};
    ULONG              m_pos{0};
};

// ---------------------------------------------------------------------------
// EnumMediaTypes Implementation
// ---------------------------------------------------------------------------
class EnumMediaTypesImpl final : public IEnumMediaTypes {
public:
    explicit EnumMediaTypesImpl(const AM_MEDIA_TYPE& mt) {
        CopyMediaType(&m_mt, &mt);
    }
    ~EnumMediaTypesImpl() {
        FreeMediaType(m_mt);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pcFetched) override {
        if (!ppMediaTypes) return E_POINTER;
        if (cMediaTypes > 0 && m_pos == 0) {
            ppMediaTypes[0] = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            CopyMediaType(ppMediaTypes[0], &m_mt);
            m_pos = 1;
            if (pcFetched) *pcFetched = 1;
            return (cMediaTypes == 1) ? S_OK : S_FALSE;
        }
        if (pcFetched) *pcFetched = 0;
        return S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cMediaTypes) override {
        m_pos += cMediaTypes;
        return (m_pos <= 1) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        m_pos = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new EnumMediaTypesImpl(m_mt);
        return S_OK;
    }

private:
    static void CopyMediaType(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src) {
        *dst = *src;
        if (src->cbFormat > 0 && src->pbFormat) {
            dst->pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(src->cbFormat));
            CopyMemory(dst->pbFormat, src->pbFormat, src->cbFormat);
        }
    }
    static void FreeMediaType(AM_MEDIA_TYPE& mt) {
        if (mt.cbFormat > 0 && mt.pbFormat) {
            CoTaskMemFree(mt.pbFormat);
            mt.pbFormat = nullptr;
            mt.cbFormat = 0;
        }
    }

    std::atomic<ULONG> m_ref{1};
    AM_MEDIA_TYPE      m_mt{};
    ULONG              m_pos{0};
};

// ---------------------------------------------------------------------------
// DuwnVirtualCamFilter Implementation
// ---------------------------------------------------------------------------
DuwnVirtualCamFilter::DuwnVirtualCamFilter() {
    m_pin = new DuwnOutputPin(this);
}

DuwnVirtualCamFilter::~DuwnVirtualCamFilter() {
    Stop();
    if (m_pin) {
        m_pin->Release();
        m_pin = nullptr;
    }
    if (m_clock) {
        m_clock->Release();
        m_clock = nullptr;
    }
}

STDMETHODIMP DuwnVirtualCamFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IBaseFilter || riid == IID_IMediaFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IPersist) {
        *ppv = static_cast<IPersist*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DuwnVirtualCamFilter::AddRef() {
    return ++m_ref_count;
}

STDMETHODIMP_(ULONG) DuwnVirtualCamFilter::Release() {
    ULONG r = --m_ref_count;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP DuwnVirtualCamFilter::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_DuwnMirrorVirtualCam;
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::Stop() {
    if (m_pin) m_pin->StopStreaming();
    m_state = State_Stopped;
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::Pause() {
    m_state = State_Paused;
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::Run(REFERENCE_TIME tStart) {
    m_state = State_Running;
    if (m_pin) m_pin->StartStreaming();
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) {
    if (!State) return E_POINTER;
    *State = m_state;
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::SetSyncSource(IReferenceClock* pClock) {
    if (m_clock) m_clock->Release();
    m_clock = pClock;
    if (m_clock) m_clock->AddRef();
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::GetSyncSource(IReferenceClock** ppClock) {
    if (!ppClock) return E_POINTER;
    *ppClock = m_clock;
    if (*ppClock) (*ppClock)->AddRef();
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new EnumPinsImpl(m_pin);
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    if (m_pin && Id && wcscmp(Id, L"Capture") == 0) {
        *ppPin = m_pin;
        m_pin->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP DuwnVirtualCamFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    wcsncpy_s(pInfo->achName, m_filter_name.c_str(), sizeof(pInfo->achName) / sizeof(WCHAR) - 1);
    pInfo->pGraph = m_graph;
    if (pInfo->pGraph) pInfo->pGraph->AddRef();
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    m_graph = pGraph;
    if (pName) m_filter_name = pName;
    return S_OK;
}

STDMETHODIMP DuwnVirtualCamFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    const WCHAR vendor[] = L"Duwn Mirror";
    *pVendorInfo = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(vendor)));
    if (*pVendorInfo) CopyMemory(*pVendorInfo, vendor, sizeof(vendor));
    return *pVendorInfo ? S_OK : E_OUTOFMEMORY;
}

// ---------------------------------------------------------------------------
// DuwnOutputPin Implementation
// ---------------------------------------------------------------------------
DuwnOutputPin::DuwnOutputPin(DuwnVirtualCamFilter* parent) : m_parent(parent) {
    SetupMediaType(&m_media_type, 1920, 1080, 166666); // 1080p @ 60fps
}

DuwnOutputPin::~DuwnOutputPin() {
    Disconnect();
    if (m_media_type.pbFormat) {
        CoTaskMemFree(m_media_type.pbFormat);
        m_media_type.pbFormat = nullptr;
    }
}

STDMETHODIMP DuwnOutputPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IKsPropertySet) {
        *ppv = static_cast<IKsPropertySet*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IAMStreamConfig) {
        *ppv = static_cast<IAMStreamConfig*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DuwnOutputPin::AddRef() {
    return ++m_ref_count;
}

STDMETHODIMP_(ULONG) DuwnOutputPin::Release() {
    ULONG r = --m_ref_count;
    if (r == 0) delete this;
    return r;
}

STDMETHODIMP DuwnOutputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    if (m_connected_pin) return VFW_E_ALREADY_CONNECTED;

    const AM_MEDIA_TYPE* target_mt = pmt ? pmt : &m_media_type;
    HRESULT hr = pReceivePin->ReceiveConnection(this, target_mt);
    if (FAILED(hr)) return hr;

    m_connected_pin = pReceivePin;
    m_connected_pin->AddRef();

    hr = m_connected_pin->QueryInterface(IID_IMemInputPin, reinterpret_cast<void**>(&m_mem_input));
    if (FAILED(hr) || !m_mem_input) {
        Disconnect();
        return VFW_E_NO_ALLOCATOR;
    }

    hr = m_mem_input->GetAllocator(&m_allocator);
    if (FAILED(hr) || !m_allocator) {
        hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IMemAllocator, reinterpret_cast<void**>(&m_allocator));
    }

    if (SUCCEEDED(hr) && m_allocator) {
        ALLOCATOR_PROPERTIES props{}, actual{};
        props.cBuffers = 3;
        props.cbBuffer = m_media_type.lSampleSize;
        props.cbAlign = 1;
        m_allocator->SetProperties(&props, &actual);
        m_mem_input->NotifyAllocator(m_allocator, FALSE);
    }

    return S_OK;
}

STDMETHODIMP DuwnOutputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) {
    return E_UNEXPECTED; // Output pin does not accept inbound connections
}

STDMETHODIMP DuwnOutputPin::Disconnect() {
    StopStreaming();
    if (m_allocator) {
        m_allocator->Decommit();
        m_allocator->Release();
        m_allocator = nullptr;
    }
    if (m_mem_input) {
        m_mem_input->Release();
        m_mem_input = nullptr;
    }
    if (m_connected_pin) {
        m_connected_pin->Release();
        m_connected_pin = nullptr;
    }
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::ConnectedTo(IPin** pPin) {
    if (!pPin) return E_POINTER;
    *pPin = m_connected_pin;
    if (*pPin) (*pPin)->AddRef();
    return *pPin ? S_OK : VFW_E_NOT_CONNECTED;
}

STDMETHODIMP DuwnOutputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (!m_connected_pin) return VFW_E_NOT_CONNECTED;
    *pmt = m_media_type;
    if (m_media_type.cbFormat > 0 && m_media_type.pbFormat) {
        pmt->pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(m_media_type.cbFormat));
        CopyMemory(pmt->pbFormat, m_media_type.pbFormat, m_media_type.cbFormat);
    }
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_parent;
    if (pInfo->pFilter) pInfo->pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Capture");
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    const WCHAR pin_id[] = L"Capture";
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(pin_id)));
    if (*Id) CopyMemory(*Id, pin_id, sizeof(pin_id));
    return *Id ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP DuwnOutputPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->majortype == MEDIATYPE_Video && pmt->subtype == MEDIASUBTYPE_RGB32) {
        return S_OK;
    }
    return S_FALSE;
}

STDMETHODIMP DuwnOutputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new EnumMediaTypesImpl(m_media_type);
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::QueryInternalConnections(IPin** apPin, ULONG* nPin) { return E_NOTIMPL; }
STDMETHODIMP DuwnOutputPin::EndOfStream() { return S_OK; }
STDMETHODIMP DuwnOutputPin::BeginFlush() { return S_OK; }
STDMETHODIMP DuwnOutputPin::EndFlush() { return S_OK; }
STDMETHODIMP DuwnOutputPin::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) { return S_OK; }

// ---------------------------------------------------------------------------
// IKsPropertySet Implementation
// ---------------------------------------------------------------------------
STDMETHODIMP DuwnOutputPin::Set(REFGUID PropSet, ULONG Id, void* InstanceData, ULONG InstanceLength,
                                void* PropertyData, ULONG DataLength) {
    return E_NOTIMPL;
}

STDMETHODIMP DuwnOutputPin::Get(REFGUID PropSet, ULONG Id, void* InstanceData, ULONG InstanceLength,
                                void* PropertyData, ULONG DataLength, ULONG* BytesReturned) {
    if (PropSet == AMPROPSETID_Pin_Local && Id == 0 /* AMPROPERTY_PIN_CATEGORY */) {
        if (!PropertyData) return E_POINTER;
        if (DataLength < sizeof(GUID)) return E_UNEXPECTED;
        CopyMemory(PropertyData, &PIN_CATEGORY_CAPTURE_Local, sizeof(GUID));
        if (BytesReturned) *BytesReturned = sizeof(GUID);
        return S_OK;
    }
    return E_PROP_SET_UNSUPPORTED;
}

STDMETHODIMP DuwnOutputPin::QuerySupported(REFGUID PropSet, ULONG Id, ULONG* TypeSupport) {
    if (PropSet == AMPROPSETID_Pin_Local && Id == 0) {
        if (TypeSupport) *TypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    return E_PROP_SET_UNSUPPORTED;
}

// ---------------------------------------------------------------------------
// IAMStreamConfig Implementation
// ---------------------------------------------------------------------------
STDMETHODIMP DuwnOutputPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (QueryAccept(pmt) != S_OK) return VFW_E_INVALIDMEDIATYPE;
    if (m_media_type.pbFormat) CoTaskMemFree(m_media_type.pbFormat);
    m_media_type = *pmt;
    if (pmt->cbFormat > 0 && pmt->pbFormat) {
        m_media_type.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(pmt->cbFormat));
        CopyMemory(m_media_type.pbFormat, pmt->pbFormat, pmt->cbFormat);
    }
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    **ppmt = m_media_type;
    if (m_media_type.cbFormat > 0 && m_media_type.pbFormat) {
        (*ppmt)->pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(m_media_type.cbFormat));
        CopyMemory((*ppmt)->pbFormat, m_media_type.pbFormat, m_media_type.cbFormat);
    }
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = 1;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP DuwnOutputPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex != 0) return S_FALSE;

    GetFormat(ppmt);

    VIDEO_STREAM_CONFIG_CAPS* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    ZeroMemory(caps, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    caps->guid = FORMAT_VideoInfo;
    caps->VideoStandard = 0;
    caps->InputSize = SIZE{1920, 1080};
    caps->MinCroppingSize = SIZE{1920, 1080};
    caps->MaxCroppingSize = SIZE{1920, 1080};
    caps->MinOutputSize = SIZE{1920, 1080};
    caps->MaxOutputSize = SIZE{1920, 1080};
    caps->MinFrameInterval = 166666; // 60 FPS
    caps->MaxFrameInterval = 166666;
    caps->MinBitsPerSecond = static_cast<DWORD>(1920ULL * 1080ULL * 4ULL * 8ULL * 30ULL);
    caps->MaxBitsPerSecond = static_cast<DWORD>(1920ULL * 1080ULL * 4ULL * 8ULL * 60ULL);
    return S_OK;
}

// ---------------------------------------------------------------------------
// Streaming Control and Worker Thread
// ---------------------------------------------------------------------------
HRESULT DuwnOutputPin::StartStreaming() {
    if (m_streaming.load()) return S_OK;
    if (!m_allocator) return VFW_E_NO_ALLOCATOR;

    m_allocator->Commit();
    m_streaming.store(true);
    m_worker = std::thread(&DuwnOutputPin::WorkerLoop, this);
    return S_OK;
}

HRESULT DuwnOutputPin::StopStreaming() {
    if (!m_streaming.load()) return S_OK;
    m_streaming.store(false);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (m_allocator) {
        m_allocator->Decommit();
    }
    return S_OK;
}

void DuwnOutputPin::RenderDefaultPattern(uint8_t* dst, uint32_t width, uint32_t height, uint32_t pitch, uint64_t frame_seq) {
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(dst + (y * pitch));
        uint8_t b = static_cast<uint8_t>((y * 40) / height + 20);
        uint8_t g = static_cast<uint8_t>((x_pos_dummy(y, frame_seq)) % 30 + 15);
        uint8_t r = 25;
        uint32_t color = (0xFF << 24) | (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = color;
        }
    }
}

void DuwnOutputPin::WorkerLoop() {
    using namespace duwn::capture;

    ::timeBeginPeriod(1);

    if (!m_d3d_device) {
        D3D_FEATURE_LEVEL fl;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                          D3D11_SDK_VERSION, &m_d3d_device, &fl, &m_d3d_context);
        if (!m_d3d_device) {
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                              D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                              D3D11_SDK_VERSION, &m_d3d_device, &fl, &m_d3d_context);
        }
    }

    HANDLE h_map = nullptr;
    CaptureMemoryHeader* header = nullptr;
    HANDLE h_event = nullptr;
    HANDLE h_my_event = nullptr;
    int32_t my_consumer_slot = -1;
    uint64_t last_frame_index = 0;
    uint64_t local_seq = 0;
    REFERENCE_TIME rt_frame_duration = 166666; // ~16.6ms (60 FPS)
    REFERENCE_TIME rt_current = 0;
    auto last_producer_activity = std::chrono::steady_clock::now();

    while (m_streaming.load(std::memory_order_relaxed)) {
        if (!h_map) {
            h_map = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\DUWN_MIRROR_CAPTURE");
            if (h_map) {
                header = static_cast<CaptureMemoryHeader*>(
                    ::MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CaptureMemoryHeader)));
                if (header) {
                    wchar_t my_evt_name[64];
                    swprintf_s(my_evt_name, L"Local\\DUWN_FRAME_EVENT_%u", ::GetCurrentProcessId());
                    h_my_event = ::CreateEventW(nullptr, FALSE, FALSE, my_evt_name);
                    my_consumer_slot = RegisterConsumer(header, ::GetCurrentProcessId(), my_evt_name);
                }
            }
        }
        if (!h_event) {
            h_event = ::OpenEventW(SYNCHRONIZE, FALSE, L"Local\\DUWN_MIRROR_CAPTURE_FRAME_READY");
        }

        bool got_clean_frame = false;
        CaptureMemoryHeader snapshot{};

        if (header) {
            HANDLE wait_target = h_my_event ? h_my_event : h_event;
            DWORD wr = wait_target ? ::WaitForSingleObject(wait_target, 20) : WAIT_TIMEOUT;
            (void)wr;

            if (ReadHeaderConsistent(header, snapshot)) {
                bool gen_changed = (snapshot.generation != m_cached_gen);
                bool res_gen_changed = (snapshot.resource_generation != m_cached_res_gen);
                bool consumer_missing = (my_consumer_slot < 0 ||
                    (my_consumer_slot < 4 && header->consumers[my_consumer_slot].process_id != ::GetCurrentProcessId()));

                if (gen_changed || res_gen_changed || consumer_missing) {
                    m_staging_tex.Reset();
                    m_staging_w = 0;
                    m_staging_h = 0;
                    for (int k = 0; k < 4; ++k) {
                        m_cached_shared_tex[k].Reset();
                        m_cached_handles[k] = nullptr;
                    }
                    m_cached_gen = snapshot.generation;
                    m_cached_res_gen = snapshot.resource_generation;
                    last_frame_index = 0;

                    wchar_t my_evt_name[64];
                    swprintf_s(my_evt_name, L"Local\\DUWN_FRAME_EVENT_%u", ::GetCurrentProcessId());
                    my_consumer_slot = RegisterConsumer(header, ::GetCurrentProcessId(), my_evt_name);
                }

                if (std::memcmp(snapshot.magic, "DUWNCAP", 7) == 0 &&
                    snapshot.protocol_version == 2 &&
                    (snapshot.frame_index > last_frame_index || last_frame_index == 0)) {
                    last_frame_index = snapshot.frame_index;
                    got_clean_frame = true;
                    last_producer_activity = std::chrono::steady_clock::now();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        IMediaSample* pSample = nullptr;
        HRESULT hr = m_allocator->GetBuffer(&pSample, nullptr, nullptr, 0);
        if (FAILED(hr) || !pSample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        BYTE* pBuffer = nullptr;
        hr = pSample->GetPointer(&pBuffer);
        if (SUCCEEDED(hr) && pBuffer) {
            const uint32_t out_w = 1920;
            const uint32_t out_h = 1080;
            const uint32_t row_pitch = out_w * 4;

            if (got_clean_frame && m_d3d_device && m_d3d_context) {
                uint32_t active_idx = snapshot.active_buffer_index;
                HANDLE shared_h = reinterpret_cast<HANDLE>(snapshot.shared_handles[active_idx < 4 ? active_idx : 0]);

                // Acquire ring slot lease during texture access using TryAcquireRingSlot
                LARGE_INTEGER qpc_now{};
                ::QueryPerformanceCounter(&qpc_now);
                bool slot_acquired = false;
                if (header && my_consumer_slot >= 0) {
                    slot_acquired = TryAcquireRingSlot(header, my_consumer_slot, active_idx, snapshot.frame_index, qpc_now.QuadPart);
                    if (!slot_acquired) {
                        pSample->Release();
                        continue;
                    }
                }

                uint32_t slot = active_idx < 4 ? active_idx : 0;
                if (!m_cached_shared_tex[slot] || m_cached_handles[slot] != shared_h) {
                    m_cached_shared_tex[slot].Reset();
                    m_d3d_device->OpenSharedResource(shared_h, IID_PPV_ARGS(&m_cached_shared_tex[slot]));
                    m_cached_handles[slot] = shared_h;
                }
                auto& shared_tex = m_cached_shared_tex[slot];
                if (shared_tex) {
                    if (!m_staging_tex || m_staging_w != snapshot.width || m_staging_h != snapshot.height) {
                        D3D11_TEXTURE2D_DESC sdesc{};
                        shared_tex->GetDesc(&sdesc);
                        sdesc.Usage = D3D11_USAGE_STAGING;
                        sdesc.BindFlags = 0;
                        sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        sdesc.MiscFlags = 0;
                        m_d3d_device->CreateTexture2D(&sdesc, nullptr, m_staging_tex.ReleaseAndGetAddressOf());
                        m_staging_w = snapshot.width;
                        m_staging_h = snapshot.height;
                    }

                    if (m_staging_tex) {
                        m_d3d_context->CopyResource(m_staging_tex.Get(), shared_tex.Get());

                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        hr = m_d3d_context->Map(m_staging_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                        if (SUCCEEDED(hr)) {
                            const uint8_t* src_px = static_cast<const uint8_t*>(mapped.pData);
                            uint32_t copy_lines = (snapshot.height < out_h) ? snapshot.height : out_h;
                            uint32_t copy_pitch = (snapshot.width * 4 < row_pitch) ? snapshot.width * 4 : row_pitch;
                            // DirectShow VIDEOINFOHEADER has biHeight > 0 (bottom-up DIB)
                            // Invert scanlines so top row in DirectX texture is top row visually
                            for (uint32_t y = 0; y < copy_lines; ++y) {
                                uint32_t dst_y = copy_lines - 1 - y;
                                CopyMemory(pBuffer + (dst_y * row_pitch), src_px + (y * mapped.RowPitch), copy_pitch);
                            }
                            m_d3d_context->Unmap(m_staging_tex.Get(), 0);
                        }
                    }
                }

                // Release ring slot lease immediately after copy
                if (header && my_consumer_slot >= 0 && slot_acquired) {
                    ReleaseRingSlot(header, my_consumer_slot);
                }
            } else {
                auto time_since_active = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - last_producer_activity).count();
                if (time_since_active > 500 || last_frame_index == 0) {
                    RenderDefaultPattern(pBuffer, out_w, out_h, row_pitch, ++local_seq);
                } else {
                    pSample->Release();
                    std::this_thread::yield();
                    continue;
                }
            }

            pSample->SetActualDataLength(out_w * out_h * 4);
            REFERENCE_TIME rt_start = rt_current;
            REFERENCE_TIME rt_end   = rt_current + rt_frame_duration;
            rt_current += rt_frame_duration;

            pSample->SetTime(&rt_start, &rt_end);
            pSample->SetSyncPoint(TRUE);

            if (m_mem_input) {
                m_mem_input->Receive(pSample);
            }
        }

        pSample->Release();
        if (!got_clean_frame) {
            std::this_thread::yield();
        }
    }

    ::timeEndPeriod(1);

    if (header) {
        if (my_consumer_slot >= 0) {
            UnregisterConsumer(header, my_consumer_slot);
        }
        ::UnmapViewOfFile(header);
    }
    if (h_map) {
        ::CloseHandle(h_map);
    }
    if (h_event) {
        ::CloseHandle(h_event);
    }
    if (h_my_event) {
        ::CloseHandle(h_my_event);
    }
}

}
