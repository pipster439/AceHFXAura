#pragma once
#include "Experiment.h"
#include <windows.h>
namespace gatea {
inline constexpr size_t JournalMaxBytes=2*1024*1024;
inline constexpr size_t JournalMaxEntries=256;
inline constexpr size_t JournalMaxEntryBytes=32*1024;
std::string UtcNow();
class Journal final:public IJournal {
public:
    Journal(const std::wstring& newFile,std::string experimentId);
    ~Journal();
    void Append(Json entry) override;
    void Emergency(const char* event,unsigned exception) noexcept;
private:
    HANDLE _file=INVALID_HANDLE_VALUE;
    std::string _id;
    size_t _bytes=0,_entries=0;
    std::atomic_uint _emergencyEntries=0;
    char _emergencyId[40]{};
    std::mutex _lock;
};
}
