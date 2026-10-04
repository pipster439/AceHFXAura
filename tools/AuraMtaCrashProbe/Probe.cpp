#include <initguid.h>
#include "ProbeAbi.h"
#include <sddl.h>
#include <roapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#ifdef RICH_PREFLIGHT
#include "Preflight.h"
#endif
static void Mark(const char* format,...) {
 char text[2048]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
 char output[2100]{};sprintf_s(output,"AURA_PROBE %s",text);OutputDebugStringA(output);
}
static bool Identity() {
 HANDLE token=nullptr;DWORD bytes=0,session=0;BOOL inJob=FALSE;TOKEN_ELEVATION elevated{};
 alignas(void*) BYTE buffer[4096]{};DWORD rid=0;LPSTR sid=nullptr;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
 bool ok=GetTokenInformation(token,TokenUser,buffer,sizeof(buffer),&bytes)!=FALSE;
 if(ok)ok=ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid,&sid)!=FALSE;
 if(ok)ok=GetTokenInformation(token,TokenIntegrityLevel,buffer,sizeof(buffer),&bytes)!=FALSE;
 if(ok){auto s=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buffer)->Label.Sid;rid=*GetSidSubAuthority(s,*GetSidSubAuthorityCount(s)-1);}
 ok=ok&&GetTokenInformation(token,TokenElevation,&elevated,sizeof(elevated),&bytes)&&ProcessIdToSessionId(GetCurrentProcessId(),&session)&&IsProcessInJob(GetCurrentProcess(),nullptr,&inJob);
 Mark("identity pid=%lu tid=%lu sid=%s rid=%lu elevated=%lu session=%lu inJob=%d valid=%d",GetCurrentProcessId(),GetCurrentThreadId(),sid?sid:"UNKNOWN",rid,elevated.TokenIsElevated,session,inJob,ok);
 if(sid)LocalFree(sid);CloseHandle(token);return ok&&rid==SECURITY_MANDATORY_MEDIUM_RID&&!elevated.TokenIsElevated&&session!=0;
}
int main(int argc,char** argv) {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc!=3)return 2;
 const char* variant=argv[1];char* end=nullptr;long delay=strtol(argv[2],&end,10);
 if(!end||*end||!(delay==0||delay==250||delay==1000||delay==5000))return 2;
 bool ro=!strcmp(variant,"RO_INITIALIZE"),qi=!strcmp(variant,"SDK2_QI"),exact=false;
 bool gpu=!strcmp(variant,"GPU_ONLY");bool accepted=!strcmp(variant,"COM_ONLY")||ro||qi||gpu;
#ifdef RICH_PREFLIGHT
 exact=!strcmp(variant,"M2_5_EXACT_PREFLIGHT");
 accepted=exact||!strcmp(variant,"LAMP_PREFLIGHT")||!strcmp(variant,"METADATA_DELAY");
 ro=exact||!strcmp(variant,"LAMP_PREFLIGHT");qi=exact;
#endif
 if(!accepted||!Identity())return 3;
 Mark("start variant=%s delay=%ld",variant,delay);
 // Exact preflight uses the same RoInitialize-only MTA initialization as M2.5.
 HRESULT init=exact?RoInitialize(RO_INIT_MULTITHREADED):CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 Mark("initialize hr=%ld roOnly=%d",init,exact);if(FAILED(init))return 4;
 bool extraRo=false;
 if(ro&&!exact){auto hr=RoInitialize(RO_INIT_MULTITHREADED);Mark("winrt hr=%ld",hr);if(FAILED(hr)){CoUninitialize();return 4;}extraRo=true;}
#ifdef RICH_PREFLIGHT
 if(!RunPreflight(variant,Mark)){if(exact)RoUninitialize();else {if(extraRo)RoUninitialize();CoUninitialize();}return 5;}
#endif
 IAuraSdk* sdk=nullptr;Version2Prefix* sdk2=nullptr;IAuraSyncDeviceCollection* collection=nullptr;
 HRESULT hr=CoCreateInstance(CLSID_AuraSdk,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IAuraSdk),reinterpret_cast<void**>(&sdk));
 Mark("activate hr=%ld",hr);
 if(SUCCEEDED(hr)&&sdk) {
  if(qi){hr=sdk->QueryInterface(__uuidof(Version2Prefix),reinterpret_cast<void**>(&sdk2));Mark("sdk2 hr=%ld",hr);}
  if(SUCCEEDED(hr)&&(!qi||sdk2)) {
   if(delay)Sleep(static_cast<DWORD>(delay));
   APTTYPE apt=APTTYPE_CURRENT;APTTYPEQUALIFIER qualifier=APTTYPEQUALIFIER_NONE;
   auto aptHr=CoGetApartmentType(&apt,&qualifier);Mark("apartment hr=%ld type=%d qualifier=%d",aptHr,apt,qualifier);
   const ULONG category=gpu?0x20000UL:0UL; // Two fixed, previously approved read-only choices.
   Mark("enumerate_enter category=%lu tid=%lu",category,GetCurrentThreadId());
   hr=(sdk2?static_cast<IAuraSdk*>(sdk2):sdk)->Enumerate(category,&collection);
   Mark("enumerate_return hr=%ld",hr);
   if(SUCCEEDED(hr)&&collection){INT count=0;hr=collection->get_Count(&count);Mark("count hr=%ld value=%d",hr,count);}
  }
 }
 if(collection)collection->Release();if(sdk2)sdk2->Release();if(sdk)sdk->Release();
 if(exact)RoUninitialize();else {if(extraRo)RoUninitialize();CoUninitialize();}
 Mark("completed hr=%ld",hr);return SUCCEEDED(hr)?0:6;
}
