#include "pch.h"
#include "AppMigrate.h"

// Minimal probe: is AppMigrate::Preset visible through the PCH state?
namespace
{
    AppMigrate::Preset probePreset = AppMigrate::Preset::WeChat;
    AppMigrate::Candidate probeCandidate;
    AppMigrate::DetectState probeDetect;
}

int ProbeEntrypoint()
{
    return static_cast<int>(probePreset) + static_cast<int>(probeDetect.sized.load());
}
