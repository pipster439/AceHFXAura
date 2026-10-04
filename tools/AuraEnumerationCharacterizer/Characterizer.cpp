#include <initguid.h>
#include "ReadOnlyAuraAbi.h"
#include <wrl/client.h>
#include <sddl.h>
#include <bcrypt.h>
#include <roapi.h>
#include <third_party/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
using Microsoft::WRL::ComPtr;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr unsigned Categories[]={0,0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x120000,0x2f0000};
constexpr int MaxDevices=16,MaxLights=4096;
static std::string Utf8(const std::wstring& text){
    if(text.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("InvalidVendorString");
    std::string out(static_cast<size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);return out;
}
static Json Obs(HRESULT hr,const Json& value){return {{"execution",SUCCEEDED(hr)?"Succeeded":hr==E_ACCESSDENIED?"PermissionDenied":"Failed"},{"hresult",static_cast<int>(hr)},{"value",SUCCEEDED(hr)?value:Json(nullptr)}};}
static Json NotAttempted(){return {{"execution","NotAttempted"},{"hresult",nullptr},{"value",nullptr}};}
template<class T,class F> static Json Get(F call){T value{};HRESULT hr=call(&value);return Obs(hr,value);}
static double Elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static void Progress(const Json& value){std::cout<<"@@AURA_READONLY_PROGRESS@@"<<value.dump()<<std::endl;}
static Json Identity(){
    HANDLE token=nullptr;DWORD size=0;Json result={{"pid",GetCurrentProcessId()},{"tid",GetCurrentThreadId()},{"sid",nullptr},{"integrityRid",nullptr},{"elevated",nullptr},{"session",nullptr},{"inJob",nullptr},{"execution","Failed"}};
    DWORD session=0;if(ProcessIdToSessionId(GetCurrentProcessId(),&session))result["session"]=session;
    BOOL job=FALSE;if(IsProcessInJob(GetCurrentProcess(),nullptr,&job))result["inJob"]=job!=FALSE;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return result;
    GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<BYTE> user(size);
    bool userOk=GetTokenInformation(token,TokenUser,user.data(),size,&size)!=FALSE;LPWSTR sid=nullptr;
    if(userOk&&ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)){result["sid"]=Utf8(sid);LocalFree(sid);}
    GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<BYTE> integrity(size);
    if(GetTokenInformation(token,TokenIntegrityLevel,integrity.data(),size,&size)){
        PSID value=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;
        result["integrityRid"]=*GetSidSubAuthority(value,*GetSidSubAuthorityCount(value)-1);
    }
    TOKEN_ELEVATION elevation{};if(GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size))result["elevated"]=elevation.TokenIsElevated!=0;
    CloseHandle(token);
    if(!result["sid"].is_null()&&!result["integrityRid"].is_null()&&!result["elevated"].is_null())result["execution"]="Succeeded";
    return result;
}
static std::filesystem::path SdkPath(){
    wchar_t root[MAX_PATH]{};DWORD length=GetEnvironmentVariableW(L"ProgramFiles",root,MAX_PATH);
    if(!length||length>=MAX_PATH)throw std::runtime_error("FixedSdkPathUnavailable");
    return std::filesystem::path(root)/L"ASUS/AuraSDK/AuraSdk_x64.dll";
}
static std::string Hash(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);auto size=file?file.tellg():std::streampos(-1);
    if(size<0||size>16*1024*1024)throw std::runtime_error("SdkHashSizeMissingOrInvalid");
    std::vector<BYTE> bytes(static_cast<size_t>(size));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("SdkReadIncomplete");
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE digest[32]{};
    NTSTATUS status=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status>=0)status=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(status>=0)status=BCryptHashData(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0);
    if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(status<0)throw std::runtime_error("SdkHashFailed");
    std::ostringstream out;for(BYTE value:digest)out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(value);return out.str();
}
static Json SdkEvidence(){
    auto path=SdkPath();wchar_t registered[1024]{};DWORD size=sizeof(registered),type=0;wchar_t model[64]{};
    constexpr auto key=L"SOFTWARE\\Classes\\CLSID\\{05921124-5057-483e-A037-E9497B523590}\\InprocServer32";
    HKEY user=nullptr;bool overridePresent=RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE,&user)==ERROR_SUCCESS;if(user)RegCloseKey(user);
    LONG error=RegGetValueW(HKEY_LOCAL_MACHINE,key,nullptr,RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,&type,registered,&size);
    size=sizeof(model);LONG modelError=RegGetValueW(HKEY_LOCAL_MACHINE,key,L"ThreadingModel",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,&type,model,&size);
    auto registeredPath=std::filesystem::path(registered).lexically_normal();registeredPath.make_preferred();path.make_preferred();
    bool matches=error==ERROR_SUCCESS&&modelError==ERROR_SUCCESS&&!overridePresent&&_wcsicmp(registeredPath.c_str(),path.c_str())==0&&_wcsicmp(model,L"Both")==0;
    auto digest=Hash(path);DWORD length=GetFileVersionInfoSizeW(path.c_str(),nullptr);std::vector<BYTE> bytes(length);VS_FIXEDFILEINFO* info=nullptr;UINT infoSize=0;Json version=nullptr;
    if(length&&GetFileVersionInfoW(path.c_str(),0,length,bytes.data())&&VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&infoSize)&&info)
        version=std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
    return {{"sha256",digest},{"version",version},{"registeredServer",Utf8(registered)},{"threadingModel",Utf8(model)},{"hkcuOverride",overridePresent},
        {"reviewMatches",matches&&digest=="0a94593c8c7f4e1af99abed0afe2803a6d615318ebe96d85529eb232f6a8b0af"}};
}
static std::pair<Json,ComPtr<IUnknown>> ObjectIdentity(IUnknown* object){
    ComPtr<IUnknown> identity;HRESULT hr=object->QueryInterface(IID_PPV_ARGS(&identity));Json pointer=nullptr;
    if(SUCCEEDED(hr)&&!identity)hr=E_POINTER;
    if(SUCCEEDED(hr)&&identity){std::ostringstream out;out<<static_cast<const void*>(identity.Get());pointer=out.str();}
    return {Obs(hr,pointer),identity};
}
static Json Device(ReadOnlyIAuraSyncDevice* device,int index){
    Json result={{"index",index},{"unknownIdentity",ObjectIdentity(device).first},{"LightColorSampling","NotAttempted"},{"sampledLightCount",0},{"lights",Json::array()}};
    result["type"]=Get<ULONG>([&](ULONG* v){return device->get_Type(v);});
    BSTR name=nullptr;HRESULT hr=device->get_Name(&name);std::wstring value;bool truncated=false;
    if(SUCCEEDED(hr)&&name){auto length=SysStringLen(name);truncated=length>256;value.assign(name,std::min<UINT>(length,256));}
    SysFreeString(name);result["name"]=Obs(hr,Utf8(value));result["name"]["truncated"]=truncated;
    result["width"]=Get<ULONG>([&](ULONG* v){return device->get_Width(v);});
    result["height"]=Get<ULONG>([&](ULONG* v){return device->get_Height(v);});
    ComPtr<ReadOnlyIAuraRgbLightCollection> lights;hr=device->get_Lights(&lights);if(SUCCEEDED(hr)&&!lights)hr=E_POINTER;
    result["lightCollection"]=Obs(hr,SUCCEEDED(hr));result["lightCount"]=NotAttempted();
    if(SUCCEEDED(hr)){
        result["lightCount"]=Get<INT>([&](INT* v){return lights->get_Count(v);});
        if(result["lightCount"]["execution"]=="Succeeded"){
            int count=result["lightCount"]["value"].get<int>();result["lightCountBounded"]=count>=0&&count<=MaxLights;
            if(count<0||count>MaxLights)result["reason"]="LightCountOutOfBoundsNoSampling";
        }
    }
    return result;
}
static Json Enumerate(ReadOnlyIAuraSdk* sdk,unsigned category,std::vector<ComPtr<IUnknown>>& retained){
    auto start=Clock::now();Json row={{"category",category},{"enumeration",NotAttempted()},{"count",NotAttempted()},{"collectionIdentity",NotAttempted()},
        {"devices",Json::array()},{"elapsedMs",nullptr},{"execution","Failed"},{"deviceLimit",MaxDevices},{"LightColorSampling","NotAttempted"}};
    Progress({{"stage","EnumerateEntering"},{"category",category}});
    ComPtr<ReadOnlyIAuraSyncDeviceCollection> collection;HRESULT hr=sdk->Enumerate(category,&collection);if(SUCCEEDED(hr)&&!collection)hr=E_POINTER;
    row["enumeration"]=Obs(hr,SUCCEEDED(hr));
    if(SUCCEEDED(hr)){
        auto identity=ObjectIdentity(collection.Get());row["collectionIdentity"]=identity.first;
        // Retain identities until the cache test ends. Address reuse after destruction is not sameness.
        if(identity.second)retained.push_back(identity.second);
        row["count"]=Get<INT>([&](INT* v){return collection->get_Count(v);});
        if(row["count"]["execution"]=="Succeeded"){
            int count=row["count"]["value"].get<int>();
            if(count<0||count>MaxDevices)row["reason"]="DeviceCountOutOfBoundsNoMetadata";
            else{
                row["execution"]="Succeeded";
                for(int i=0;i<count;++i){
                    Progress({{"stage","DeviceEntering"},{"category",category},{"index",i}});
                    ComPtr<ReadOnlyIAuraSyncDevice> device;hr=collection->get_Item(i,&device);if(SUCCEEDED(hr)&&!device)hr=E_POINTER;
                    Json item={{"index",i},{"item",Obs(hr,SUCCEEDED(hr))}};
                    if(SUCCEEDED(hr)){item=Device(device.Get(),i);item["item"]=Obs(hr,true);}row["devices"].push_back(item);
                    Progress({{"stage","DeviceCaptured"},{"device",item}});
                }
            }
        }
    }
    row["elapsedMs"]=Elapsed(start);Progress({{"stage","EnumerationCaptured"},{"observation",row}});return row;
}
static HRESULT Activate(ComPtr<ReadOnlyIAuraSdk>& sdk){
    HRESULT hr=CoCreateInstance(CLSID_AuraSdk,nullptr,CLSCTX_INPROC_SERVER,__uuidof(ReadOnlyIAuraSdk),reinterpret_cast<void**>(sdk.GetAddressOf()));
    return SUCCEEDED(hr)&&!sdk?E_POINTER:hr;
}
static bool Category(const char* text,unsigned& value){
    try{size_t end=0;auto parsed=std::stoul(text,&end,0);if(end!=std::string(text).size()||parsed>0xffffffff)return false;
        value=static_cast<unsigned>(parsed);return std::find(std::begin(Categories),std::end(Categories),value)!=std::end(Categories);
    }catch(...){return false;}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    std::string originalMode=argc>1?argv[1]:"",mode=originalMode,apartment=argc>2?argv[2]:"";unsigned a=0,b=0;
    bool useWinrt=mode.ends_with("-winrt");if(useWinrt)mode.resize(mode.size()-6);
    bool valid=(apartment=="STA"||apartment=="MTA")&&
      (((mode=="--single"||mode=="--single-base"||mode=="--single-version2")&&argc==4&&Category(argv[3],a))||
       ((mode=="--same-object"||mode=="--fresh-objects"||mode=="--fresh-objects-strict")&&argc==5&&Category(argv[3],a)&&Category(argv[4],b)&&a!=b));
    if(!valid){std::cerr<<"Fixed single category or two-category cache mode required\n";return 2;}
    Json result={{"schemaVersion",1},{"mode",originalMode},{"apartmentRequested",apartment},{"comInitialize",NotAttempted()},
      {"observations",Json::array()},{"activations",Json::array()},{"execution","Failed"},{"sdkInterface",mode=="--single-version2"?"ReadOnly inherited version2 prefix":"ReadOnly IAuraSdk prefix"},
      {"winrtInitialization",useWinrt},{"roInitialize",NotAttempted()},{"version2PrefixQuery",NotAttempted()},{"messagePump",false},{"LightColorSampling","NotAttempted"}};
    auto start=Clock::now();bool initialized=false,roInitialized=false;
    try{
        result["identity"]=Identity();Progress({{"stage","IdentityCaptured"},{"identity",result["identity"]}});
        auto id=result["identity"];if(id["execution"]!="Succeeded"||id["integrityRid"]!=8192||id["elevated"]!=false||id["session"]==0)throw std::runtime_error("MediumIdentityRequired");
        result["sdk"]=SdkEvidence();if(!result["sdk"]["reviewMatches"].get<bool>())throw std::runtime_error("ReviewedSdkDriftOrRegistrationOverride");
        HRESULT hr=CoInitializeEx(nullptr,apartment=="STA"?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED);result["comInitialize"]=Obs(hr,SUCCEEDED(hr));initialized=SUCCEEDED(hr);
        if(initialized&&useWinrt){HRESULT roHr=RoInitialize(apartment=="STA"?RO_INIT_SINGLETHREADED:RO_INIT_MULTITHREADED);result["roInitialize"]=Obs(roHr,SUCCEEDED(roHr));roInitialized=SUCCEEDED(roHr);if(FAILED(roHr))throw std::runtime_error("SameApartmentWinrtInitializationFailed");}
        if(initialized){
            APTTYPE actual;APTTYPEQUALIFIER qualifier;HRESULT apartmentHr=CoGetApartmentType(&actual,&qualifier);
            result["apartmentActual"]=Obs(apartmentHr,SUCCEEDED(apartmentHr)?Json{{"type",static_cast<int>(actual)},{"qualifier",static_cast<int>(qualifier)}}:Json(nullptr));
            Progress({{"stage","InitializationCaptured"},{"snapshot",result}});
            std::vector<ComPtr<IUnknown>> retained;
            ComPtr<ReadOnlyIAuraSdk> sdk;hr=Activate(sdk);result["activations"].push_back(Obs(hr,SUCCEEDED(hr)));
            Progress({{"stage","Activated"},{"result",result["activations"].back()}});
            if(SUCCEEDED(hr)&&mode=="--single-version2"){
                ComPtr<ReadOnlyVersion2Prefix> prefix;hr=sdk->QueryInterface(IID_PPV_ARGS(&prefix));result["version2PrefixQuery"]=Obs(hr,SUCCEEDED(hr));
                if(SUCCEEDED(hr)&&!prefix){hr=E_POINTER;result["version2PrefixQuery"]=Obs(hr,false);}
                if(SUCCEEDED(hr))sdk=prefix;
                Progress({{"stage","ReadOnlyVersion2PrefixQueried"},{"result",result["version2PrefixQuery"]}});
            }
            if(SUCCEEDED(hr)){
                result["observations"].push_back(Enumerate(sdk.Get(),a,retained));
                if(mode=="--fresh-objects"||mode=="--fresh-objects-strict"){
                    sdk.Reset();if(mode=="--fresh-objects-strict")retained.clear();
                    Progress({{"stage","SdkAOrdinaryReferenceReleased"},{"collectionIdentityRetained",mode!="--fresh-objects-strict"}});
                    hr=Activate(sdk);result["activations"].push_back(Obs(hr,SUCCEEDED(hr)));
                }
                if(mode!="--single"&&mode!="--single-base"&&mode!="--single-version2"&&SUCCEEDED(hr))result["observations"].push_back(Enumerate(sdk.Get(),b,retained));
                result["execution"]="Succeeded";
                for(const auto& row:result["observations"])if(row["execution"]!="Succeeded")result["execution"]="Failed";
                size_t expected=(mode=="--single"||mode=="--single-base"||mode=="--single-version2")?1:2;
                if(result["observations"].size()!=expected||FAILED(hr)){result["execution"]="Failed";result["reason"]="IncompleteFixedSequence";}
            }
            sdk.Reset();retained.clear();Progress({{"stage","OrdinaryReferencesReleased"}});
        }
    }catch(const std::exception& error){result["reason"]=error.what();}
    if(roInitialized)RoUninitialize();if(initialized)CoUninitialize();result["elapsedMs"]=Elapsed(start);
    std::cout<<"@@AURA_READONLY_RESULT@@"<<result.dump()<<std::endl;
    return result["execution"]=="Succeeded"?0:3;
}
