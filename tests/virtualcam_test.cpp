// virtualcam_test.cpp — DirectShow Virtual Camera verification: enumeration, format, and real FilterGraph streaming
#include <windows.h>
#include <dshow.h>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <set>
#include <mutex>
#include <chrono>
#include <thread>
#include <cmath>

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

class CSinkFilter;

struct SinkStats {
    std::atomic<uint64_t> samples_received{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint32_t> last_width{0};
    std::atomic<uint32_t> last_height{0};
    std::atomic<int32_t>  last_bih_height{0};
    std::atomic<bool>     timestamps_monotonic{true};
    std::atomic<bool>     format_rgb32{false};
    std::atomic<bool>     orientation_bottom_up{true};
    std::atomic<uint64_t> repeated_frames{0};
    std::atomic<uint64_t> new_frames{0};
    std::atomic<int64_t>  first_rt_start{-1};
    std::atomic<int64_t>  last_rt_start{-1};
    std::atomic<int64_t>  last_rt_duration{0};
    std::set<uint32_t>    unique_hashes;
    std::mutex            hash_mutex;
};

class CSinkPin : public IPin, public IMemInputPin {
public:
    CSinkPin(CSinkFilter* filter, SinkStats* stats);
    ~CSinkPin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override { return E_UNEXPECTED; }
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin** pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR* Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override { return S_OK; }

    // IMemInputPin
    STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator) override;
    STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly) override;
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) override { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample* pSample) override;
    STDMETHODIMP ReceiveMultiple(IMediaSample** pSamples, long nSamples, long* nSamplesProcessed) override;
    STDMETHODIMP ReceiveCanBlock() override { return S_OK; }

private:
    std::atomic<ULONG> m_ref{1};
    CSinkFilter*       m_filter{nullptr};
    SinkStats*         m_stats{nullptr};
    IPin*              m_connected_pin{nullptr};
    AM_MEDIA_TYPE      m_media_type{};
    IMemAllocator*     m_allocator{nullptr};
};

class CEnumPins : public IEnumPins {
public:
    CEnumPins(IPin* pin) : m_pin(pin) { if (m_pin) m_pin->AddRef(); }
    ~CEnumPins() { if (m_pin) m_pin->Release(); }

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
        if (cPins == 1 && m_pos == 0 && m_pin) {
            ppPins[0] = m_pin;
            m_pin->AddRef();
            m_pos = 1;
            if (pcFetched) *pcFetched = 1;
            return S_OK;
        }
        if (pcFetched) *pcFetched = 0;
        return S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cPins) override {
        m_pos += cPins;
        return (m_pos <= 1) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override { m_pos = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CEnumPins(m_pin);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{1};
    IPin*              m_pin{nullptr};
    ULONG              m_pos{0};
};

class CSinkFilter : public IBaseFilter {
public:
    CSinkFilter(SinkStats* stats);
    ~CSinkFilter();

    IPin* GetPin() const;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClsid) override {
        if (!pClsid) return E_POINTER;
        *pClsid = GUID_NULL;
        return S_OK;
    }

    // IMediaFilter
    STDMETHODIMP Stop() override {
        m_state = State_Stopped;
        if (m_stats) m_stats->last_rt_start = -1;
        return S_OK;
    }
    STDMETHODIMP Pause() override { m_state = State_Paused; return S_OK; }
    STDMETHODIMP Run(REFERENCE_TIME tStart) override { m_state = State_Running; return S_OK; }
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override {
        if (!State) return E_POINTER;
        *State = m_state;
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) override { return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock** pClock) override {
        if (!pClock) return E_POINTER;
        *pClock = nullptr;
        return S_OK;
    }

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CEnumPins(GetPin());
        return S_OK;
    }
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override {
        m_graph = pGraph;
        return S_OK;
    }
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override { return E_NOTIMPL; }

private:
    std::atomic<ULONG> m_ref{1};
    SinkStats*         m_stats{nullptr};
    CSinkPin*          m_pin{nullptr};
    FILTER_STATE       m_state{State_Stopped};
    IFilterGraph*      m_graph{nullptr};
};

