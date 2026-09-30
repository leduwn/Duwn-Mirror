#include "WiredDeviceManager.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <setupapi.h>
#include <iphlpapi.h>
#include <algorithm>
#include <cwctype>
#include <format>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace duwn::wired {

WiredPhase ClassifyWiredPhase(uint32_t count, bool runtime, bool running) noexcept {
    if (count == 0) return WiredPhase::UsbNotConnected;
    if (!runtime) return WiredPhase::AppleRuntimeMissing;
    if (!running) return WiredPhase::AppleServiceStopped;
    return WiredPhase::WaitingForUsbNetwork;
}

namespace {
std::wstring DeviceProperty(HDEVINFO set, SP_DEVINFO_DATA& item, DWORD property) {
    wchar_t text[512]{};
    DWORD type = 0;
    if (!::SetupDiGetDeviceRegistryPropertyW(set, &item, property, &type,
            reinterpret_cast<PBYTE>(text), sizeof(text), nullptr)) return {};
    return text;
}

bool Contains(std::wstring text, const wchar_t* needle) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text.find(needle) != std::wstring::npos;
}
} // namespace

WiredSnapshot WiredDeviceManager::Probe(uint32_t airplay_pid) const noexcept {
    WiredSnapshot result;
    SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager) {
        SC_HANDLE service = ::OpenServiceW(manager, L"Apple Mobile Device Service", SERVICE_QUERY_STATUS);
        if (service) {
            result.apple_runtime_present = true;
            SERVICE_STATUS_PROCESS status{};
            DWORD bytes = 0;
            result.apple_service_running =
                ::QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                    reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes) &&
                status.dwCurrentState == SERVICE_RUNNING;
            ::CloseServiceHandle(service);
        }
        ::CloseServiceHandle(manager);
    }

    HDEVINFO set = ::SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA item{};
        item.cbSize = sizeof(item);
        for (DWORD index = 0; ::SetupDiEnumDeviceInfo(set, index, &item); ++index) {
            const std::wstring ids = DeviceProperty(set, item, SPDRP_HARDWAREID);
            if (!Contains(ids, L"vid_05ac")) continue;
            std::wstring name = DeviceProperty(set, item, SPDRP_FRIENDLYNAME);
            if (name.empty()) name = DeviceProperty(set, item, SPDRP_DEVICEDESC);
            if (!Contains(name, L"iphone") && !Contains(name, L"ipad") &&
                !Contains(name, L"ipod") && !Contains(name, L"apple mobile device")) continue;
            ++result.usb_interface_count;
            if (result.device_name.empty()) result.device_name = std::move(name);
        }
        ::SetupDiDestroyDeviceInfoList(set);
    }
    result.phase = ClassifyWiredPhase(result.usb_interface_count,
        result.apple_runtime_present, result.apple_service_running);
    if (result.usb_interface_count == 0) return result;

    ULONG size = 0;
    if (::GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW)
        return result;
    std::vector<BYTE> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (::GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &size) != NO_ERROR)
        return result;
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        if (!adapter->Description || !Contains(adapter->Description, L"apple mobile device ethernet"))
            continue;
        if (adapter->OperStatus != IfOperStatusUp) continue;
        for (auto* address = adapter->FirstUnicastAddress; address; address = address->Next) {
            if (!address->Address.lpSockaddr || address->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->Address.lpSockaddr);
            if (address->OnLinkPrefixLength < 1 || address->OnLinkPrefixLength > 32)
                continue;
            if (ipv4->sin_addr.S_un.S_un_b.s_b1 == 169 && ipv4->sin_addr.S_un.S_un_b.s_b2 == 254)
                continue;
            wchar_t text[INET_ADDRSTRLEN]{};
            if (!::InetNtopW(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), text, INET_ADDRSTRLEN))
                continue;
            result.network_ipv4 = text;
            result.network_prefix = address->OnLinkPrefixLength;
            result.network_if_index = adapter->IfIndex;
            result.network_luid = adapter->Luid.Value;
            result.network_up = true;
            if (adapter->AdapterName) {
                int length = ::MultiByteToWideChar(CP_ACP, 0, adapter->AdapterName, -1, nullptr, 0);
                if (length > 1) {
                    result.network_guid.resize(length);
                    ::MultiByteToWideChar(CP_ACP, 0, adapter->AdapterName, -1,
                                          result.network_guid.data(), length);
                    result.network_guid.pop_back();
                }
            }
            result.phase = WiredPhase::UsbNetworkReady;
            if (airplay_pid) {
                DWORD table_size = 0;
                if (::GetExtendedTcpTable(nullptr, &table_size, FALSE, AF_INET,
                        TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<BYTE> table_buffer(table_size);
                    if (::GetExtendedTcpTable(table_buffer.data(), &table_size, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(table_buffer.data());
                        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                            const auto& row = table->table[i];
                            if (row.dwOwningPid != airplay_pid || row.dwState != MIB_TCP_STATE_ESTAB ||
                                row.dwLocalAddr != ipv4->sin_addr.S_un.S_addr) continue;
                            IN_ADDR remote{};
                            remote.S_un.S_addr = row.dwRemoteAddr;
                            wchar_t peer[INET_ADDRSTRLEN]{};
                            if (!::InetNtopW(AF_INET, &remote, peer, INET_ADDRSTRLEN)) continue;
                            result.rtsp_local_endpoint = std::format(L"{}:{}", result.network_ipv4,
                                ::ntohs(static_cast<u_short>(row.dwLocalPort)));
                            result.rtsp_peer_endpoint = std::format(L"{}:{}", peer,
                                ::ntohs(static_cast<u_short>(row.dwRemotePort)));
                            break;
                        }
                    }
                }
            }
            return result;
        }
    }
    result.phase = WiredPhase::WaitingForUsbNetwork;
    return result;
}

} // namespace duwn::wired
