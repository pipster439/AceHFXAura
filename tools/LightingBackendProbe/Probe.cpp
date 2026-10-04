#include <windows.h>
#include <sddl.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Lights.h>
#include <third_party/json.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include "MediatorReadOnly.h"
using Json=nlohmann::json;
void Emit(const Json& value){std::cout<<value.dump()<<std::endl;}
std::string Utf8(const std::wstring& value){
 if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
 if(!n)throw std::runtime_error("invalid UTF-16");std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr);return result;
}
static bool Identity(){
 HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
 DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<BYTE> user(n);
 bool ok=!!GetTokenInformation(token,TokenUser,user.data(),n,&n);LPWSTR sid=nullptr;
 if(ok)ok=!!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid);
 DWORD session=0;ok=ok&&!!GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&n);
 GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&n);std::vector<BYTE> level(n);
 ok=ok&&!!GetTokenInformation(token,TokenIntegrityLevel,level.data(),n,&n);DWORD rid=0;
 if(ok){PSID s=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(level.data())->Label.Sid;rid=*GetSidSubAuthority(s,*GetSidSubAuthorityCount(s)-1);}
 Emit({{"stage","Identity"},{"pid",GetCurrentProcessId()},{"tid",GetCurrentThreadId()},{"sid",sid?Utf8(sid):""},{"session",session},{"integrityRid",rid},{"valid",ok&&rid==SECURITY_MANDATORY_MEDIUM_RID&&session!=0}});
 if(sid)LocalFree(sid);CloseHandle(token);return ok&&rid==SECURITY_MANDATORY_MEDIUM_RID&&session!=0;
}
static bool LightingRunning(){
 auto scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);auto svc=scm?OpenServiceW(scm,L"LightingService",SERVICE_QUERY_STATUS):nullptr;
 SERVICE_STATUS_PROCESS s{};DWORD n=0;bool ok=svc&&QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&s),sizeof(s),&n)&&s.dwCurrentState==SERVICE_RUNNING;
 Emit({{"stage","LightingService"},{"running",ok},{"pid",s.dwProcessId}});if(svc)CloseServiceHandle(svc);if(scm)CloseServiceHandle(scm);return ok;
}
static void Mediator(){
 if(!LightingRunning()){Emit({{"stage","Activation"},{"state","NotAttempted"},{"reason","LightingService must already be running"}});return;}
 if(!mediator::VerifyReadOnlyContract()){Emit({{"stage","Activation"},{"state","NotAttempted"},{"reason","TypeLib contract mismatch"}});return;}
 IDispatch* service=nullptr;auto start=std::chrono::steady_clock::now();Emit({{"stage","ActivationPending"}});
 HRESULT h=CoCreateInstance(mediator::ClassId,nullptr,CLSCTX_LOCAL_SERVER,mediator::InterfaceId,reinterpret_cast<void**>(&service));
 Emit({{"stage","Activation"},{"hresult",static_cast<long>(h)},{"state",SUCCEEDED(h)?"Succeeded":"Failed"},{"latencyMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()}});
 if(SUCCEEDED(h)){try{mediator::QueryMetadata(service);}catch(...){service->Release();throw;}service->Release();}
}
static void Wdl(){
 using namespace winrt::Windows;auto selector=Devices::Lights::LampArray::GetDeviceSelector();Emit({{"stage","LampSelector"},{"value",Utf8(selector.c_str())}});
 auto query=Devices::Enumeration::DeviceInformation::FindAllAsync(selector);
 if(query.wait_for(std::chrono::seconds(5))!=Foundation::AsyncStatus::Completed){query.Cancel();Emit({{"stage","Wdl"},{"state","Timeout"},{"devices",nullptr}});return;}
 auto devices=query.get();if(devices.Size()>32)throw std::runtime_error("too many LampArray interfaces");
 Json result={{"stage","Wdl"},{"state","Succeeded"},{"interfaceCount",devices.Size()},{"devices",Json::array()}};
 for(const auto& d:devices){Json item={{"interfaceId",Utf8(d.Id().c_str())},{"name",Utf8(d.Name().c_str())},{"isEnabled",d.IsEnabled()},{"lampMetadataState","NotAttempted"},{"reason","DeviceInformation only; opening a LampArray control object is outside this read-only probe"}};
  result["devices"].push_back(item);
 }Emit(result);
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2||(wcscmp(argv[1],L"--abi")&&wcscmp(argv[1],L"--mediator")&&wcscmp(argv[1],L"--wdl")))return 2;
 // Read-only worker lifetime is bounded even if the research launcher disappears.
 // No hardware ownership exists to release. Exit 124 is an explicit worker deadline.
 std::jthread deadline([](std::stop_token stop){std::mutex mutex;std::condition_variable_any cv;std::unique_lock lock(mutex);
  cv.wait_for(lock,stop,std::chrono::seconds(20),[]{return false;});if(!stop.stop_requested())std::_Exit(124);
 });
 try{if(!Identity())return 3;HRESULT h=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Emit({{"stage","ComInitialize"},{"hresult",static_cast<long>(h)}});if(FAILED(h))return 4;
  try{if(!wcscmp(argv[1],L"--abi"))Emit(mediator::LibraryMetadata());else if(!wcscmp(argv[1],L"--mediator"))Mediator();else{HRESULT r=RoInitialize(RO_INIT_MULTITHREADED);if(FAILED(r))throw winrt::hresult_error(r);try{Wdl();}catch(...){RoUninitialize();throw;}RoUninitialize();}}
  catch(...){CoUninitialize();throw;}CoUninitialize();Emit({{"stage","Completed"}});return 0;
 }catch(const winrt::hresult_error& e){Emit({{"stage","Failed"},{"hresult",static_cast<long>(e.code())}});return 5;}catch(const std::exception& e){Emit({{"stage","Failed"},{"reason",e.what()}});return 6;}
}