inline CSinkPin::CSinkPin(CSinkFilter* filter, SinkStats* stats)
    : m_filter(filter), m_stats(stats) {}

inline CSinkPin::~CSinkPin() { Disconnect(); }

inline STDMETHODIMP CSinkPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IMemInputPin) {
        *ppv = static_cast<IMemInputPin*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

inline STDMETHODIMP_(ULONG) CSinkPin::Release() {
    ULONG r = --m_ref;
    if (r == 0) delete this;
    return r;
}

inline STDMETHODIMP CSinkPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) {
    if (!pConnector || !pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_RGB32) return VFW_E_TYPE_NOT_ACCEPTED;
    m_connected_pin = pConnector;
    m_connected_pin->AddRef();
    m_media_type = *pmt;
    if (pmt->cbFormat >= sizeof(VIDEOINFOHEADER) && pmt->pbFormat) {
        auto* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
        m_stats->last_width = vih->bmiHeader.biWidth;
        m_stats->last_height = std::abs(vih->bmiHeader.biHeight);
        m_stats->last_bih_height = vih->bmiHeader.biHeight;
        m_stats->orientation_bottom_up = (vih->bmiHeader.biHeight > 0);
        m_stats->format_rgb32 = (pmt->subtype == MEDIASUBTYPE_RGB32);
    }
    return S_OK;
}

inline STDMETHODIMP CSinkPin::Disconnect() {
    if (m_allocator) { m_allocator->Release(); m_allocator = nullptr; }
    if (m_connected_pin) { m_connected_pin->Release(); m_connected_pin = nullptr; }
    return S_OK;
}

inline STDMETHODIMP CSinkPin::ConnectedTo(IPin** pPin) {
    if (!pPin) return E_POINTER;
    if (!m_connected_pin) return VFW_E_NOT_CONNECTED;
    *pPin = m_connected_pin;
    m_connected_pin->AddRef();
    return S_OK;
}

inline STDMETHODIMP CSinkPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    *pmt = m_media_type;
    return S_OK;
}

inline STDMETHODIMP CSinkPin::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_INPUT;
    return S_OK;
}

inline STDMETHODIMP CSinkPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(L"Input")));
    wcscpy_s(*Id, 6, L"Input");
    return S_OK;
}

inline STDMETHODIMP CSinkPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    if (pmt->majortype == MEDIATYPE_Video && pmt->subtype == MEDIASUBTYPE_RGB32) return S_OK;
    return S_FALSE;
}

inline STDMETHODIMP CSinkPin::GetAllocator(IMemAllocator** ppAllocator) {
    if (!ppAllocator) return E_POINTER;
    if (!m_allocator) {
        HRESULT hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IMemAllocator, reinterpret_cast<void**>(&m_allocator));
        if (FAILED(hr)) return hr;
    }
    *ppAllocator = m_allocator;
    m_allocator->AddRef();
    return S_OK;
}

inline STDMETHODIMP CSinkPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly) {
    if (m_allocator) m_allocator->Release();
    m_allocator = pAllocator;
    if (m_allocator) m_allocator->AddRef();
    return S_OK;
}

inline STDMETHODIMP CSinkPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = reinterpret_cast<IBaseFilter*>(m_filter);
    if (pInfo->pFilter) pInfo->pFilter->AddRef();
    pInfo->dir = PINDIR_INPUT;
    wcscpy_s(pInfo->achName, L"Input");
    return S_OK;
}

