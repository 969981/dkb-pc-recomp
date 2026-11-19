/* Stand-in for a release's python\pythonw.exe in the launcher end-to-end test (tests/launcher_e2e.py). It appends its
   working folder and command line to the file named by STUB_LOG and exits 0. With E2E_FORWARD_PYTHON and
   E2E_FORWARD_SCRIPT set it is a version's window instead: it runs "<python>" "<script>" with its own arguments (those
   after its own name) in its folder, waits for it and exits with its code, and logs only then, so a line in the log
   means the window's start code has finished. */
#include <windows.h>
#include <stdio.h>

static void log_line(const wchar_t *cwd, const wchar_t *cmdline)
{
    wchar_t log[1024], line[8192];
    char u[16384];
    if (!GetEnvironmentVariableW(L"STUB_LOG", log, 1024)) return;
    swprintf(line, 8192, L"%ls\t%ls\n", cwd, cmdline);
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof u, NULL, NULL);
    HANDLE f = CreateFileW(log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    DWORD put;
    if (f != INVALID_HANDLE_VALUE && n > 0) WriteFile(f, u, (DWORD)n - 1, &put, NULL), CloseHandle(f);
}

int WINAPI wWinMain(HINSTANCE a, HINSTANCE b, LPWSTR c, int d)
{
    wchar_t cwd[1024], python[1024], script[1024], cmd[8192];
    const wchar_t *self = GetCommandLineW(), *rest = self;
    DWORD code = 0;
    (void)a, (void)b, (void)c, (void)d;
    GetCurrentDirectoryW(1024, cwd);
    if (GetEnvironmentVariableW(L"E2E_FORWARD_PYTHON", python, 1024) && GetEnvironmentVariableW(L"E2E_FORWARD_SCRIPT", script, 1024)) {
        if (*rest == L'"') {   /* past its own name, quoted or not */
            for (rest++; *rest && *rest != L'"'; rest++) {}
            if (*rest) rest++;
        } else {
            while (*rest && *rest != L' ') rest++;
        }
        swprintf(cmd, 8192, L"\"%ls\" \"%ls\"%ls", python, script, rest);
        STARTUPINFOW si = {sizeof si};
        PROCESS_INFORMATION pi;
        code = 1;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
        }
    }
    log_line(cwd, self);
    return (int)code;
}
