#include "Journal.h"
#include <cstdio>
#include <stdexcept>
namespace gatea {
std::string UtcNow() {
    SYSTEMTIME t;GetSystemTime(&t);char buffer[40];
    sprintf_s(buffer,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return buffer;
}
Journal::Journal(const std::wstring& newFile,std::string experimentId):_id(std::move(experimentId)) {
    if(_id.size()!=36) throw std::runtime_error("JournalExperimentIdInvalid");
    strcpy_s(_emergencyId,_id.c_str());
    _file=CreateFileW(newFile.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(_file==INVALID_HANDLE_VALUE) throw std::runtime_error("JournalCreateNewFailed");
}
Journal::~Journal(){if(_file!=INVALID_HANDLE_VALUE) CloseHandle(_file);}
void Journal::Append(Json entry) {
    std::lock_guard lock(_lock);
    entry["experimentId"]=_id;entry["timestamp"]=UtcNow();entry["sequence"]=_entries;entry["pid"]=GetCurrentProcessId();
    auto line=entry.dump()+"\n";
    // Reserve a little capacity for a best-effort terminal failure marker.
    if(line.size()>JournalMaxEntryBytes || _bytes+line.size()>JournalMaxBytes-4096 || _entries>=JournalMaxEntries-2) throw std::runtime_error("JournalBoundExceeded");
    DWORD written=0;
    if(!WriteFile(_file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) || written!=line.size() || !FlushFileBuffers(_file)) throw std::runtime_error("JournalAppendFailed");
    _bytes+=line.size();++_entries;
}
void Journal::Emergency(const char* event,unsigned exception) noexcept {
    // Best effort after native/terminate failure; no allocation, no waiting on a possibly poisoned mutex.
    // Emergency records may interleave with a failing writer and are not claimed crash-atomic.
    if(_emergencyEntries.fetch_add(1)>=2) return;
    char line[384];int length=sprintf_s(line,"{\"experimentId\":\"%s\",\"event\":\"%s\",\"state\":\"RecoveryRequired\",\"exceptionCode\":%u,\"pid\":%lu,\"tickCount64\":%llu,\"bestEffort\":true}\n",_emergencyId,event,exception,GetCurrentProcessId(),GetTickCount64());
    DWORD written=0;if(length>0 && _file!=INVALID_HANDLE_VALUE) {WriteFile(_file,line,static_cast<DWORD>(length),&written,nullptr);FlushFileBuffers(_file);}
}
}
