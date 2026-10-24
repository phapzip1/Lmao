#include "core/platform/process.h"

#include "core/io/path.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace Lmao
{
    namespace
    {

        std::wstring to_wide(const std::string &utf8) { return PathFromUtf8(utf8).wstring(); }

        // Quotes one argument so CommandLineToArgvW / the MSVC runtime split it back unchanged:
        // backslashes are literal except before a quote, where they must be doubled.
        void append_quoted(std::wstring &line, const std::wstring &argument)
        {
            if (!line.empty())
                line += L' ';
            if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            {
                line += argument;
                return;
            }
            line += L'"';
            size_t backslashes = 0;
            for (const wchar_t c : argument)
            {
                if (c == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (c == L'"')
                    line.append(backslashes * 2 + 1, L'\\');
                else
                    line.append(backslashes, L'\\');
                backslashes = 0;
                line += c;
            }
            line.append(backslashes * 2, L'\\'); // before the closing quote
            line += L'"';
        }

        std::string last_error_message()
        {
            const DWORD error = GetLastError();
            char *text = nullptr;
            FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, error, 0, reinterpret_cast<char *>(&text), 0, nullptr);
            std::string message = text ? text : "error " + std::to_string(error);
            LocalFree(text);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.pop_back();
            return message;
        }

    } // namespace

    ProcessResult RunProcess(std::span<const std::string> command)
    {
        ProcessResult result;
        if (command.empty())
        {
            result.Output = "RunProcess: empty command";
            return result;
        }

        SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE};
        HANDLE read_pipe = nullptr;
        HANDLE write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &inheritable, 0))
        {
            result.Output = "CreatePipe failed: " + last_error_message();
            return result;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0); // only the write end goes to the child

        // Pass exactly the pipe to the child, not every inheritable handle in the process.
        SIZE_T attribute_size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
        std::string attribute_storage(attribute_size, '\0');
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size);
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &write_pipe, sizeof(write_pipe),
                                  nullptr, nullptr);

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullptr;
        startup.StartupInfo.hStdOutput = write_pipe;
        startup.StartupInfo.hStdError = write_pipe;
        startup.lpAttributeList = attributes;

        std::wstring command_line;
        for (const std::string &argument : command)
            append_quoted(command_line, to_wide(argument));

        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                            &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(write_pipe); // the child has its copy; ReadFile fails with BROKEN_PIPE when it exits
        if (!created)
        {
            result.Output = "can't run '" + command[0] + "': " + last_error_message();
            CloseHandle(read_pipe);
            return result;
        }
        result.Started = true;

        char buffer[4096];
        DWORD count = 0;
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &count, nullptr) && count > 0)
            result.Output.append(buffer, count);
        CloseHandle(read_pipe);

        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(process.hProcess, &exit_code);
        result.ExitCode = static_cast<int>(exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return result;
    }

} // namespace Lmao
