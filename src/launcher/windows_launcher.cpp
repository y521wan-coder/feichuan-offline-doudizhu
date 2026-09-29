#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace {

using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);

bool useModernRuntime() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    const auto getVersion = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!getVersion) return false;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return getVersion(&version) == 0 && version.dwMajorVersion >= 10;
}

std::wstring executableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD copied = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        if (!copied) return {};
        if (copied < path.size()) {
            path.resize(copied);
            const auto slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
        }
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
}

std::wstring originalArguments() {
    const wchar_t* cursor = GetCommandLineW();
    if (*cursor == L'"') {
        ++cursor;
        while (*cursor && *cursor != L'"') ++cursor;
        if (*cursor) ++cursor;
    } else {
        while (*cursor && *cursor != L' ' && *cursor != L'\t') ++cursor;
    }
    return cursor;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring root = executableDirectory();
    if (root.empty()) return 1;

    const bool modern = useModernRuntime();
    const std::wstring workingDirectory = modern ? root : root + L"\\win7";
    const std::wstring executable = workingDirectory +
        (modern ? L"\\飞船斗地主现代版.exe" : L"\\飞船斗地主.exe");
    std::wstring commandLine = L"\"" + executable + L"\"" + originalArguments();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, workingDirectory.c_str(), &startup,
                        &process)) {
        const DWORD error = GetLastError();
        wchar_t message[240]{};
        wsprintfW(message, L"无法启动飞船斗地主。请检查安装文件是否完整。\n错误码：%lu", error);
        MessageBoxW(nullptr, message, L"飞船斗地主", MB_OK | MB_ICONERROR);
        return static_cast<int>(error);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