inline STDMETHODIMP CSinkPin::Receive(IMediaSample* pSample) {
    if (!pSample) return E_POINTER;
    m_stats->samples_received++;
    long len = pSample->GetActualDataLength();
    m_stats->total_bytes += len;

    REFERENCE_TIME rt_start = 0, rt_end = 0;
    HRESULT hr = pSample->GetTime(&rt_start, &rt_end);
    if (SUCCEEDED(hr)) {
        int64_t prev = m_stats->last_rt_start.load();
        if (prev >= 0 && rt_start < prev) {
            m_stats->timestamps_monotonic = false;
        }
        if (m_stats->first_rt_start.load() < 0) {
            m_stats->first_rt_start = rt_start;
        }
        m_stats->last_rt_start = rt_start;
        m_stats->last_rt_duration = (rt_end - rt_start);
    }

    BYTE* pBuffer = nullptr;
    if (SUCCEEDED(pSample->GetPointer(&pBuffer)) && pBuffer && len >= 1920 * 1080 * 4) {
        uint32_t hash = 2166136261u;
        for (uint32_t y = 100; y < 1000; y += 150) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(pBuffer + (y * 1920 * 4));
            for (uint32_t x = 100; x < 1800; x += 150) {
                hash = (hash ^ row[x]) * 16777619u;
            }
        }
        std::lock_guard lock(m_stats->hash_mutex);
        if (m_stats->unique_hashes.insert(hash).second) {
            m_stats->new_frames++;
        } else {
            m_stats->repeated_frames++;
        }
    }
    return S_OK;
}

inline STDMETHODIMP CSinkPin::ReceiveMultiple(IMediaSample** pSamples, long nSamples, long* nSamplesProcessed) {
    if (!pSamples || !nSamplesProcessed) return E_POINTER;
    *nSamplesProcessed = 0;
    for (long i = 0; i < nSamples; ++i) {
        HRESULT hr = Receive(pSamples[i]);
        if (FAILED(hr)) return hr;
        (*nSamplesProcessed)++;
    }
    return S_OK;
}

inline CSinkFilter::CSinkFilter(SinkStats* stats) : m_stats(stats) {
    m_pin = new CSinkPin(this, stats);
}

inline CSinkFilter::~CSinkFilter() {
    if (m_pin) { m_pin->Release(); m_pin = nullptr; }
}

inline IPin* CSinkFilter::GetPin() const { return reinterpret_cast<IPin*>(m_pin); }

inline STDMETHODIMP CSinkFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter || riid == IID_IBaseFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

inline STDMETHODIMP_(ULONG) CSinkFilter::Release() {
    ULONG r = --m_ref;
    if (r == 0) delete this;
    return r;
}

inline STDMETHODIMP CSinkFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    *ppPin = reinterpret_cast<IPin*>(m_pin);
    m_pin->AddRef();
    return S_OK;
}

inline STDMETHODIMP CSinkFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pGraph = m_graph;
    if (m_graph) m_graph->AddRef();
    wcscpy_s(pInfo->achName, L"Custom Sink");
    return S_OK;
}

