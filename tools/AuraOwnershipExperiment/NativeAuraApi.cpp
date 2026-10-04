#include <initguid.h>
#include "CanonicalAuraAbi.h"
#include "NativeAuraApi.h"
#include "ReviewedEvidence.h"
#include "Journal.h"
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Lights.h>
#include <sddl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <wtsapi32.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
using Microsoft::WRL::ComPtr;
namespace gatea {
namespace {
std::string Utf8(const std::wstring& text) {return winrt::to_string(text);}
std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&path))) throw std::runtime_error("KnownFolderUnavailable");
    std::wstring value(path);CoTaskMemFree(path);return value;
}
std::filesystem::path SdkPath(){return std::filesystem::path(Known(FOLDERID_ProgramFiles))/L"ASUS/AuraSDK/AuraSdk_x64.dll";}
Json Observation(HRESULT hr,Json value) {return {{"execution",SUCCEEDED(hr)?"Succeeded":hr==E_ACCESSDENIED?"PermissionDenied":"Failed"},{"hresult",static_cast<int>(hr)},{"value",SUCCEEDED(hr)?value:Json(nullptr)}};}
Json HashFile(const std::filesystem::path& path) {
    // Fixed caller-selected metadata sources, never a command-line file path. No DLL loading here.
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) return {{"execution","Unavailable"},{"sha256",nullptr},{"reason","FileMissingOrDenied"}};
    auto size=file.tellg();if(size<0 || size>16*1024*1024) return {{"execution","Failed"},{"sha256",nullptr},{"reason","FileHashSizeLimit"}};
    file.seekg(0);BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::vector<unsigned char> data(static_cast<size_t>(size));
    if(!file.read(reinterpret_cast<char*>(data.data()),size)) return {{"execution","Failed"},{"sha256",nullptr},{"reason","FileReadIncomplete"}};
    unsigned char digest[32];NTSTATUS status=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status>=0) status=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(status>=0) status=BCryptHashData(hash,data.data(),static_cast<ULONG>(data.size()),0);
    if(status>=0) status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash) BCryptDestroyHash(hash);if(alg) BCryptCloseAlgorithmProvider(alg,0);
    if(status<0) return {{"execution","Failed"},{"sha256",nullptr},{"reason","HashFailed"}};
    std::ostringstream text;for(auto byte:digest) text<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
    return {{"execution","Succeeded"},{"sha256",text.str()},{"bytes",static_cast<size_t>(size)}};
}
Json Version(const std::filesystem::path& path) {
    DWORD size=GetFileVersionInfoSizeW(path.c_str(),nullptr);std::vector<unsigned char> bytes(size);VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    if(!size || !GetFileVersionInfoW(path.c_str(),0,size,bytes.data()) || !VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&length) || !info)
        return {{"execution","Unavailable"},{"value",nullptr}};
    auto value=std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
    return {{"execution","Succeeded"},{"value",value}};
}
Json Service() {
    SC_HANDLE scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!scm) return {{"execution","PermissionDenied"},{"state",nullptr},{"pid",nullptr}};
    SC_HANDLE service=OpenServiceW(scm,L"LightingService",SERVICE_QUERY_STATUS);
    if(!service) {DWORD error=GetLastError();CloseServiceHandle(scm);return {{"execution",error==ERROR_ACCESS_DENIED?"PermissionDenied":"Unavailable"},{"state",nullptr},{"pid",nullptr}};}
    SERVICE_STATUS_PROCESS state{};DWORD bytes=0;bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&bytes)!=FALSE;
    CloseServiceHandle(service);CloseServiceHandle(scm);
    return ok?Json{{"execution","Succeeded"},{"state",state.dwCurrentState},{"pid",state.dwProcessId}}:Json{{"execution","Failed"},{"state",nullptr},{"pid",nullptr}};
}
Json WdlRegistry() {
    HKEY key=nullptr;LONG result=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Lighting",0,KEY_QUERY_VALUE,&key);
    if(result!=ERROR_SUCCESS) return {{"execution",result==ERROR_ACCESS_DENIED?"PermissionDenied":"Unavailable"},{"values",nullptr}};
    Json values=Json::object();
    for(auto name:{L"AmbientLightingEnabled",L"IsLampArrayEnabled",L"Brightness",L"EffectType",L"EffectMode",L"UseSystemAccentColor"}) {
        DWORD type=0,value=0,size=sizeof(value);result=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&size);
        values[Utf8(name)]=result==ERROR_SUCCESS && type==REG_DWORD?Json(value):Json(nullptr);
    }
    RegCloseKey(key);return {{"execution","Succeeded"},{"values",values},{"effectiveOwner","UnknownRegistryIsNotOwnerProof"}};
}
Json LampInterfaces() {
    try {
        auto query=winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(winrt::Windows::Devices::Lights::LampArray::GetDeviceSelector());
        if(query.wait_for(std::chrono::seconds(2))!=winrt::Windows::Foundation::AsyncStatus::Completed) {
            query.Cancel();return {{"execution","TimedOut"},{"interfaces",nullptr}};
        }
        auto devices=query.get();Json values=Json::array();
        for(auto device:devices) {if(values.size()==32) break;values.push_back({{"id",winrt::to_string(device.Id())},{"name",winrt::to_string(device.Name())},{"pnpEnabled",device.IsEnabled()}});}
        return {{"execution","Succeeded"},{"interfaces",values},{"truncated",devices.Size()>32},{"effectiveOwner","UnknownNoControlSessionOpened"}};
    } catch(const winrt::hresult_error& e) {return {{"execution","Failed"},{"hresult",static_cast<int>(e.code())},{"interfaces",nullptr}};}
}
bool RegistrationMatches() {
    constexpr auto key=L"SOFTWARE\\Classes\\CLSID\\{05921124-5057-483e-A037-E9497B523590}\\InprocServer32";
    HKEY user=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_READ,&user)==ERROR_SUCCESS) {RegCloseKey(user);return false;}
    wchar_t server[1024]{},model[64]{};DWORD size=sizeof(server),type=0;
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key,nullptr,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,&type,server,&size)!=ERROR_SUCCESS) return false;
    size=sizeof(model);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ThreadingModel",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,&type,model,&size)!=ERROR_SUCCESS) return false;
    auto registered=std::filesystem::path(server).lexically_normal();registered.make_preferred();
    auto expected=SdkPath().lexically_normal();expected.make_preferred();
    return _wcsicmp(registered.c_str(),expected.c_str())==0 && _wcsicmp(model,L"Both")==0;
}
template<class T,class F> Json Get(F operation) {T value{};HRESULT hr=operation(&value);return Observation(hr,value);}
template<class F> Json GetName(F operation) {
    BSTR value=nullptr;HRESULT hr=operation(&value);std::wstring text;bool truncated=SUCCEEDED(hr) && value && SysStringLen(value)>256;
    if(SUCCEEDED(hr) && value) text.assign(value,std::min<UINT>(SysStringLen(value),256));
    SysFreeString(value);auto observation=Observation(hr,Utf8(text));observation["truncated"]=truncated;return observation;
}
// Isolate SEH wrappers from C++ objects requiring unwind. HRESULT and exception evidence are distinct.
HRESULT SwitchCall(IAuraSdk2* sdk,DWORD* exception) {
    __try {return sdk->SwitchMode();}
    __except(EXCEPTION_EXECUTE_HANDLER) {*exception=GetExceptionCode();return E_UNEXPECTED;}
}
HRESULT ReleaseCall(IAuraSdk2* sdk,ULONG reserve,DWORD* exception) {
    __try {return sdk->ReleaseControl(reserve);}
    __except(EXCEPTION_EXECUTE_HANDLER) {*exception=GetExceptionCode();return E_UNEXPECTED;}
}
CallResult OwnershipResult(HRESULT hr,DWORD exception) {
    return {SUCCEEDED(hr)?"Succeeded":hr==E_ACCESSDENIED?"PermissionDenied":"Failed",static_cast<int>(hr),exception?Json(exception):Json(nullptr),exception?"NativeSehPartialStateUnknown":"VendorReturnedNotRestorationProof"};
}
class NativeApi final:public IAuraGateAApi {
    ComPtr<IAuraSdk2> _sdk;
    std::filesystem::path _root;
    std::string _id;
public:
    NativeApi(std::filesystem::path root,std::string id):_root(std::move(root)),_id(std::move(id)){}
    ~NativeApi() override {
        // Last reference can belong to either participating MTA thread or the caller after ordinary completion.
        HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);_sdk.Reset();if(SUCCEEDED(hr)) CoUninitialize();
    }
    void ThreadEnter() override {winrt::init_apartment(winrt::apartment_type::multi_threaded);}
    void ThreadExit() noexcept override {winrt::uninit_apartment();}
    Json CaptureMetadata() override {
        return {{"observedAt",UtcNow()},{"lightingService",Service()},{"sdkVersion",Version(SdkPath())},{"sdkHash",HashFile(SdkPath())},
            {"wdlRegistry",WdlRegistry()},{"lampArrayInterfaces",LampInterfaces()},
            {"topology",HashFile(std::filesystem::path(Known(FOLDERID_ProgramData))/L"ASUS/RogAura30/GetDeviceCap.xml")},
            {"lastConfig",HashFile(std::filesystem::path(Known(FOLDERID_ProgramFilesX86))/L"LightingService/DevLastStatConfig.xml")},
            {"TargetPrevalidated",false}};
    }
    CallResult ActivateSdk2() override {
        // Hard-coded canonical CLSID/IID only; SDK drift was checked before execute, repeated here.
        auto review=CurrentReview();if(!review.sdkMatches) return {"Unavailable",nullptr,nullptr,"ReviewedSdkMismatchNoActivation"};
        ComPtr<IAuraSdk> sdk;HRESULT hr=CoCreateInstance(CLSID_AuraSdk,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IAuraSdk),reinterpret_cast<void**>(sdk.GetAddressOf()));
        if(SUCCEEDED(hr) && sdk) hr=sdk->QueryInterface(IID_PPV_ARGS(&_sdk));
        else if(SUCCEEDED(hr)) hr=E_POINTER;
        return OwnershipResult(hr,0);
    }
    Json Enumerate(unsigned category,bool metadata) override {
        ComPtr<IAuraSyncDeviceCollection> devices;HRESULT hr=_sdk->Enumerate(category,&devices);
        Json record={{"category",category},{"execution",SUCCEEDED(hr)?"Succeeded":"Failed"},{"hresult",static_cast<int>(hr)},
            {"count",nullptr},{"countResult",{{"execution","NotAttempted"},{"value",nullptr},{"hresult",nullptr}}},{"collectionIdentity",nullptr},
            {"devices",Json::array()},{"cachedCollectionPossible",true}};
        if(FAILED(hr) || !devices) {record["execution"]="Failed";return record;}
        ComPtr<IUnknown> identity;HRESULT qi=devices->QueryInterface(IID_PPV_ARGS(&identity));
        if(SUCCEEDED(qi)) {std::ostringstream address;address<<static_cast<const void*>(identity.Get());record["collectionIdentity"]=address.str();}
        auto count=Get<INT>([&](INT* v){return devices->get_Count(v);});record["countResult"]=count;
        if(count["execution"]!="Succeeded") {record["execution"]="Failed";return record;}
        int total=count["value"].get<int>();if(total<0 || total>4096) throw std::runtime_error("CollectionCountOutOfBounds");
        record["count"]=total;record["metadataTruncated"]=metadata && total>8;
        if(metadata) for(int i=0;i<std::min(total,8);++i) {
            ComPtr<IAuraSyncDevice> device;hr=devices->get_Item(i,&device);
            if(SUCCEEDED(hr) && !device) hr=E_POINTER;
            Json item={{"index",i},{"item",Observation(hr,SUCCEEDED(hr))}};
            if(SUCCEEDED(hr) && device) {
                item["type"]=Get<ULONG>([&](ULONG* v){return device->get_Type(v);});
                item["name"]=GetName([&](BSTR* v){return device->get_Name(v);});
                item["width"]=Get<ULONG>([&](ULONG* v){return device->get_Width(v);});
                item["height"]=Get<ULONG>([&](ULONG* v){return device->get_Height(v);});
                ComPtr<IAuraRgbLightCollection> lights;hr=device->get_Lights(&lights);
                item["lightCount"]=SUCCEEDED(hr) && lights?Get<INT>([&](INT* v){return lights->get_Count(v);}):Observation(FAILED(hr)?hr:E_POINTER,nullptr);
            }
            record["devices"].push_back(item);
        }
        return record;
    }
    bool ConsumeOneShotApproval() override {
        auto path=_root/L"GATE_A_REVIEW_REQUIRED.json";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        auto value=Json{{"experimentId",_id},{"pid",GetCurrentProcessId()},{"timestamp",UtcNow()},{"reason","HumanRestorationReviewRequiredEvenIfReleaseReturnsSOK"}}.dump();
        DWORD count=0;bool ok=WriteFile(file,value.data(),static_cast<DWORD>(value.size()),&count,nullptr) && count==value.size() && FlushFileBuffers(file);
        CloseHandle(file);return ok; // Never remove this latch automatically, including errors before acquisition.
    }
    CallResult AcquireOnce() override {DWORD exception=0;HRESULT hr=SwitchCall(_sdk.Get(),&exception);return OwnershipResult(hr,exception);}
    CallResult ReleaseOnce(unsigned reserve) override {DWORD exception=0;HRESULT hr=ReleaseCall(_sdk.Get(),reserve,&exception);return OwnershipResult(hr,exception);}
};
}
Identity CurrentIdentity() {
    Identity value;value.pid=GetCurrentProcessId();DWORD session=0;HANDLE token=nullptr;
    if(!ProcessIdToSessionId(value.pid,&session) || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return value;
    value.session=session;DWORD size=0;TOKEN_ELEVATION elevation{};
    bool ok=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size)!=FALSE;value.elevated=elevation.TokenIsElevated!=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> user(size);
    ok=ok && GetTokenInformation(token,TokenUser,user.data(),size,&size)!=FALSE;
    LPWSTR sid=nullptr;if(ok && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)) {value.sid=Utf8(sid);LocalFree(sid);} else ok=false;
    GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<BYTE> integrity(size);
    if(GetTokenInformation(token,TokenIntegrityLevel,integrity.data(),size,&size)) {
        auto label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(integrity.data());auto count=*GetSidSubAuthorityCount(label->Label.Sid);
        value.integrityRid=*GetSidSubAuthority(label->Label.Sid,count-1);
    } else ok=false;
    BOOL inJob=TRUE;if(!IsProcessInJob(GetCurrentProcess(),nullptr,&inJob)) ok=false;value.inJob=inJob!=FALSE;
    PWSTR state=nullptr;DWORD bytes=0;
    if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSConnectState,&state,&bytes)) {
        value.sessionActive=bytes>=sizeof(WTS_CONNECTSTATE_CLASS) && *reinterpret_cast<WTS_CONNECTSTATE_CLASS*>(state)==WTSActive;WTSFreeMemory(state);
    } else value.sessionActive=false;
    CloseHandle(token);value.valid=ok;return value;
}
std::filesystem::path JournalRoot(){return std::filesystem::path(Known(FOLDERID_LocalAppData))/L"AceHFXAura/AuraOwnershipExperiment";}
Review CurrentReview() {
    auto hash=HashFile(SdkPath());return {ReviewedReleaseValueKnown,ReviewedReleaseValue,
        std::string(ReviewedAbiSha256)=="2672030e2439e99d2397fa8da9fa13a92cff08f42851cb13b69cd31ebec83efb" &&
        RegistrationMatches() && hash.value("execution","")=="Succeeded" && hash["sha256"]==ReviewedSdkSha256,
        ReviewedAbiSha256,ReviewedAnalysisSha256,ReviewedGateAExecutionEnabled};
}
std::shared_ptr<IAuraGateAApi> CreateNativeApi(const std::filesystem::path& root,const std::string& id){return std::make_shared<NativeApi>(root,id);}
}
