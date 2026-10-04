#include <windows.h>
#include <dbghelp.h>
#include <third_party/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
static std::string Hex(DWORD64 value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
static std::string Utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(static_cast<size_t>(n),0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
static double Ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static std::string Time(){SYSTEMTIME t{};GetSystemTime(&t);char b[64]{};sprintf_s(b,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return b;}
static std::string Path(HANDLE f){wchar_t b[32768]{};DWORD n=f?GetFinalPathNameByHandleW(f,b,32768,FILE_NAME_NORMALIZED):0;return n&&n<32768?Utf8(std::wstring(b,n)):"Unknown";}
static DWORD ImageSize(HANDLE p,DWORD64 base){IMAGE_DOS_HEADER dos{};SIZE_T n=0;IMAGE_NT_HEADERS64 nt{};if(!ReadProcessMemory(p,reinterpret_cast<void*>(base),&dos,sizeof(dos),&n)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1024*1024)return 0;if(!ReadProcessMemory(p,reinterpret_cast<void*>(base+static_cast<DWORD>(dos.e_lfanew)),&nt,sizeof(nt),&n))return 0;return nt.OptionalHeader.SizeOfImage;}
static void AddModule(HANDLE process,HANDLE file,DWORD64 base,Json& modules,double elapsed){
 auto path=Path(file);auto size=ImageSize(process,base);std::string image=path.starts_with("\\\\?\\")?path.substr(4):path;
 SetLastError(0);auto loaded=SymLoadModuleEx(process,file,image=="Unknown"?nullptr:image.c_str(),nullptr,base,size,nullptr,0);DWORD error=GetLastError();
 modules.push_back({{"base",base},{"size",size},{"path",path},{"loadedMs",elapsed},{"symbolBase",loaded},{"symbolLoadError",error}});
}
static Json Attribution(DWORD64 address,const Json& modules){for(auto it=modules.rbegin();it!=modules.rend();++it){auto base=(*it)["base"].get<DWORD64>();auto size=(*it)["size"].get<DWORD>();if(address>=base&&address-base<size)return {{"path",(*it)["path"]},{"base",base},{"rva",address-base},{"rvaHex",Hex(address-base)}};}return nullptr;}
static Json Stack(HANDLE p,HANDLE t,CONTEXT context,const Json& modules){
 Json result=Json::array();STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};DWORD64 previous=0;
 for(int i=0;i<64;++i){auto pc=frame.AddrPC.Offset;if(!pc||pc==previous)break;previous=pc;Json row={{"pc",pc},{"pcHex",Hex(pc)},{"module",Attribution(pc,modules)}};
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 offset=0;
  if(SymFromAddr(p,pc,&offset,symbol)){row["symbol"]=symbol->Name;row["symbolOffset"]=offset;}else row["symbol"]=nullptr;
  result.push_back(row);if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,p,t,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
  if(frame.AddrPC.Offset==previous&&!StackWalk64(IMAGE_FILE_MACHINE_AMD64,p,t,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
 }return result;
}
static Json Context(HANDLE process,HANDLE thread,const Json& modules){CONTEXT c{};c.ContextFlags=CONTEXT_ALL;BOOL ok=GetThreadContext(thread,&c);Json result={{"contextSucceeded",ok!=FALSE},{"contextError",ok?0:GetLastError()}};if(ok){result["registers"]={{"rip",Hex(c.Rip)},{"rsp",Hex(c.Rsp)},{"rbp",Hex(c.Rbp)},{"rax",Hex(c.Rax)},{"rcx",Hex(c.Rcx)},{"rdx",Hex(c.Rdx)},{"r8",Hex(c.R8)},{"r9",Hex(c.R9)}};result["stack"]=Stack(process,thread,c,modules);}return result;}
int wmain(int argc,wchar_t** argv){
 if(argc!=4){std::cerr<<"launcher <fixed variant or fixture name> <delay 0/250/1000/5000> <fresh trial id>\nNo arbitrary target, category, command or output path.\n";return 2;}
 const std::filesystem::path root(TRIAGE_ROOT),audit=root/L"audit_artifacts/phase3-m2.7";
 std::wstring variant=argv[1],delay=argv[2],id=argv[3];
 bool fixture=variant==L"COMPLETE"||variant==L"FAIL_FAST"||variant==L"TIMEOUT";
 bool rich=variant==L"M2_5_EXACT_PREFLIGHT"||variant==L"LAMP_PREFLIGHT"||variant==L"METADATA_DELAY";
 bool minimal=variant==L"COM_ONLY"||variant==L"RO_INITIALIZE"||variant==L"SDK2_QI"||variant==L"GPU_ONLY";
 if(id.empty()||id.size()>80||id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::wstring::npos)return 2;
 auto exe=fixture?audit/L"debug-build/Release/DebugFixture.exe":audit/L"probe-build/Release"/(rich?L"AuraMtaPreflightProbe.exe":L"AuraMtaCrashProbe.exe");
 auto output=audit/L"raw"/(id+L".json");
 DWORD budget=fixture?1:35;if(!(fixture||rich||minimal)||!(delay==L"0"||delay==L"250"||delay==L"1000"||delay==L"5000")||std::filesystem::exists(output))return 2;
 std::filesystem::create_directories(output.parent_path());std::filesystem::create_directories(output.parent_path().parent_path()/L"dumps");
 auto start=Clock::now(),entered=start;bool inEnumerate=false,timedOut=false,started=false;unsigned nextSample=5;
 Json report={{"schemaVersion",1},{"startedAt",Time()},{"variant",Utf8(variant)},{"workingDirectory",Utf8(root.wstring())},{"delayMs",wcstoul(delay.c_str(),nullptr,10)},{"observationBudgetSeconds",budget},{"fixture",fixture},{"count",nullptr},{"enumerateHresult",nullptr},{"events",Json::array()},{"modules",Json::array()},{"preEnumerateModules",Json::array()},{"exceptions",Json::array()},{"samples",Json::array()},{"dumps",Json::array()},{"outcome","NotStarted"}};
 std::wstring command=L"\""+exe.wstring()+L"\" "+variant+L" "+delay;STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,nullptr,root.c_str(),&si,&pi)){report["createError"]=GetLastError();report["outcome"]="LaunchFailed";std::ofstream(output)<<report.dump(2);return 3;}
 CloseHandle(pi.hThread);std::map<DWORD,HANDLE> threads;report["pid"]=pi.dwProcessId;
 SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
 report["symbolsInitialized"]=SymInitialize(pi.hProcess,exe.parent_path().string().c_str(),FALSE)!=FALSE;
 bool running=true,fatalSeen=false;auto fatalAt=start;unsigned dumpCount=0;
 while(running){
  DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,100)){
   if(GetLastError()!=ERROR_SEM_TIMEOUT){report["waitError"]=GetLastError();timedOut=true;TerminateProcess(pi.hProcess,0x102);}
   if(!timedOut&&!fatalSeen&&((inEnumerate&&Ms(entered)>budget*1000.0)||Ms(start)>65000)){timedOut=true;report["timeoutAt"]=Time();TerminateProcess(pi.hProcess,0x102);}
   if(fatalSeen&&Ms(fatalAt)>10000){report["postFatalExitForced"]=true;TerminateProcess(pi.hProcess,0x103);}
   if(!timedOut&&!fatalSeen&&inEnumerate&&nextSample<=20&&Ms(entered)>=nextSample*1000.0){
    Json sample={{"enumerateElapsedMs",Ms(entered)},{"at",Time()},{"threads",Json::array()}};
    // Sampling suspends one thread briefly; balance every successful suspension.
    for(auto [tid,t]:threads){if(sample["threads"].size()>=64)break;DWORD prior=SuspendThread(t);Json row={{"tid",tid},{"suspended",prior!=static_cast<DWORD>(-1)}};if(prior!=static_cast<DWORD>(-1)){row["diagnostics"]=Context(pi.hProcess,t,report["modules"]);ResumeThread(t);}sample["threads"].push_back(row);}
    report["samples"].push_back(sample);nextSample+=5;
   }
   continue;
  }
  DWORD continuation=DBG_CONTINUE;
  switch(event.dwDebugEventCode){
   case CREATE_PROCESS_DEBUG_EVENT:{threads[event.dwThreadId]=event.u.CreateProcessInfo.hThread;auto base=reinterpret_cast<DWORD64>(event.u.CreateProcessInfo.lpBaseOfImage);AddModule(pi.hProcess,event.u.CreateProcessInfo.hFile,base,report["modules"],Ms(start));if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);CloseHandle(event.u.CreateProcessInfo.hProcess);break;}
   case CREATE_THREAD_DEBUG_EVENT:threads[event.dwThreadId]=event.u.CreateThread.hThread;break;
   case EXIT_THREAD_DEBUG_EVENT:if(threads.contains(event.dwThreadId)){CloseHandle(threads[event.dwThreadId]);threads.erase(event.dwThreadId);}break;
   case LOAD_DLL_DEBUG_EVENT:{auto base=reinterpret_cast<DWORD64>(event.u.LoadDll.lpBaseOfDll);AddModule(pi.hProcess,event.u.LoadDll.hFile,base,report["modules"],Ms(start));if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);break;}
   case UNLOAD_DLL_DEBUG_EVENT:report["events"].push_back({{"kind","ModuleUnloaded"},{"base",reinterpret_cast<DWORD64>(event.u.UnloadDll.lpBaseOfDll)},{"elapsedMs",Ms(start)}});SymUnloadModule64(pi.hProcess,reinterpret_cast<DWORD64>(event.u.UnloadDll.lpBaseOfDll));break;
   case OUTPUT_DEBUG_STRING_EVENT:{auto& s=event.u.DebugString;SIZE_T size=static_cast<SIZE_T>(s.nDebugStringLength)*(s.fUnicode?2:1);if(size>16384)break;std::vector<char> b(size+2,0);SIZE_T read=0;ReadProcessMemory(pi.hProcess,s.lpDebugStringData,b.data(),size,&read);std::string value=s.fUnicode?Utf8(reinterpret_cast<wchar_t*>(b.data())):std::string(b.data());
    if(value.starts_with("AURA_PROBE ")){report["events"].push_back({{"kind","ProbeMarker"},{"text",value},{"tid",event.dwThreadId},{"at",Time()},{"elapsedMs",Ms(start)}});
     if(value.find("enumerate_enter ")!=std::string::npos){entered=Clock::now();inEnumerate=true;report["preEnumerateModules"]=report["modules"];report["enumerateTid"]=event.dwThreadId;}
     if(value.find("enumerate_return ")!=std::string::npos){inEnumerate=false;long hr=0;sscanf_s(value.c_str(),"AURA_PROBE enumerate_return hr=%ld",&hr);report["enumerateHresult"]=hr;report["enumerateElapsedMs"]=Ms(entered);}
     if(value.find("count ")!=std::string::npos){long hr=-1;int count=0;if(sscanf_s(value.c_str(),"AURA_PROBE count hr=%ld value=%d",&hr,&count)==2){report["countHresult"]=hr;if(hr>=0)report["count"]=count;}}
    }break;}
   case EXCEPTION_DEBUG_EVENT:{auto& e=event.u.Exception;auto& r=e.ExceptionRecord;if(r.ExceptionCode==EXCEPTION_BREAKPOINT&&!started){started=true;break;}
    continuation=DBG_EXCEPTION_NOT_HANDLED;
    Json row={{"code",r.ExceptionCode},{"codeHex",Hex(r.ExceptionCode)},{"flags",r.ExceptionFlags},{"address",reinterpret_cast<DWORD64>(r.ExceptionAddress)},{"module",Attribution(reinterpret_cast<DWORD64>(r.ExceptionAddress),report["modules"])},{"numberParameters",r.NumberParameters},{"parameters",Json::array()},{"firstChance",e.dwFirstChance!=0},{"pid",event.dwProcessId},{"tid",event.dwThreadId},{"at",Time()},{"elapsedMs",Ms(start)}};
    for(DWORD i=0;i<r.NumberParameters&&i<EXCEPTION_MAXIMUM_PARAMETERS;++i)row["parameters"].push_back(r.ExceptionInformation[i]);
    row["fastFailReason"]=(r.ExceptionCode==0xc0000409&&r.NumberParameters)?Json(r.ExceptionInformation[0]):Json(nullptr);
    if(threads.contains(event.dwThreadId))row["diagnostics"]=Context(pi.hProcess,threads[event.dwThreadId],report["modules"]);
    if(r.ExceptionCode==0xc0000409||!e.dwFirstChance){fatalSeen=true;fatalAt=Clock::now();}
    if((r.ExceptionCode==0xc0000409||!e.dwFirstChance)&&dumpCount<3&&threads.contains(event.dwThreadId)){
     CONTEXT context{};context.ContextFlags=CONTEXT_ALL;BOOL got=GetThreadContext(threads[event.dwThreadId],&context);EXCEPTION_RECORD record=r;record.ExceptionRecord=nullptr;EXCEPTION_POINTERS pointers{&record,&context};MINIDUMP_EXCEPTION_INFORMATION info{event.dwThreadId,&pointers,FALSE};
     auto file=output.parent_path().parent_path()/L"dumps"/(output.stem().wstring()+L"-"+std::to_wstring(dumpCount++)+L".dmp");HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
     auto type=static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|MiniDumpWithIndirectlyReferencedMemory|MiniDumpWithDataSegs);
     BOOL ok=handle!=INVALID_HANDLE_VALUE&&got&&MiniDumpWriteDump(pi.hProcess,pi.dwProcessId,handle,type,&info,nullptr,nullptr);DWORD error=ok?0:GetLastError();if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
     report["dumps"].push_back({{"path",Utf8(file.wstring())},{"succeeded",ok!=FALSE},{"error",error},{"type",static_cast<unsigned>(type)},{"contextSucceeded",got!=FALSE},{"clientPointers",false}});
    }
    if(report["exceptions"].size()<256)report["exceptions"].push_back(row);else report["exceptionRecordsTruncated"]=true;break;}
   case EXIT_PROCESS_DEBUG_EVENT:report["exitCode"]=event.u.ExitProcess.dwExitCode;running=false;break;
   default:break;
  }
  if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,continuation)){report["continueError"]=GetLastError();TerminateProcess(pi.hProcess,0x103);}
 }
 SymCleanup(pi.hProcess);for(auto [tid,t]:threads){(void)tid;CloseHandle(t);}CloseHandle(pi.hProcess);
 bool failfast=false;for(auto& e:report["exceptions"])if(e["code"]==0xc0000409)failfast=true;
 report["outcome"]=failfast?"FailFast":fatalSeen?"Failed":timedOut?"Timeout":report.value("exitCode",1u)==0&&!report["enumerateHresult"].is_null()?"Completed":"Failed";
 report["endedAt"]=Time();report["elapsedMs"]=Ms(start);std::ofstream(output)<<report.dump(2);std::cout<<report["outcome"]<<" pid="<<pi.dwProcessId<<" elapsedMs="<<report["elapsedMs"]<<"\n";return 0;
}
