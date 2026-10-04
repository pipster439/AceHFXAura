// Software-only fault fixture. Never linked into AuraWorker; contains no ASUS COM or hardware calls.
#include "WorkerWire.h"
#include <fstream>
static Json Value(Json value,int execution=1,int hr=0) {return {{"execution",execution},{"hresult",hr},{"value",value}};}
int main(int argc,char** argv) {
    std::string mode=argc>1 ? argv[1] : "zero";
    // Only the test executable supports this file; UI fault experiments use an isolated candidate directory.
    if(argc==1) {std::ifstream file("AuraWorkerFixture.mode");std::string configured;if(file>>configured) mode=configured;}
    try {
        auto request=ReadFrame(); unsigned sequence=0;
        auto frame=[&](const char* kind,const char* stage,Json snapshot=nullptr) {
            WriteFrame({{"protocolVersion",1},{"requestId",request.at("requestId")},{"sequence",sequence++},
                {"kind",kind},{"stage",stage},{"workerPid",GetCurrentProcessId()},{"snapshot",snapshot}});
        };
        frame("progress","startup"); frame("progress","activation");
        if(mode=="crash") ExitProcess(27);
        if(mode=="hang") Sleep(INFINITE);
        if(mode=="malformed") {DWORD length=0;Transfer(GetStdHandle(STD_OUTPUT_HANDLE),&length,4,true);return 0;}
        Json snapshot={{"activation",Value(true)},{"sdk2",Value(true)},{"sdk3",Value(true)},{"categories",Json::array()}};
        if(mode=="denied") {snapshot["activation"]=Value(nullptr,3,static_cast<int>(0x80070005));snapshot["sdk2"]=nullptr;snapshot["sdk3"]=nullptr;}
        frame("activation","activation",{{"activation",snapshot["activation"]}});
        if(mode=="enum-hang") {frame("progress","enumeration");Sleep(INFINITE);}
        if(mode!="denied") for(unsigned category:{0u,0x10000u,0x20000u,0x30000u,0x40000u,0x50000u,0x60000u,0x70000u,0x80000u,0x120000u,0x2f0000u}) {
            frame("progress","enumeration");
            bool found=(mode=="devices" || mode=="duplicates" || mode=="mismatch") && (category==0 || (mode=="duplicates" && category==0x10000));
            Json entry={{"category",category},{"enumeration",Value(true)},{"count",Value(found?1:0)},{"devices",Json::array()}};
            if(mode=="enum-failed" && category==0x10000) {entry["enumeration"]=Value(nullptr,2,static_cast<int>(0x80004005));entry["count"]={{"execution",0},{"hresult",nullptr},{"value",nullptr}};}
            if(found) {
                Json light={{"index",0},{"item",Value(true)},{"name",Value("Fixture Zone")},{"red",Value(11)},{"green",Value(22)},
                    {"blue",Value(33)},{"color",Value(0x0b1621)},{"locationId",Value(8)}};
                Json device={{"runtimeIndex",0},{"category",category},{"identityIndex",0},{"stableDeviceId",nullptr},{"item",Value(true)},
                    {"type",Value(0x10000)},{"name",Value("Fixture Motherboard")},{"width",Value(1)},{"height",Value(mode=="mismatch"?2:1)},
                    {"lightCount",Value(1)},{"lights",Json::array({light})}};
                entry["devices"].push_back(device);
            }
            snapshot["categories"].push_back(entry);
        }
        frame("progress","shutdown"); frame("result","complete",snapshot); return 0;
    } catch (...) { return 3; }
}
