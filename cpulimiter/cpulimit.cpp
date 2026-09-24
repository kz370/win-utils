#define UNICODE
#define _UNICODE

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3) {
        std::wcerr << L"Usage: cpulimit.exe <percent> <command> [arguments...]\n";
        return 1;
    }

    int percent = _wtoi(argv[1]);

    if (percent < 1 || percent > 100) {
        std::wcerr << L"CPU percentage must be between 1 and 100.\n";
        return 1;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);

    if (!job) {
        std::wcerr << L"CreateJobObject failed: " << GetLastError() << L"\n";
        return 1;
    }

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuInfo{};
    cpuInfo.ControlFlags =
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE |
        JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;

    cpuInfo.CpuRate = percent * 100;

    if (!SetInformationJobObject(
        job,
        JobObjectCpuRateControlInformation,
        &cpuInfo,
        sizeof(cpuInfo)))
    {
        std::wcerr << L"SetInformationJobObject failed: "
                   << GetLastError() << L"\n";
        CloseHandle(job);
        return 1;
    }

    std::wstring commandLine;

    for (int i = 2; i < argc; ++i) {
        if (i > 2)
            commandLine += L" ";

        std::wstring arg = argv[i];

        // Quote arguments containing spaces.
        if (arg.find_first_of(L" \t\"") != std::wstring::npos) {
            commandLine += L"\"";

            for (wchar_t c : arg) {
                if (c == L'"')
                    commandLine += L'\\';

                commandLine += c;
            }

            commandLine += L"\"";
        }
        else {
            commandLine += arg;
        }
    }

    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        nullptr,
        &si,
        &pi))
    {
        std::wcerr << L"CreateProcess failed: "
                   << GetLastError() << L"\n";
        CloseHandle(job);
        return 1;
    }

    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        std::wcerr << L"AssignProcessToJobObject failed: "
                   << GetLastError() << L"\n";

        TerminateProcess(pi.hProcess, 1);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);

        return 1;
    }

    ResumeThread(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job);

    return static_cast<int>(exitCode);
}