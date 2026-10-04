#pragma once
#include "Experiment.h"
#include <filesystem>
namespace gatea {
Identity CurrentIdentity();
std::filesystem::path JournalRoot();
Review CurrentReview();
std::shared_ptr<IAuraGateAApi> CreateNativeApi(const std::filesystem::path& root,const std::string& id);
}
