#pragma once

// Child processes (tools such as the shader compiler, for development features).

#include <span>
#include <string>

namespace Lmao
{

    struct ProcessResult
    {
        bool Started = false; // false: the program couldn't be run at all (Output says why)
        int ExitCode = -1;
        std::string Output; // stdout and stderr, interleaved as written
    };

    // Runs command[0] (a path, or a name looked up in PATH) with the rest as its arguments,
    // waits for it to exit and captures its output. No shell is involved, so arguments are
    // passed as-is and need no quoting. UTF-8. Blocks: call it off the main thread for slow tools.
    ProcessResult RunProcess(std::span<const std::string> command);

} // namespace Lmao
