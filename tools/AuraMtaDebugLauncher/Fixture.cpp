#include <windows.h>
#include <intrin.h>
#include <cstring>
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 OutputDebugStringA("AURA_PROBE enumerate_enter category=0");
 if(argc>1&&!strcmp(argv[1],"FAIL_FAST"))__fastfail(FAST_FAIL_FATAL_APP_EXIT);
 if(argc>1&&!strcmp(argv[1],"TIMEOUT"))Sleep(60000);
 OutputDebugStringA("AURA_PROBE enumerate_return hr=0");OutputDebugStringA("AURA_PROBE count hr=0 value=3");OutputDebugStringA("AURA_PROBE completed hr=0");return 0;
}
