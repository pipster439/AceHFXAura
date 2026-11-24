#include "engine/effect_program.h"
#include "engine/input_sources.h"
#include <iostream>
#include <limits>
#include <cmath>
#include <cstring>
#include <atomic>
#include <cstdlib>
#include <fstream>

// Detect interpreter-owned allocations on successful warmed Render paths.
static std::atomic<unsigned> allocation_count{0};
void* operator new(std::size_t size) { allocation_count.fetch_add(1);if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using Json=nlohmann::json;
using namespace aura;
namespace {
unsigned checks=0;
void Require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
Json N(double n){return {{"op","number"},{"value",n}};}
Json B(bool b){return {{"op","boolean"},{"value",b}};}
Json T(const char* t){return {{"op","text"},{"value",t}};}
Json E(const char* op,std::initializer_list<Json> args){return {{"op",op},{"args",args}};}
Json R(double r,double g=0,double b=0){return E("rgb",{N(r),N(g),N(b)});}
Json Var(){return {{"op","var"},{"index",0}};}
Json Program(Json nodes,uint32_t entry=0,uint32_t vars=0,uint32_t slots=0,const char* mode="continuous",uint32_t fade=0){
    return {{"schema_version",1},{"entry",entry},{"variables",vars},{"slots",slots},{"publication",{{"mode",mode},{"fade_out_ms",fade}}},{"nodes",nodes}};
}
Json Fill(Json color,uint32_t next=1){return {{"kind","action"},{"op","fill"},{"color",color},{"next",next}};}
FrameBuffer Render(ProgramEffect& effect,const Keymap& keys,uint64_t time,const IGsiReader* gsi=nullptr){FrameBuffer out;effect.RenderWithContext(EffectContext(time,keys,gsi),out);return out;}
FrameBuffer RenderWithInputs(ProgramEffect& effect,const Keymap& keys,uint64_t time,const IGsiReader* gsi=nullptr,const AudioFeatureSource* audio=nullptr,const ScreenColorSource* screen=nullptr){FrameBuffer out;effect.RenderWithContext(EffectContext(time,keys,gsi,audio,screen),out);return out;}
class Gsi final:public IGsiReader {
public:double health{75};bool active{true},fail{false};mutable char text[257]{};
    double GetNumber(const char*,double)const override{if(fail)throw std::runtime_error("injected");return health;}
    bool GetBool(const char*,bool)const override{return true;}
    const char* GetString(const char* field,const char*)const override{strcpy_s(text,std::strcmp(field,"player.team")==0?"CT":"planted");return text;}
    bool IsActive()const override{return active;}
};
struct Keyboard {
    AuraLogicalKeyboardSnapshotV1 snapshot{};unsigned reads{0};
    static uint32_t AURA_PLUGIN_CALL Read(void* context,AuraLogicalKeyboardSnapshotV1* out,uint32_t bytes){auto& self=*static_cast<Keyboard*>(context);++self.reads;if(bytes!=sizeof(*out))return 0;*out=self.snapshot;return 1;}
    AuraLogicalKeyboardHostV1 Host(){return {sizeof(AuraLogicalKeyboardHostV1),1,this,&Read};}
};
class FixtureGsi final:public IGsiReader {
public:
    Json fields=Json::object();bool available{false};mutable std::string text;
    const Json* Field(const char* path)const {
        auto direct=fields.find(path);if(direct!=fields.end())return &*direct;
        const Json* value=&fields;std::string field(path);size_t offset=0;
        while(offset<field.size()) {const auto end=field.find('.',offset);const auto segment=field.substr(offset,end==std::string::npos?end:end-offset);
            if(!value->is_object()||!value->contains(segment))return nullptr;value=&value->at(segment);if(end==std::string::npos)break;offset=end+1;}
        return value;
    }
    double GetNumber(const char* field,double fallback)const override {const auto* value=Field(field);return value&&value->is_number()?value->get<double>():fallback;}
    bool GetBool(const char* field,bool fallback)const override {const auto* value=Field(field);return value&&value->is_boolean()?value->get<bool>():fallback;}
    const char* GetString(const char* field,const char* fallback)const override {const auto* value=Field(field);text=value&&value->is_string()?value->get<std::string>():fallback;return text.c_str();}
    bool IsActive()const override{return available;}
};
int RunFixture(const char* fixture_path,const char* keymap_path) {
    std::ifstream stream(fixture_path);Json fixture;stream>>fixture;
    Require(fixture.is_object()&&fixture.contains("program")&&fixture.at("frames").is_array()&&fixture.at("frames").size()<=2048,"invalid frame fixture");
    Keymap keys;Require(keys.LoadFromJson(keymap_path),"fixture keymap load");
    Keyboard keyboard;auto host=keyboard.Host();ProgramEffect effect(EffectProgram::Compile(fixture.at("program")),&host);FixtureGsi gsi;
    Json result={{"schema_version",1},{"frames",Json::array()}};
    for(const auto& frame:fixture.at("frames")) {
        const auto time=frame.at("time_ms").get<uint64_t>();gsi.fields=frame.value("gsi",Json::object());gsi.available=frame.value("gsi_available",frame.contains("gsi"));
        keyboard.snapshot={};keyboard.snapshot.struct_size=sizeof(keyboard.snapshot);keyboard.snapshot.version=1;keyboard.snapshot.observed_at_ms=time;
        if(frame.contains("keyboard")) {
            const auto& input=frame.at("keyboard");keyboard.snapshot.source_available=input.value("available",true)?1:0;
            keyboard.snapshot.observed_at_ms=input.value("observed_at_ms",time);keyboard.snapshot.source_epoch=input.value("source_epoch",uint64_t{1});keyboard.snapshot.sequence=input.value("sequence",uint64_t{0});
            const auto states=input.value("keys",Json::array());Require(states.is_array()&&states.size()<=128,"invalid keyboard fixture");
            for(const auto& state:states){auto& target=keyboard.snapshot.keys[keyboard.snapshot.count++];const auto name=state.at("name").get<std::string>();Require(name.size()<16,"fixture key name too long");std::memcpy(target.name,name.c_str(),name.size()+1);
                target.held=state.value("held",false)?1:0;target.has_release=state.value("has_release",state.contains("last_up_ms"))?1:0;target.last_down_ms=state.value("last_down_ms",uint64_t{0});target.last_up_ms=state.value("last_up_ms",uint64_t{0});target.down_sequence=state.value("down_sequence",uint64_t{0});target.up_sequence=state.value("up_sequence",uint64_t{0});}
        }
        if(frame.value("reset",false))effect.Reset();FrameBuffer output;effect.RenderWithContext(EffectContext(time,keys,&gsi),output);
        Json rendered={{"time_ms",time},{"keys",Json::array()},{"finished",effect.Finished()},{"opacity",effect.Opacity()}};
        for(const auto& name:Keymap::GetStandardLayoutKeys()){int led=-1;keys.FindLedId(name,led);Json key={{"name",name},{"led_id",led},{"coverage",led>=0&&led<128&&effect.Coverage().test(static_cast<size_t>(led))}};
            key["rgb"]=led>=0&&led<128?Json::array({output.buffer[led*3],output.buffer[led*3+1],output.buffer[led*3+2]}):Json::array({0,0,0});rendered["keys"].push_back(std::move(key));}
        const auto& status=effect.Status();rendered["status"]={{"faulted",status.faulted},{"yielded",status.yielded},{"waiting",status.waiting},{"terminal",status.terminal},{"pc",status.pc},{"work_units",status.work_units},{"instructions",status.instructions},{"keyboard_available",status.keyboard_available}};
        if(status.faulted)rendered["status"]["error"]={{"code",status.error.code},{"path",status.error.path},{"node",status.error.node},{"message",status.error.message}};
        result["frames"].push_back(std::move(rendered));
    }
    std::cout<<"AURA_IR_RESULT "<<result.dump()<<'\n';return 0;
}
void Reject(Json document,const char* message){try{EffectProgram::Compile(document);}catch(const EffectProgramException& error){Require(!error.Error().code.empty()&&!error.Error().message.empty(),message);return;}Require(false,message);}
void Expression(const Keymap& keys,Json expression,double expected){
    auto program=EffectProgram::Compile(Program(Json::array({Fill(E("rgb",{expression,N(0),N(0)})),{{"kind","end"}}})));
    ProgramEffect effect(program);const auto frame=Render(effect,keys,1000);
    Require(!effect.Status().faulted&&std::abs(static_cast<int>(frame.buffer[0])-expected)<=1,"numeric expression mismatch");
}
}
int main(int argc,char** argv){
try{
    if(argc==4&&std::string(argv[1])=="--fixture")return RunFixture(argv[2],argv[3]);
    Require(argc==2,"expected calibrated keymap fixture");Keymap keys;Require(keys.LoadFromJson(argv[1]),"keymap fixture load");
    auto plain=Program(Json::array({Fill(R(20)),{{"kind","end"}}}));
    auto shared=EffectProgram::Compile(plain);ProgramEffect effect(shared);
    Require(Render(effect,keys,0).buffer[0]==20&&effect.Coverage().all(),"fill marks coverage including black");
    ProgramEffect black(EffectProgram::Compile(Program(Json::array({Fill(R(0)),{{"kind","end"}}}))));
    Require(Render(black,keys,0).buffer[0]==0&&black.Coverage().all(),"opaque black must be distinguishable from transparent");
    Json sequence=Program(Json::array({Fill(R(200),1),{{"kind","wait"},{"duration",N(10)},{"next",2}},Fill(R(100),3),{{"kind","end"}}}),0,0,0,"one_shot",10);
    ProgramEffect once(EffectProgram::Compile(sequence));
    Require(Render(once,keys,0).buffer[0]==200&&once.Status().waiting,"wait yields initial frame");
    Require(Render(once,keys,5).buffer[0]==200&&once.Status().instructions==0,"wait retains frame without executing");
    Require(Render(once,keys,10).buffer[0]==100&&!once.Finished(),"one-shot terminal before fade");
    Render(once,keys,15);Require(std::abs(once.Opacity()-.5f)<.001f,"one-shot half opacity");
    Render(once,keys,20);Require(!once.Finished()&&once.Opacity()==0,"one-shot exposes terminal zero-opacity frame like native compiler");
    Render(once,keys,21);Require(once.Finished()&&once.Opacity()==0,"one-shot completes after zero-opacity frame");
    Require(Render(once,keys,0).buffer[0]==200&&!once.Finished(),"time rollback resets instance deterministically");
    auto variables=Program(Json::array({
        {{"kind","action"},{"op","setVar"},{"index",0},{"value",N(0)},{"next",1}},
        {{"kind","loopInit"},{"slot",0},{"mode","repeat"},{"count",N(3)},{"next",2}},
        {{"kind","loopTest"},{"slot",0},{"mode","repeat"},{"body",3},{"after",5}},
        {{"kind","action"},{"op","changeVar"},{"index",0},{"value",N(10)},{"next",4}},
        {{"kind","increment"},{"slot",0},{"next",2}},Fill(E("rgb",{Var(),N(0),N(0)}),6),{{"kind","end"}}
    }),0,1,1);
    ProgramEffect repeats(EffectProgram::Compile(variables));Require(Render(repeats,keys,0).buffer[0]==30,"repeat variables and PCs");
    auto looping=Program(Json::array({{{"kind","loopTest"},{"slot",0},{"mode","while"},{"condition",B(true)},{"body",1},{"after",2}},{{"kind","jump"},{"next",0}},{{"kind","end"}}}),0,0,1);
    ProgramEffect bounded(EffectProgram::Compile(looping));Render(bounded,keys,0);Require(bounded.Status().yielded&&bounded.Status().work_units<=4096,"infinite while yields under exact budget");
    Render(bounded,keys,1);Require(bounded.Status().yielded&&!bounded.Status().faulted,"bounded while resumes across frames");
    auto while_vars=variables;while_vars["nodes"][2]={{"kind","loopTest"},{"slot",0},{"mode","while"},{"condition",E("ge",{Var(),N(30)})},{"until",true},{"body",3},{"after",5}};
    ProgramEffect until(EffectProgram::Compile(while_vars));Require(Render(until,keys,0).buffer[0]==30,"until branch variable control");
    Keyboard keyboard;keyboard.snapshot.struct_size=sizeof(keyboard.snapshot);keyboard.snapshot.version=1;keyboard.snapshot.source_available=1;keyboard.snapshot.count=1;keyboard.snapshot.observed_at_ms=1000;strcpy_s(keyboard.snapshot.keys[0].name,"W");keyboard.snapshot.keys[0].held=1;auto host=keyboard.Host();
    auto per_key=Program(Json::array({
        {{"kind","loopInit"},{"slot",0},{"mode","keys"},{"next",1}},
        {{"kind","loopTest"},{"slot",0},{"mode","keys"},{"zone","wasd"},{"body",2},{"after",4}},
        {{"kind","action"},{"op","setKey"},{"keySlot",0},{"led",{{"op","coords"},{"field","led_id"}}},{"color",E("brightness",{R(200),E("decay",{N(2)})})},{"next",3}},
        {{"kind","increment"},{"slot",0},{"next",1}},{{"kind","end"}}
    }),0,0,1);
    auto compiled_keys=EffectProgram::Compile(per_key);Require(compiled_keys->UsesKeyboard(),"derived IR keyboard capability");
    ProgramEffect reactive(compiled_keys,&host);int w=-1;Require(keys.FindLedId("W",w),"W logical mapping");
    auto frame=Render(reactive,keys,0);Require(frame.buffer[w*3]==200&&keyboard.reads==1&&reactive.Coverage().count()==4,"one snapshot, explicit key mapping, zone coverage");
    keyboard.snapshot.keys[0].held=0;keyboard.snapshot.keys[0].has_release=1;keyboard.snapshot.keys[0].last_up_ms=500;
    frame=Render(reactive,keys,1);Require(std::abs(frame.buffer[w*3]-73)<=1&&keyboard.reads==2,"release exponential decay with monotonic time");
    keyboard.snapshot.source_available=0;frame=Render(reactive,keys,2);Require(frame.buffer[w*3]==0&&!reactive.Status().keyboard_available,"unavailable key source degrades to zero");
    auto conditional=Program(Json::array({{{"kind","if"},{"condition",E("eq",{{{"op","gsi_text"},{"path","player.team"},{"default",T("")}},T("CT")})},{"yes",1},{"no",2}},Fill(E("rgb",{{{"op","gsi_number"},{"path","player.state.health"},{"default",N(0)}},N(0),N(0)}),3),Fill(R(0),3),{{"kind","end"}}}));
    Gsi gsi;auto compiled_gsi=EffectProgram::Compile(conditional);Require(compiled_gsi->UsesGsi(),"derived GSI capability");ProgramEffect game(compiled_gsi);
    Require(Render(game,keys,0,&gsi).buffer[0]==75,"GSI text and numeric typed branch");
    gsi.active=false;Require(Render(game,keys,1,&gsi).buffer[0]==0,"inactive GSI uses declared fallback");gsi.active=true;
    auto text_pair=E("ne",{{{"op","gsi_text"},{"path","player.team"},{"default",T("")}},{{"op","gsi_text"},{"path","round.bomb"},{"default",T("")}}});
    conditional["nodes"][0]["condition"]=text_pair;ProgramEffect text_effect(EffectProgram::Compile(conditional));Require(Render(text_effect,keys,0,&gsi).buffer[0]==75,"TLS text reads copied before following read");
    AudioFeatureSource audio_source;
    auto audio_program = Program(Json::array({
        Fill(E("rgb", {
            E("mul", {{{"op", "audio_feature"}, {"feature", "rms"}, {"default", N(0)}}, N(255)}),
            E("mul", {{{"op", "audio_feature"}, {"feature", "peak"}, {"default", N(0)}}, N(255)}),
            N(0)
        })),
        {{"kind", "end"}}
    }));
    auto compiled_audio = EffectProgram::Compile(audio_program);
    Require(compiled_audio->UsesAudio(), "derived audio capability");
    ProgramEffect audio_effect(compiled_audio);
    Require(RenderWithInputs(audio_effect, keys, 0, nullptr, &audio_source).buffer[0] == 0, "disabled audio returns default 0");
    audio_source.Enable();
    audio_source.Submit({1000, 0.5f, 0.8f, 1000.0f, 1});
    auto audio_frame = RenderWithInputs(audio_effect, keys, 1010, nullptr, &audio_source);
    Require(std::abs(static_cast<int>(audio_frame.buffer[0]) - 127) <= 1, "fresh audio RMS mapped");
    Require(std::abs(static_cast<int>(audio_frame.buffer[1]) - 204) <= 1, "fresh audio peak mapped");
    audio_source.Disable();
    Require(RenderWithInputs(audio_effect, keys, 1020, nullptr, &audio_source).buffer[0] == 0, "disabled audio returns default");

    ScreenColorSource screen_source;
    auto screen_program = Program(Json::array({
        Fill({{"op", "screen_color"}, {"default", R(0)}}),
        {{"kind", "end"}}
    }));
    auto compiled_screen = EffectProgram::Compile(screen_program);
    Require(compiled_screen->UsesScreen(), "derived screen capability");
    ProgramEffect screen_effect(compiled_screen);
    Require(RenderWithInputs(screen_effect, keys, 0, nullptr, nullptr, &screen_source).buffer[0] == 0, "disabled screen returns default");
    screen_source.Enable();
    screen_source.Submit({2000, {150, 100, 50}, 1});
    auto screen_frame = RenderWithInputs(screen_effect, keys, 2010, nullptr, nullptr, &screen_source);
    Require(screen_frame.buffer[0] == 150 && screen_frame.buffer[1] == 100 && screen_frame.buffer[2] == 50, "fresh screen color mapped");
    screen_source.Disable();
    Require(RenderWithInputs(screen_effect, keys, 2020, nullptr, nullptr, &screen_source).buffer[0] == 0, "disabled screen returns default");
    auto fault_program=Program(Json::array({Fill(R(25),1),{{"kind","wait"},{"duration",N(1)},{"next",2}},Fill(R(200),3),Fill(E("rgb",{E("div",{N(1),N(0)}),N(0),N(0)}),4),{{"kind","end"}}}));fault_program["nodes"][3]["source"]="block_failure";
    ProgramEffect faulty(EffectProgram::Compile(fault_program));Require(Render(faulty,keys,0).buffer[0]==25,"fault fixture baseline");
    Require(Render(faulty,keys,1).buffer[0]==25&&faulty.Status().faulted&&faulty.Status().error.node=="block_failure","numeric fault rolls back full frame and maps source node");
    Require(Render(faulty,keys,2).buffer[0]==25,"faulted state remains stable until explicit reset");
    gsi.fail=true;Require(Render(game,keys,2,&gsi).buffer[0]==0&&game.Status().faulted,"input exception preserves prior frame");gsi.fail=false;
    const std::pair<const char*,double> unary[]={{"sqrt",2},{"abs",4},{"neg",0},{"ln",1},{"log10",0},{"exp",54},{"pow10",255},{"sin",0},{"cos",0},{"tan",1},{"asin",0},{"acos",0},{"atan",1},{"round",4},{"ceil",4},{"floor",4}};
    for(const auto& item:unary)if(std::string(item.first)!="asin"&&std::string(item.first)!="acos")Expression(keys,E(item.first,{N(4)}),item.second);
    Expression(keys,E("asin",{N(1)}),1);Expression(keys,E("acos",{N(0)}),1);
    for(const auto& item:std::vector<std::pair<const char*,double>>{{"add",9},{"sub",3},{"mul",18},{"div",2},{"mod",0},{"pow",216},{"min",3},{"max",6}})Expression(keys,E(item.first,{N(6),N(3)}),item.second);
    Expression(keys,E("time_phase",{N(2000)}),0);Expression(keys,E("distance",{N(0),N(0),N(3),N(4)}),5);
    auto wave=E("waveform",{N(.25),N(100),N(100)});wave["wave"]="sine";Expression(keys,wave,200);
    ProgramEffect hsv(EffectProgram::Compile(Program(Json::array({Fill(E("hsv",{N(120),N(100),N(100)})),{{"kind","end"}}}))));Require(Render(hsv,keys,0).buffer[1]==255,"HSV conversion");
    ProgramEffect blend(EffectProgram::Compile(Program(Json::array({Fill(E("lerp",{R(0),R(200),N(.5)})),{{"kind","end"}}}))));Require(Render(blend,keys,0).buffer[0]==100,"color lerp");
    ProgramEffect cycle(EffectProgram::Compile(Program(Json::array({Fill(E("cycle",{R(0),R(200),N(2)})),{{"kind","end"}}}))));Require(Render(cycle,keys,1000).buffer[0]==200,"color cycle");
    ProgramEffect instant(EffectProgram::Compile(Program(Json::array({Fill(R(60)),{{"kind","end"}}}),0,0,0,"one_shot",0)));
    Require(Render(instant,keys,0).buffer[0]==60&&!instant.Finished(),"zero-fade terminal frame remains observable once");
    Render(instant,keys,1);Require(instant.Finished()&&instant.Opacity()==0,"zero-fade completes on next frame");
    for(const auto& condition:std::vector<Json>{E("eq",{N(2),N(2)}),E("ne",{B(true),B(false)}),E("lt",{N(1),N(2)}),E("le",{N(2),N(2)}),E("gt",{N(3),N(2)}),E("ge",{N(2),N(2)}),E("not",{B(false)}),E("and",{B(true),B(true)}),E("or",{B(false),B(true)}),{{"op","gsi_bool"},{"path","player.state.helmet"},{"default",B(false)}}}){
        auto boolean_program=Program(Json::array({{{"kind","if"},{"condition",condition},{"yes",1},{"no",2}},Fill(R(123),3),Fill(R(0),3),{{"kind","end"}}}));
        ProgramEffect branch(EffectProgram::Compile(boolean_program));Require(Render(branch,keys,0,&gsi).buffer[0]==123,"typed boolean operator/GSI branch");
    }
    auto short_circuit=Program(Json::array({{{"kind","if"},{"condition",E("or",{B(true),E("eq",{E("div",{N(1),N(0)}),N(0)})})},{"yes",1},{"no",1}},{{"kind","end"}}}));
    ProgramEffect shorted(EffectProgram::Compile(short_circuit));Render(shorted,keys,0);Require(!shorted.Status().faulted,"boolean short circuit avoids unused failing expression");
    auto press_program=per_key;press_program["nodes"][2]={{"kind","if"},{"condition",{{"op","pressed"}}},{"keySlot",0},{"yes",5},{"no",3}};
    press_program["nodes"].push_back({{"kind","action"},{"op","setKey"},{"led",{{"op","coords"},{"field","led_id"}}},{"color",R(80)},{"keySlot",0},{"next",3}});
    keyboard.snapshot.source_available=1;keyboard.snapshot.keys[0].held=1;
    ProgramEffect pressed(EffectProgram::Compile(press_program),&host);Require(Render(pressed,keys,0).buffer[w*3]==80&&pressed.Coverage().count()==1,"pressed is actual held flag in a key context");
    for(const char* zone:{"all","alpha","arrows","numeric","navigation","modifier"}){
        auto zoned=per_key;zoned["nodes"][1]["zone"]=zone;zoned["nodes"][2]["color"]=R(40);
        ProgramEffect zone_effect(EffectProgram::Compile(zoned));Render(zone_effect,keys,0);Require(!zone_effect.Status().faulted&&zone_effect.Coverage().count()>0,"standard key zone has bounded actual LEDs");
        if(std::string(zone)=="alpha")Require(zone_effect.Coverage().count()==26,"alpha zone includes exactly A through Z");
    }
    auto ripple=Program(Json::array({{{"kind","action"},{"op","ripple"},{"key","W"},{"speed",N(2.5)},{"color",R(180)},{"next",1}},{{"kind","end"}}}));
    ProgramEffect ripples(EffectProgram::Compile(ripple));Render(ripples,keys,0);Require(!ripples.Status().faulted&&ripples.Coverage().count()==68,"ripple maps calibrated actual LED ids with bounded coverage");
    auto budget_program=Program(Json::array({Fill(R(10),1),Fill(R(200),1)}));ProgramEffect budget(EffectProgram::Compile(budget_program));Render(budget,keys,0);
    Require(budget.Status().yielded&&budget.Status().work_units<=4096&&budget.Status().pc==1,"action yields before exceeding remaining frame budget");
    auto isolated_a=std::make_unique<ProgramEffect>(shared),isolated_b=std::make_unique<ProgramEffect>(shared);Render(*isolated_a,keys,2);Require(!isolated_b->Status().instructions,"shared immutable program has independent instance state");
    auto bad=plain;bad["nodes"][0]["color"]=N(3);Reject(bad,"reject expression type mismatch");bad=plain;bad["nodes"][0]["next"]=9;Reject(bad,"reject missing PC");bad=plain;bad["extra"]=true;Reject(bad,"reject arbitrary root fields");bad=plain;bad["nodes"][0]["op"]="shell";Reject(bad,"reject arbitrary actions");bad=plain;bad["variables"]=65;Reject(bad,"reject variable excess");bad=plain;bad["slots"]=65;Reject(bad,"reject slot excess");bad=plain;bad["nodes"][0]["color"]={{"op","eval"},{"args",Json::array()}};Reject(bad,"reject arbitrary expression");
    bad=plain;bad["nodes"][0]["color"]=E("rgb",{{{"op","gsi_number"},{"path","event.kill"},{"default",N(0)}},N(0),N(0)});Reject(bad,"reject retired event pulses");
    bad=plain;bad["nodes"][0]["color"]=E("rgb",{{{{"op","audio_feature"},{"feature","invalid_feat"},{"default",N(0)}},N(0),N(0)}});Reject(bad,"reject invalid audio feature");
    bad=plain;bad["nodes"][0]["color"]=E("rgb",{{{{"op","audio_feature"},{"feature","rms"},{"default",B(true)}},N(0),N(0)}});Reject(bad,"reject non-number audio default");
    bad=plain;bad["nodes"][0]["color"]={{"op","screen_color"},{"default",N(0)}};Reject(bad,"reject non-color screen default");
    auto nested=N(1);for(int i=0;i<34;++i)nested=E("abs",{nested});bad=plain;bad["nodes"][0]["color"]=E("rgb",{nested,N(0),N(0)});Reject(bad,"reject excessive expression depth");
    bad=plain;bad["nodes"]=Json::array();for(uint32_t i=0;i<2049;++i)bad["nodes"].push_back({{"kind","end"}});Reject(bad,"reject instruction capacity overflow");
    auto expression_tree=N(1);for(int i=0;i<12;++i)expression_tree=E("add",{expression_tree,expression_tree});bad=plain;bad["nodes"][0]["color"]=E("rgb",{expression_tree,N(0),N(0)});Reject(bad,"reject expression cost/count overflow before render starvation");
    auto invalid_led=Program(Json::array({{{"kind","action"},{"op","setKey"},{"led",N(128)},{"color",R(30)},{"next",1}},{{"kind","end"}}}));ProgramEffect invalid_pixel(EffectProgram::Compile(invalid_led));Render(invalid_pixel,keys,0);Require(invalid_pixel.Status().faulted&&invalid_pixel.Coverage().none(),"invalid LED fails without partial output");
    try{EffectProgram::Parse("{\"schema_version\":1,\"schema_version\":1}");Require(false,"duplicate JSON accepted");}catch(const EffectProgramException&){Require(true,"duplicate JSON rejected");}
    try{EffectProgram::Parse(std::string(EffectProgram::MaxJsonBytes+1,' '));Require(false,"oversized text accepted");}catch(const EffectProgramException&){Require(true,"oversized text rejected");}
    Require(Render(effect,keys,2).buffer[0]==20,"failed candidate compilation never mutates existing instance");
    Render(repeats,keys,1);const auto allocations=allocation_count.load();FrameBuffer output;for(uint64_t time=2;time<10002;++time)repeats.Render(time,output,keys);
    Require(allocation_count.load()==allocations&&!repeats.Status().faulted&&output.buffer[0]==30,"10000 warmed renders have no interpreter allocations or state drift");
    std::cout<<"PASS: "<<checks<<" restricted IR checks; deterministic execution, rollback, budgets, coverage, no hot allocations\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
