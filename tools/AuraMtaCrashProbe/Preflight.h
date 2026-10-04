#pragma once
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Lights.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
using Marker=void(*)(const char*,...);
static std::filesystem::path Known(REFKNOWNFOLDERID id){PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value)))throw std::runtime_error("KnownFolder");std::filesystem::path p(value);CoTaskMemFree(value);return p;}
static void FileMetadata(const std::filesystem::path& path,Marker mark){
 std::ifstream file(path,std::ios::binary|std::ios::ate);auto size=file?file.tellg():std::streampos(-1);
 if(size<0||size>16*1024*1024){mark("metadata missing=1");return;}
 std::vector<BYTE> data(static_cast<size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),size);
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE digest[32]{};char hex[65]{};
 NTSTATUS status=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0);
 if(status>=0)status=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
 if(status>=0)status=BCryptHashData(hash,data.data(),static_cast<ULONG>(data.size()),0);
 if(status>=0)status=BCryptFinishHash(hash,digest,32,0);
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
 for(int i=0;i<32;++i)sprintf_s(hex+2*i,3,"%02x",digest[i]);mark("metadata bytes=%lld hashStatus=%ld sha256=%s",static_cast<long long>(size),status,hex);
 DWORD len=GetFileVersionInfoSizeW(path.c_str(),nullptr);std::vector<BYTE> bytes(len);VS_FIXEDFILEINFO* info=nullptr;UINT n=0;
 if(len&&GetFileVersionInfoW(path.c_str(),0,len,bytes.data())&&VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&n)&&info)
  mark("version major=%u minor=%u build=%u revision=%u",HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS));
}
static bool RunPreflight(const char* variant,Marker mark){
 try{
  bool lamp=!strcmp(variant,"LAMP_PREFLIGHT"),exact=!strcmp(variant,"M2_5_EXACT_PREFLIGHT");
  if(!lamp){
   SC_HANDLE scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);SC_HANDLE service=scm?OpenServiceW(scm,L"LightingService",SERVICE_QUERY_STATUS):nullptr;
   SERVICE_STATUS_PROCESS state{};DWORD n=0;BOOL ok=service&&QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&n);
   mark("lighting ok=%d state=%lu pid=%lu",ok,state.dwCurrentState,state.dwProcessId);if(service)CloseServiceHandle(service);if(scm)CloseServiceHandle(scm);
   FileMetadata(Known(FOLDERID_ProgramFiles)/L"ASUS/AuraSDK/AuraSdk_x64.dll",mark);
  }
  if(exact){
   for(auto name:{L"AmbientLightingEnabled",L"IsLampArrayEnabled",L"Brightness",L"EffectType",L"EffectMode",L"UseSystemAccentColor"}){
    DWORD value=0,n=sizeof(value);LONG error=RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Lighting",name,RRF_RT_REG_DWORD,nullptr,&value,&n);mark("wdl name=%ls error=%ld value=%lu",name,error,value);
   }
  }
  if(lamp||exact){
   auto query=winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(winrt::Windows::Devices::Lights::LampArray::GetDeviceSelector());
   auto status=query.wait_for(std::chrono::seconds(2));
   if(status==winrt::Windows::Foundation::AsyncStatus::Completed){auto items=query.get();mark("lamp count=%u",items.Size());}
   else{query.Cancel();mark("lamp timeout=1");}
  }
  if(!lamp){FileMetadata(Known(FOLDERID_ProgramData)/L"ASUS/RogAura30/GetDeviceCap.xml",mark);FileMetadata(Known(FOLDERID_ProgramFilesX86)/L"LightingService/DevLastStatConfig.xml",mark);}
  mark("preflight_complete");return true;
 }catch(const winrt::hresult_error& e){mark("preflight_failed hr=%ld",static_cast<HRESULT>(e.code()));return false;}
 catch(...){mark("preflight_failed other=1");return false;}
}
