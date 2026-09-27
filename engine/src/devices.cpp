// MixCast engine - audio endpoint and process discovery.
#include "devices.h"

#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audiopolicy.h>
#include <tlhelp32.h>

#include <algorithm>
#include <iterator>
#include <cwctype>
#include <map>
#include <set>

namespace mixcast {

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle)
{
    auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };
    return lower(haystack).find(lower(needle)) != std::wstring::npos;
}

static ComPtr<IMMDeviceEnumerator> Enumerator()
{
    ComPtr<IMMDeviceEnumerator> e;
    MC_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    return e;
}

static std::wstring FriendlyName(IMMDevice* dev)
{
    ComPtr<IPropertyStore> props;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return L"(unknown)";

    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring name = L"(unknown)";
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
        name = pv.pwszVal;
    PropVariantClear(&pv);
    return name;
}

static std::wstring DeviceId(IMMDevice* dev)
{
    LPWSTR raw = nullptr;
    std::wstring id;
    if (SUCCEEDED(dev->GetId(&raw)) && raw) { id = raw; CoTaskMemFree(raw); }
    return id;
}

std::vector<Endpoint> ListEndpoints(EDataFlow flow)
{
    auto e = Enumerator();

    std::wstring defaultId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(e->GetDefaultAudioEndpoint(flow, eConsole, &def))) defaultId = DeviceId(def.Get());

    ComPtr<IMMDeviceCollection> coll;
    MC_CHECK(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll));

    UINT count = 0;
    coll->GetCount(&count);

    std::vector<Endpoint> out;
    for (UINT i = 0; i < count; i++)
    {
        ComPtr<IMMDevice> dev;
        if (FAILED(coll->Item(i, &dev))) continue;
        Endpoint ep;
        ep.id        = DeviceId(dev.Get());
        ep.name      = FriendlyName(dev.Get());
        ep.isDefault = (ep.id == defaultId);
        out.push_back(std::move(ep));
    }
    return out;
}

std::optional<Endpoint> FindEndpoint(EDataFlow flow, const std::wstring& part)
{
    for (auto& ep : ListEndpoints(flow))
        if (ContainsNoCase(ep.name, part)) return ep;
    return std::nullopt;
}

std::optional<Endpoint> FindMixCastInput()
{
    return FindEndpoint(eRender, L"MixCast");
}

bool IsVirtualCable(const std::wstring& deviceName)
{
    return ContainsNoCase(deviceName, L"MixCast") ||
           ContainsNoCase(deviceName, L"VB-Audio") ||
           ContainsNoCase(deviceName, L"Virtual Cable");
}

std::vector<VirtualOutput> ListVirtualOutputs()
{
    std::vector<VirtualOutput> vb, mixcast;

    for (auto& ep : ListEndpoints(eRender))
    {
        // "CABLE Input (VB-Audio Virtual Cable)" -> endpoint part "CABLE Input"
        std::wstring part = ep.name;
        if (auto paren = part.find(L" ("); paren != std::wstring::npos) part.resize(paren);

        if (ContainsNoCase(ep.name, L"MixCast"))
        {
            mixcast.push_back({ ep, L"MixCast Mic", L"MixCast driver" });
        }
        else if (ContainsNoCase(ep.name, L"VB-Audio") && ContainsNoCase(part, L"Input"))
        {
            // Its recording twin swaps "Input" for "Output": CABLE Input -> CABLE Output.
            std::wstring mic = part;
            if (auto pos = mic.rfind(L"Input"); pos != std::wstring::npos) mic.replace(pos, 5, L"Output");
            vb.push_back({ ep, mic, L"VB-CABLE" });
        }
    }

    vb.insert(vb.end(), mixcast.begin(), mixcast.end());
    return vb;
}

std::optional<Endpoint> DefaultRealSpeakers()
{
    for (auto& ep : ListEndpoints(eRender))
        if (ep.isDefault && !IsVirtualCable(ep.name)) return ep;
    return std::nullopt;
}

std::optional<Endpoint> DefaultRealMic()
{
    auto all = ListEndpoints(eCapture);
    for (auto& ep : all)
        if (ep.isDefault && !IsVirtualCable(ep.name)) return ep;
    for (auto& ep : all)
        if (!IsVirtualCable(ep.name)) return ep;
    return std::nullopt;
}

static std::map<DWORD, PROCESSENTRY32W> SnapshotProcesses()
{
    std::map<DWORD, PROCESSENTRY32W> procs;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return procs;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do { procs[pe.th32ProcessID] = pe; } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return procs;
}

std::vector<AudioProcess> ListAudioProcesses()
{
    auto procs = SnapshotProcesses();
    std::set<DWORD> seen;
    std::vector<AudioProcess> out;

    auto e = Enumerator();
    ComPtr<IMMDeviceCollection> coll;
    MC_CHECK(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll));
    UINT count = 0;
    coll->GetCount(&count);

    for (UINT i = 0; i < count; i++)
    {
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioSessionManager2> mgr;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(coll->Item(i, &dev))) continue;
        if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &mgr))) continue;
        if (FAILED(mgr->GetSessionEnumerator(&sessions))) continue;

        int n = 0;
        sessions->GetCount(&n);
        for (int s = 0; s < n; s++)
        {
            ComPtr<IAudioSessionControl> ctl;
            ComPtr<IAudioSessionControl2> ctl2;
            if (FAILED(sessions->GetSession(s, &ctl))) continue;
            if (FAILED(ctl.As(&ctl2))) continue;
            if (ctl2->IsSystemSoundsSession() == S_OK) continue;

            DWORD pid = 0;
            if (FAILED(ctl2->GetProcessId(&pid)) || pid == 0) continue;
            if (!seen.insert(pid).second) continue;

            AudioProcess ap;
            ap.pid = pid;
            auto it = procs.find(pid);
            ap.exe  = (it != procs.end()) ? it->second.szExeFile : L"(unknown)";
            ap.path = ProcessImagePath(pid);
            out.push_back(std::move(ap));
        }
    }

    std::sort(out.begin(), out.end(), [](const AudioProcess& a, const AudioProcess& b) { return a.exe < b.exe; });
    return out;
}

std::wstring ProcessImagePath(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(std::size(buf));
    std::wstring path;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) path.assign(buf, len);
    CloseHandle(h);
    return path;
}

std::optional<DWORD> FindRootProcess(const std::wstring& exeName)
{
    auto procs = SnapshotProcesses();

    std::set<DWORD> matches;
    for (auto& [pid, pe] : procs)
        if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) matches.insert(pid);

    // Prefer a match whose parent is NOT the same exe (the root of the tree).
    for (DWORD pid : matches)
        if (!matches.count(procs[pid].th32ParentProcessID)) return pid;

    if (!matches.empty()) return *matches.begin();
    return std::nullopt;
}

} // namespace mixcast