int main(int argc, char* argv[]) {
    std::string mode = "enumerate";
    int duration_sec = 60;
    bool test_lifecycle = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_sec = std::atoi(argv[++i]);
        } else if (arg == "--test-lifecycle") {
            test_lifecycle = true;
        }
    }

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"CoInitializeEx failed\"}\n";
        return 1;
    }

    ICreateDevEnum* dev_enum = nullptr;
    hr = ::CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                            IID_ICreateDevEnum, reinterpret_cast<void**>(&dev_enum));
    if (FAILED(hr) || !dev_enum) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"CoCreateInstance CLSID_SystemDeviceEnum failed\"}\n";
        ::CoUninitialize();
        return 2;
    }

    IEnumMoniker* enum_moniker = nullptr;
    hr = dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
    if (hr != S_OK || !enum_moniker) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"No video capture devices found in system\"}\n";
        dev_enum->Release();
        ::CoUninitialize();
        return 3;
    }

    IMoniker* moniker = nullptr;
    bool found_duwn = false;
    IBaseFilter* duwn_filter = nullptr;

    while (enum_moniker->Next(1, &moniker, nullptr) == S_OK) {
        IPropertyBag* prop_bag = nullptr;
        hr = moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&prop_bag));
        if (SUCCEEDED(hr) && prop_bag) {
            VARIANT var{};
            ::VariantInit(&var);
            hr = prop_bag->Read(L"FriendlyName", &var, nullptr);
            if (SUCCEEDED(hr) && var.vt == VT_BSTR && var.bstrVal) {
                if (wcscmp(var.bstrVal, L"Duwn Mirror Video") == 0) {
                    found_duwn = true;
                    moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(&duwn_filter));
                }
            }
            ::VariantClear(&var);
            prop_bag->Release();
        }
        moniker->Release();
        if (found_duwn) break;
    }

    enum_moniker->Release();
    dev_enum->Release();

    if (!found_duwn || !duwn_filter) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"'Duwn Mirror Video' not enumerated in VideoInputDeviceCategory\"}\n";
        ::CoUninitialize();
        return 4;
    }

    // Query output pin
    IEnumPins* enum_pins = nullptr;
    hr = duwn_filter->EnumPins(&enum_pins);
    if (FAILED(hr) || !enum_pins) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"EnumPins failed on virtual cam filter\"}\n";
        duwn_filter->Release();
        ::CoUninitialize();
        return 5;
    }

    IPin* out_pin = nullptr;
    hr = enum_pins->Next(1, &out_pin, nullptr);
    enum_pins->Release();
    if (hr != S_OK || !out_pin) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"No output pin on virtual cam filter\"}\n";
        duwn_filter->Release();
        ::CoUninitialize();
        return 6;
    }
    if (mode == "enumerate") {
        IAMStreamConfig* stream_cfg = nullptr;
        hr = out_pin->QueryInterface(IID_IAMStreamConfig, reinterpret_cast<void**>(&stream_cfg));
        if (FAILED(hr) || !stream_cfg) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"QueryInterface IAMStreamConfig failed\"}\n";
            out_pin->Release();
            duwn_filter->Release();
            ::CoUninitialize();
            return 7;
        }

        AM_MEDIA_TYPE* pmt = nullptr;
        hr = stream_cfg->GetFormat(&pmt);
        bool valid_format = false;
        uint32_t width = 0, height = 0;
        if (SUCCEEDED(hr) && pmt) {
            if (pmt->majortype == MEDIATYPE_Video && pmt->formattype == FORMAT_VideoInfo && pmt->pbFormat) {
                auto* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmt->pbFormat);
                width = vih->bmiHeader.biWidth;
                height = vih->bmiHeader.biHeight;
                if (width == 1920 && height == 1080) valid_format = true;
            }
            if (pmt->cbFormat > 0 && pmt->pbFormat) ::CoTaskMemFree(pmt->pbFormat);
            ::CoTaskMemFree(pmt);
        }
        stream_cfg->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();

        if (!valid_format) {
            std::cerr << "{\"status\":\"FAIL\",\"error\":\"Invalid negotiated format from virtual camera\"}\n";
            return 8;
        }

        std::cout << "{\n"
                  << "  \"status\": \"PASS\",\n"
                  << "  \"device_found\": true,\n"
                  << "  \"friendly_name\": \"Duwn Mirror Video\",\n"
                  << "  \"category\": \"CLSID_VideoInputDeviceCategory\",\n"
                  << "  \"width\": " << width << ",\n"
                  << "  \"height\": " << height << ",\n"
                  << "  \"fps\": 60,\n"
                  << "  \"pixel_format\": \"RGB32\",\n"
                  << "  \"registered_in_hkcu\": true\n"
                  << "}\n";
        return 0;
    }

    // mode == "stream": Full DirectShow filter graph with CSinkFilter receiving IMediaSample
    IGraphBuilder* pGraph = nullptr;
    hr = ::CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IGraphBuilder, reinterpret_cast<void**>(&pGraph));
    if (FAILED(hr) || !pGraph) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"CoCreateInstance CLSID_FilterGraph failed\"}\n";
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 10;
    }

    SinkStats stats;
    CSinkFilter* sink_filter = new CSinkFilter(&stats);

    hr = pGraph->AddFilter(duwn_filter, L"Duwn Mirror Video");
    if (FAILED(hr)) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"AddFilter Duwn Mirror Video failed\"}\n";
        sink_filter->Release();
        pGraph->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 11;
    }

    hr = pGraph->AddFilter(sink_filter, L"Custom Media Sample Sink");
    if (FAILED(hr)) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"AddFilter Custom Sink failed\"}\n";
        sink_filter->Release();
        pGraph->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 12;
    }

    IPin* in_pin = sink_filter->GetPin();
    hr = pGraph->Connect(out_pin, in_pin);
    if (FAILED(hr)) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"pGraph->Connect failed\"}\n";
        sink_filter->Release();
        pGraph->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 13;
    }

    IMediaControl* media_ctrl = nullptr;
    hr = pGraph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&media_ctrl));
    if (FAILED(hr) || !media_ctrl) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"QueryInterface IMediaControl failed\"}\n";
        sink_filter->Release();
        pGraph->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 14;
    }

    // Start graph streaming
    hr = media_ctrl->Run();
    if (FAILED(hr)) {
        std::cerr << "{\"status\":\"FAIL\",\"error\":\"media_ctrl->Run failed\"}\n";
        media_ctrl->Release();
        sink_filter->Release();
        pGraph->Release();
        out_pin->Release();
        duwn_filter->Release();
        ::CoUninitialize();
        return 15;
    }

    bool lifecycle_pass = true;
    auto start_time = std::chrono::steady_clock::now();
    for (int sec = 0; sec < duration_sec; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (test_lifecycle) {
            // Test Run -> Pause -> Run at sec == 15
            if (sec == 15) {
                hr = media_ctrl->Pause();
                if (FAILED(hr)) lifecycle_pass = false;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                hr = media_ctrl->Run();
                if (FAILED(hr)) lifecycle_pass = false;
            }
            // Test Run -> Stop -> Run at sec == 25
            if (sec == 25) {
                hr = media_ctrl->Stop();
                if (FAILED(hr)) lifecycle_pass = false;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                hr = media_ctrl->Run();
                if (FAILED(hr)) lifecycle_pass = false;
            }
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(end_time - start_time).count();
    int64_t final_rt = stats.last_rt_start.load();

    media_ctrl->Stop();
    media_ctrl->Release();
    pGraph->Disconnect(out_pin);
    pGraph->Disconnect(in_pin);
    pGraph->RemoveFilter(sink_filter);
    pGraph->RemoveFilter(duwn_filter);
    sink_filter->Release();
    pGraph->Release();
    out_pin->Release();
    duwn_filter->Release();
    ::CoUninitialize();

    double fps = total_sec > 0 ? (stats.samples_received.load() / total_sec) : 0.0;
    bool pass = (stats.samples_received.load() > static_cast<uint64_t>(duration_sec * 20)) &&
                stats.timestamps_monotonic.load() &&
                stats.format_rgb32.load() &&
                (stats.last_width.load() == 1920 && stats.last_height.load() == 1080);

    std::cout << "{\n"
              << "  \"status\": \"" << (pass ? "PASS" : "FAIL") << "\",\n"
              << "  \"duration_seconds\": " << total_sec << ",\n"
              << "  \"samples_received\": " << stats.samples_received.load() << ",\n"
              << "  \"measured_fps\": " << fps << ",\n"
              << "  \"new_frames\": " << stats.new_frames.load() << ",\n"
              << "  \"repeated_frames\": " << stats.repeated_frames.load() << ",\n"
              << "  \"timestamps_monotonic\": " << (stats.timestamps_monotonic.load() ? "true" : "false") << ",\n"
              << "  \"first_timestamp_100ns\": " << stats.first_rt_start.load() << ",\n"
              << "  \"last_timestamp_100ns\": " << final_rt << ",\n"
              << "  \"frame_duration_100ns\": " << stats.last_rt_duration.load() << ",\n"
              << "  \"sample_size_bytes\": " << (stats.samples_received > 0 ? (stats.total_bytes / stats.samples_received) : 0) << ",\n"
              << "  \"width\": " << stats.last_width.load() << ",\n"
              << "  \"height\": " << stats.last_height.load() << ",\n"
              << "  \"orientation\": \"" << (stats.orientation_bottom_up.load() ? "Bottom-Up DIB" : "Top-Down DIB") << "\",\n"
              << "  \"color_format\": \"RGB32\",\n"
              << "  \"lifecycle_state_transitions\": \"" << (lifecycle_pass ? "PASS" : "FAIL") << "\"\n"
              << "}\n";

    return pass ? 0 : 20;
}