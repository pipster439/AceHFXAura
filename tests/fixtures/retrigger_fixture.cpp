#include "engine/effect.h"
#include <cstdio>
#ifndef RETRIGGER_MARKER
#define RETRIGGER_MARKER 17
#endif
namespace {
unsigned attempts=0,created=0,destroyed=0,failures=0;
uint64_t duration=1000,last[512]{};
bool first_render_gate_used=false;
void Trace(const char* action,unsigned id,uint64_t elapsed=0) {
    char path[MAX_PATH];if(!GetEnvironmentVariableA("AURA_RETRIGGER_TRACE",path,MAX_PATH))return;
    HANDLE f=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;char text[128];int n=snprintf(text,sizeof(text),"%s %d %u %llu\n",action,RETRIGGER_MARKER,id,static_cast<unsigned long long>(elapsed));
    DWORD written;WriteFile(f,text,n,&written,nullptr);CloseHandle(f);
}
void PauseFirstRender(unsigned id) {
    if(RETRIGGER_MARKER!=17 || first_render_gate_used)return;
    wchar_t ready_name[128]{},release_name[128]{};
    if(!GetEnvironmentVariableW(L"AURA_RETRIGGER_GATE_READY",ready_name,128) ||
       !GetEnvironmentVariableW(L"AURA_RETRIGGER_GATE_RELEASE",release_name,128))return;
    first_render_gate_used=true;
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,ready_name);
    HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,release_name);
    if(!ready || !release) {
        Trace("gate_error",id);if(ready)CloseHandle(ready);if(release)CloseHandle(release);return;
    }
    Trace("gate_entered",id);
    const bool signalled=SetEvent(ready)!=FALSE;
    const DWORD result=signalled?WaitForSingleObject(release,20000):WAIT_FAILED;
    Trace(result==WAIT_OBJECT_0?"gate_released":result==WAIT_TIMEOUT?"gate_timeout":"gate_error",id);
    CloseHandle(release);CloseHandle(ready);
}
struct Fixture: aura::Effect {
    unsigned id=++created;uint64_t end=duration;int marker=RETRIGGER_MARKER;
    Fixture(){last[id%512]=UINT64_MAX;Trace("create",id);}
    void Render(uint64_t elapsed,aura::FrameBuffer& frame,const aura::Keymap&) override {
        last[id%512]=elapsed;frame.Fill(marker,0,0);Trace("render",id,elapsed);PauseFirstRender(id);
    }
    ~Fixture(){++destroyed;Trace("destroy",id);}
};
}
AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion(){return 1;}
AURA_PLUGIN_EXPORT const char* AuraGetEffectName(){return "retrigger_fixture";}
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect(){++attempts;if(failures){--failures;return nullptr;}return new Fixture;}
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect){delete effect;}
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetEffectLifecycleVersion(){return 1;}
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraIsEffectFinished(const aura::Effect* effect,uint64_t elapsed){
    const auto* f=static_cast<const Fixture*>(effect);if(f->marker!=RETRIGGER_MARKER || last[f->id%512]!=elapsed)return 2;return elapsed>=f->end;
}
AURA_PLUGIN_EXPORT float AURA_PLUGIN_CALL AuraGetEffectOpacity(const aura::Effect*,uint64_t){return 1.f;}
AURA_PLUGIN_EXPORT void FixtureConfigure(unsigned fail,uint64_t end){failures=fail;duration=end;}
AURA_PLUGIN_EXPORT unsigned FixtureCount(unsigned kind){return kind==0?attempts:kind==1?created:destroyed;}
AURA_PLUGIN_EXPORT uint64_t FixtureElapsed(unsigned id){return last[id%512];}
