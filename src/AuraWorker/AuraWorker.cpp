#include <initguid.h>
#include "CanonicalAuraAbi.h"
#include "WorkerWire.h"
#include <wrl/client.h>
#include <unordered_map>
#include <functional>
using Microsoft::WRL::ComPtr;
static std::string requestId;
static unsigned sequence = 0;
static void Stage(const char* stage) {
    WriteFrame({{"protocolVersion",1},{"requestId",requestId},{"sequence",sequence++},
                {"kind","progress"},{"stage",stage},{"workerPid",GetCurrentProcessId()}});
}
static int State(HRESULT hr) { return SUCCEEDED(hr) ? 1 : (hr == E_ACCESSDENIED ? 3 : 2); }
static Json Observation(HRESULT hr, const Json& value) {
    return {{"execution",State(hr)},{"hresult",static_cast<int>(hr)},
            {"value",SUCCEEDED(hr) ? value : Json(nullptr)}};
}
static std::string Utf8(BSTR text) {
    if (!text) return {};
    auto length = static_cast<int>(SysStringLen(text));
    if (length > 4096) throw std::runtime_error("VendorStringLimit");
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
    if(length>0 && size==0) throw std::runtime_error("VendorStringInvalid");
    std::string result(size, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, result.data(), size, nullptr, nullptr);
    return result;
}
template<class T, class F> static Json Get(F query) {
    Stage("property"); T value{}; HRESULT hr = query(&value); return Observation(hr, value);
}
template<class F> static Json GetString(F query) {
    Stage("property"); BSTR value = nullptr; HRESULT hr = query(&value);
    std::string text; try { if (SUCCEEDED(hr)) text = Utf8(value); } catch (...) { SysFreeString(value); throw; }
    SysFreeString(value); return Observation(hr,text);
}
static Json Device(IAuraSyncDevice* device, int index, unsigned category, int identity) {
    Json result = {{"runtimeIndex",index},{"category",category},{"identityIndex",identity},
                   {"stableDeviceId",nullptr},{"lights",Json::array()}};
    result["type"] = Get<ULONG>([&](ULONG* v){return device->get_Type(v);});
    result["name"] = GetString([&](BSTR* v){return device->get_Name(v);});
    result["width"] = Get<ULONG>([&](ULONG* v){return device->get_Width(v);});
    result["height"] = Get<ULONG>([&](ULONG* v){return device->get_Height(v);});
    Stage("property"); ComPtr<IAuraRgbLightCollection> lights; HRESULT hr = device->get_Lights(&lights);
    if (SUCCEEDED(hr) && !lights) hr = E_POINTER;
    result["lightCollection"] = Observation(hr, SUCCEEDED(hr));
    result["lightCount"] = {{"execution",0},{"hresult",nullptr},{"value",nullptr}};
    if (FAILED(hr)) return result;
    result["lightCount"] = Get<INT>([&](INT* v){return lights->get_Count(v);});
    if (result["lightCount"]["execution"] != 1) return result;
    auto count = result["lightCount"]["value"].get<int>();
    if (count < 0 || count > 4096) throw std::runtime_error("VendorLightCountLimit");
    for (int i=0;i<count;++i) {
        Stage("property"); ComPtr<IAuraRgbLight> light; hr = lights->get_Item(i,&light);
        if (SUCCEEDED(hr) && !light) hr = E_POINTER;
        Json item={{"index",i},{"item",Observation(hr,SUCCEEDED(hr))}};
        if (SUCCEEDED(hr)) {
            item["name"] = GetString([&](BSTR* v){return light->get_Name(v);});
            item["red"] = Get<BYTE>([&](BYTE* v){return light->get_Red(v);});
            item["green"] = Get<BYTE>([&](BYTE* v){return light->get_Green(v);});
            item["blue"] = Get<BYTE>([&](BYTE* v){return light->get_Blue(v);});
            item["color"] = Get<ULONG>([&](ULONG* v){return light->get_Color(v);});
            Stage("property"); ComPtr<IAuraMbLight> mb; HRESULT qi=light->QueryInterface(IID_PPV_ARGS(&mb));
            item["locationId"] = SUCCEEDED(qi) ? Get<ULONG>([&](ULONG* v){return mb->get_LocationId(v);}) : Observation(qi,nullptr);
        }
        result["lights"].push_back(item);
    }
    return result;
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        // No vendor code executes until the parent has assigned this process to its kill-on-close job
        // and sent the fixed discovery request. No file paths/COM identifiers/method names from IPC.
        Json request=ReadFrame();
        if (request.value("protocolVersion",0)!=1 || request.value("command","")!="discover" || !request.contains("requestId")) return 2;
        requestId=request.at("requestId").get<std::string>(); if(requestId.size()!=36) return 2;
        Stage("startup");
        HANDLE token=nullptr; TOKEN_ELEVATION elevation{}; DWORD size=0;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return 3;
        bool tokenOk=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size)!=FALSE;
        CloseHandle(token); if(!tokenOk || elevation.TokenIsElevated) return 3;
        Stage("activation"); HRESULT hr = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); bool initialized=SUCCEEDED(hr);
        ComPtr<IAuraSdk> sdk;
        if (SUCCEEDED(hr)) hr=CoCreateInstance(CLSID_AuraSdk,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IAuraSdk),reinterpret_cast<void**>(sdk.GetAddressOf()));
        if (SUCCEEDED(hr) && !sdk) hr=E_POINTER;
        Json snapshot={{"activation",Observation(hr,SUCCEEDED(hr))},{"sdk2",nullptr},{"sdk3",nullptr},{"categories",Json::array()}};
        WriteFrame({{"protocolVersion",1},{"requestId",requestId},{"sequence",sequence++},{"kind","activation"},
                    {"stage","activation"},{"workerPid",GetCurrentProcessId()},{"snapshot",{{"activation",snapshot["activation"]}}}});
        if (SUCCEEDED(hr)) {
            Stage("property"); ComPtr<IAuraSdk2> sdk2; HRESULT h2=sdk->QueryInterface(IID_PPV_ARGS(&sdk2)); snapshot["sdk2"]=Observation(h2,SUCCEEDED(h2));
            Stage("property"); ComPtr<IAuraSdk3> sdk3; HRESULT h3=sdk->QueryInterface(IID_PPV_ARGS(&sdk3)); snapshot["sdk3"]=Observation(h3,SUCCEEDED(h3));
            // Verified category constants from Phase 2.1 AuraSdk.h and host DevLastStatConfig.xml.
            // These are category IDs, not composable flags; the WDL device category is evidence-based.
            constexpr unsigned categories[]={0,0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x120000,0x2f0000};
            std::unordered_map<IUnknown*,int> identities; std::vector<ComPtr<IUnknown>> retained;
            for (auto category:categories) {
                Stage("enumeration"); ComPtr<IAuraSyncDeviceCollection> devices; hr=sdk->Enumerate(category,&devices);
                if(SUCCEEDED(hr) && !devices) hr=E_POINTER;
                Json entry={{"category",category},{"enumeration",Observation(hr,SUCCEEDED(hr))},
                            {"count",{{"execution",0},{"hresult",nullptr},{"value",nullptr}}},{"devices",Json::array()}};
                if(SUCCEEDED(hr)) {
                    entry["count"]=Get<INT>([&](INT* v){return devices->get_Count(v);});
                    if(entry["count"]["execution"]==1) {
                        int count=entry["count"]["value"].get<int>(); if(count<0 || count>256) throw std::runtime_error("VendorDeviceCountLimit");
                        for(int i=0;i<count;++i) {
                            Stage("property"); ComPtr<IAuraSyncDevice> device; hr=devices->get_Item(i,&device);
                            if(SUCCEEDED(hr) && !device) hr=E_POINTER;
                            if(FAILED(hr)) { entry["devices"].push_back({{"runtimeIndex",i},{"category",category},{"item",Observation(hr,false)}}); continue; }
                            Stage("property"); ComPtr<IUnknown> identity; hr=device->QueryInterface(IID_PPV_ARGS(&identity));
                            int id=-1;
                            if(SUCCEEDED(hr)) {
                                auto found=identities.find(identity.Get());
                                if(found!=identities.end()) id=found->second;
                                else { id=static_cast<int>(retained.size()); identities.emplace(identity.Get(),id); retained.push_back(identity); }
                            }
                            auto metadata=Device(device.Get(),i,category,id); metadata["item"]=Observation(S_OK,true); entry["devices"].push_back(metadata);
                        }
                    }
                }
                snapshot["categories"].push_back(entry);
            }
        }
        Stage("shutdown");
        // COM Release (reference lifetime) is distinct from prohibited IAuraSdk2::ReleaseControl.
        sdk.Reset(); if(initialized) CoUninitialize();
        WriteFrame({{"protocolVersion",1},{"requestId",requestId},{"sequence",sequence++},{"kind","result"},
                    {"stage","complete"},{"workerPid",GetCurrentProcessId()},{"snapshot",snapshot}});
        return 0;
    } catch(const std::exception&) { return 4; }
}
