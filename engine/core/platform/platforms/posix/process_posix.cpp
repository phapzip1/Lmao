#include "core/platform/process.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

extern char **environ;

namespace Lmao
{

    ProcessResult RunProcess(std::span<const std::string> command)
    {
        ProcessResult result;
        if (command.empty())
        {
            result.Output = "RunProcess: empty command";
            return result;
        }

        // O_CLOEXEC: the read end (and the write end, after dup2) must not leak into the child
        // or into processes other threads spawn meanwhile.
        int pipe_fds[2];
        if (pipe2(pipe_fds, O_CLOEXEC) != 0)
        {
            result.Output = std::string("pipe2 failed: ") + std::strerror(errno);
            return result;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO); // dup2 clears CLOEXEC
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

        std::vector<char *> arguments;
        for (const std::string &argument : command)
            arguments.push_back(const_cast<char *>(argument.c_str()));
        arguments.push_back(nullptr);

        pid_t pid = 0;
        const int spawn_error = posix_spawnp(&pid, arguments[0], &actions, nullptr, arguments.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipe_fds[1]); // the child has its copy; EOF arrives when it exits
        if (spawn_error != 0)
        {
            close(pipe_fds[0]);
            result.Output = "can't run '" + command[0] + "': " + std::strerror(spawn_error);
            return result;
        }
        result.Started = true;

        char buffer[4096];
        for (;;)
        {
            const ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
            if (count > 0)
                result.Output.append(buffer, static_cast<size_t>(count));
            else if (count == 0 || errno != EINTR)
                break;
        }
        close(pipe_fds[0]);

        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        {
        }
        if (WIFEXITED(status))
            result.ExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            result.ExitCode = 128 + WTERMSIG(status); // shell convention
        return result;
    }

} // namespace Lmao
