/* SPDX-License-Identifier: GPL-3.0-or-later

   The DKB launcher's first install (fetch.h): dkb-release.txt of the latest release, then the tools zip it names,
   written to downloads\<sha256>.zip.part and kept only when its size and SHA-256 match; Windows' tar.exe unpacks it into
   versions\<tag>.part, which becomes versions\<tag> once it holds the Python and the VERSION the manifest names, and
   versions\current.txt then names it. Nothing downloaded runs before that check. A progress window shows each step;
   closing it cancels the install at the next block of the download, or once the unpack is done (nothing is then kept
   or started). A named mutex per base folder makes a second launcher wait for the first one's install instead of
   unpacking into the same folders. launcher.c (MIT) calls this file; only the Bananza build compiles it.

   Adapted from Longshot, https://git.magicbean.dev/MagicBeanDev/Longshot @ 0caf06184b391a5259d3741bb006ef5826eb6417,
   builder/setup/setup.c (http_get, Sink, FileSink and file_sink, hex, run_wait, rmtree, read_small, wndproc and the
   progress window of wWinMain), Copyright (C) 2026 the Longshot contributors, and from Project ReLink,
   https://github.com/pixl27/ProjectReLink @ 6570d6e73576b1a9506d7168f640754b61386cb3, launcher/setup/setup.c (the same
   functions, and the versions\current.txt, previous.txt, <tag>.part and downloads\<sha256>.zip.part layout of work and
   prune), itself a modified version of Longshot; Longshot is a fork of Hookshot (https://github.com/NotRexed/Hookshot),
   Copyright (C) 2026 NotRexed and the Hookshot contributors. All three are released under the GPL-3.0-or-later.
   Changed here: one release manifest in this project's own format (manifest.c), the catalog's strings, the launcher's
   folder beside the exe, the cancel, the mutex and the error reporting. THIRD_PARTY_NOTICES.md has the row.

   This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
   version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License (LICENSE in this
   repository) for more details. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "launcher.h"
#include "manifest.h"
#include "fetch.h"

#define MANIFEST_LIMIT (64u << 10)
#define WM_STATUS (WM_APP + 1)
#define WM_DONE (WM_APP + 2)

static HWND window, status_text, status_bar;
static wchar_t status_line[TEXT_CHARS];
static volatile LONG status_percent = -1, cancelled;

/* ---- files ------------------------------------------------------------------------------------------------------ */

/* A folder and what it holds. A link or junction, dir itself included, is removed as a link: what it points to is
   never entered */
static void remove_tree(const wchar_t *dir)
{
    wchar_t pattern[PATH_CHARS], path[PATH_CHARS];
    WIN32_FIND_DATAW fd;
    DWORD a = GetFileAttributesW(dir);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (a & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(dir);
        else DeleteFileW(dir);
        return;
    }
    if (swprintf(pattern, PATH_CHARS, L"%ls\\*", dir) < 0) return;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (swprintf(path, PATH_CHARS, L"%ls\\%ls", dir, fd.cFileName) < 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
                else DeleteFileW(path);
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_tree(path);
            } else {
                SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

void fetch_read_line(const wchar_t *path, char *out, int cap)
{
    out[0] = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    if (!ReadFile(f, out, (DWORD)cap - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    out[got] = 0;
    for (char *p = out; *p; p++)
        if (*p == '\r' || *p == '\n') { *p = 0; break; }
}

/* text into path through path.tmp, so a stop half way never leaves a cut-off file */
static int write_file(const wchar_t *path, const char *text)
{
    wchar_t tmp[PATH_CHARS];
    DWORD put = 0;
    if (swprintf(tmp, PATH_CHARS, L"%ls.tmp", path) < 0) return 0;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int ok = WriteFile(f, text, (DWORD)strlen(text), &put, NULL) && put == strlen(text) && FlushFileBuffers(f);
    CloseHandle(f);
    return ok && MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

/* Whether base\versions\current.txt names a version folder that holds its Python */
static int current_installed(const wchar_t *base)
{
    wchar_t path[PATH_CHARS], wtag[64];
    char tag[64];
    swprintf(path, PATH_CHARS, L"%ls\\versions\\current.txt", base);
    fetch_read_line(path, tag, sizeof tag);
    if (!valid_tag(tag)) return 0;
    widen(tag, wtag, 64);
    swprintf(path, PATH_CHARS, L"%ls\\versions\\%ls\\python\\pythonw.exe", base, wtag);
    return is_file(path);
}

/* ---- HTTPS ------------------------------------------------------------------------------------------------------ */

typedef int (*Sink)(void *user, const void *data, DWORD n);
static DWORD http_status;   /* the last answer's status; 0 when no server answered */

/* GET url into sink, at most limit bytes, following redirects (a release's download link goes to GitHub's file host),
   never from https down to http. A test build also takes http to 127.0.0.1 (the tests' local server). 1 when the
   whole answer arrived with status 200 */
static int http_get(const wchar_t *url, Sink sink, void *user, unsigned long long limit, unsigned long long expect)
{
    URL_COMPONENTS uc = {sizeof uc};
    wchar_t host[256], path[2048];
    uc.lpszHostName = host, uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path, uc.dwUrlPathLength = 2048;
    http_status = 0;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;
#ifdef DKB_LAUNCHER_TEST
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && wcscmp(host, L"127.0.0.1")) return 0;
#else
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return 0;
#endif
    HINTERNET s = WinHttpOpen(L"dkb-recomp-launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"dkb-recomp-launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) return 0;
    WinHttpSetTimeouts(s, 30000, 30000, 30000, 60000);   /* a stalled download fails after a minute without data */
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : NULL;
    int ok = 0;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                            &size, WINHTTP_NO_HEADER_INDEX);
        http_status = code;
        if (code == 200) {
            static char buf[1 << 16];
            unsigned long long got = 0;
            ok = 1;
            for (;;) {
                DWORD n = 0;
                if (cancelled || !WinHttpReadData(r, buf, sizeof buf, &n)) { ok = 0; break; }
                if (!n) break;
                got += n;
                if (got > limit || !sink(user, buf, n)) { ok = 0; break; }
                if (expect) {
                    InterlockedExchange(&status_percent, (LONG)(got * 100 / expect));
                    PostMessageW(window, WM_STATUS, 0, 0);
                }
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

typedef struct { char data[MANIFEST_LIMIT + 1]; DWORD n; } Text;

static int text_sink(void *user, const void *data, DWORD n)
{
    Text *t = (Text *)user;
    if (t->n + n > MANIFEST_LIMIT) return 0;
    memcpy(t->data + t->n, data, n);
    t->n += n;
    t->data[t->n] = 0;
    return 1;
}

typedef struct { HANDLE file; BCRYPT_HASH_HANDLE hash; int write_failed; } FileSink;

/* write_failed tells a full disk (or a folder that refuses writes) apart from a connection that broke off */
static int file_sink(void *user, const void *data, DWORD n)
{
    FileSink *f = (FileSink *)user;
    DWORD put = 0;
    if (!WriteFile(f->file, data, n, &put, NULL) || put != n) {
        f->write_failed = 1;
        return 0;
    }
    return BCryptHashData(f->hash, (PUCHAR)data, n, 0) == 0;
}

/* ---- the first install ------------------------------------------------------------------------------------------ */

static void status(const wchar_t *text, LONG percent)
{
    wcsncpy(status_line, text, TEXT_CHARS - 1);
    InterlockedExchange(&status_percent, percent);
    PostMessageW(window, WM_STATUS, 0, 0);
}

typedef struct {
    wchar_t base[PATH_CHARS];
    wchar_t error[TEXT_CHARS];   /* why it stopped, for the message box; empty after a cancel */
    int ok;
} Job;

static void job_fail(Job *job, const wchar_t *tmpl, const wchar_t *name, const wchar_t *value)
{
    const wchar_t *pairs[] = {name, value, NULL};
    if (!cancelled) fill(job->error, TEXT_CHARS, tmpl, pairs);
}

/* The link name of the GitHub release files, or in a test build the environment variable that replaces it */
static const wchar_t *link(const wchar_t *test_variable, const wchar_t *release, wchar_t *buf, DWORD cap)
{
#ifdef DKB_LAUNCHER_TEST
    DWORD n = GetEnvironmentVariableW(test_variable, buf, cap);
    if (n && n < cap) return buf;
#else
    (void)test_variable, (void)buf, (void)cap;
#endif
    return release;
}

static int sha_hex(BCRYPT_HASH_HANDLE h, char out[65])
{
    unsigned char d[32];
    if (BCryptFinishHash(h, d, 32, 0)) return 0;
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", d[i]);
    return 1;
}

/* The tools zip of m into zip (through zip.part), checked against the manifest's size and SHA-256 */
static int fetch_tools(Job *job, const Manifest *m, const wchar_t *url, const wchar_t *zip)
{
    wchar_t part[PATH_CHARS], line[TEXT_CHARS], total[32], wtag[64];
    swprintf(part, PATH_CHARS, L"%ls.part", zip);
    swprintf(total, 32, L"%llu", (m->tools.size + (1 << 20) - 1) >> 20);
    widen(m->tag, wtag, 64);
    const wchar_t *pairs[] = {L"tag", wtag, L"total", total, NULL};
    for (int attempt = 0; attempt < 3 && !cancelled; attempt++) {
        if (attempt) Sleep(3000u << attempt);
        fill(line, TEXT_CHARS, T(DOWNLOADING), pairs);
        status(line, 0);
        BCRYPT_ALG_HANDLE alg;
        FileSink sink = {0};
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) break;
        if (BCryptCreateHash(alg, &sink.hash, NULL, 0, NULL, 0, 0)) { BCryptCloseAlgorithmProvider(alg, 0); break; }
        sink.file = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (sink.file == INVALID_HANDLE_VALUE) {
            BCryptDestroyHash(sink.hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            job_fail(job, T(WRITE_FAILED), L"path", part);
            return 0;
        }
        LARGE_INTEGER size = {0};
        int got = http_get(url, file_sink, &sink, m->tools.size, m->tools.size);
        got = got && GetFileSizeEx(sink.file, &size) && (unsigned long long)size.QuadPart == m->tools.size;
        CloseHandle(sink.file);
        char have[65] = "";
        int hashed = sha_hex(sink.hash, have);
        BCryptDestroyHash(sink.hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (got && hashed && !strcmp(have, m->tools.sha)) {
            if (MoveFileExW(part, zip, MOVEFILE_REPLACE_EXISTING)) return 1;
            DeleteFileW(part);
            job_fail(job, T(WRITE_FAILED), L"path", zip);
            return 0;
        }
        DeleteFileW(part);
        if (sink.write_failed) {   /* the disk, not the network: trying again would fail the same way */
            job_fail(job, T(WRITE_FAILED), L"path", part);
            return 0;
        }
        if (got) {   /* the whole file came and it is not the release's: nothing of it is kept, nothing runs */
            job_fail(job, T(CHECKSUM), NULL, NULL);
            return 0;
        }
        if (http_status && http_status != 200 && http_status < 500 && http_status != 429) break;
    }
    wchar_t code[32];
    swprintf(code, 32, L"HTTP %lu", http_status);
    job_fail(job, T(NETWORK), L"detail", http_status ? code : L"no answer");
    return 0;
}

/* Windows' own tar.exe -xf rel in folder; its exit code, 1 when it did not start */
static DWORD run_tar(const wchar_t *rel, const wchar_t *folder)
{
    wchar_t sys[MAX_PATH], tar[CMD_CHARS];
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    GetSystemDirectoryW(sys, MAX_PATH);
    swprintf(tar, CMD_CHARS, L"\"%ls\\tar.exe\" -xf \"%ls\"", sys, rel);
    si.dwFlags = STARTF_USESHOWWINDOW, si.wShowWindow = SW_HIDE;
    if (CreateProcessW(NULL, tar, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, folder, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    }
    return code;
}

static int install_job(Job *job)
{
    wchar_t url[PATH_CHARS * 2], override[PATH_CHARS], versions[PATH_CHARS], downloads[PATH_CHARS];
    static Text text;
    Manifest m;
    swprintf(versions, PATH_CHARS, L"%ls\\versions", job->base);
    swprintf(downloads, PATH_CHARS, L"%ls\\downloads", job->base);
    status(T(CHECKING), -1);
    const wchar_t *manifest = link(L"DKB_LAUNCHER_MANIFEST",
                                   L"https://github.com/" LAUNCHER_REPO L"/releases/latest/download/dkb-release.txt",
                                   override, PATH_CHARS);
    text.n = 0;
    if (!http_get(manifest, text_sink, &text, MANIFEST_LIMIT, 0)) {
        if (http_status == 404) job_fail(job, T(NO_RELEASE), L"url", L"https://github.com/" LAUNCHER_REPO L"/releases");
        else {
            wchar_t code[32];
            swprintf(code, 32, L"HTTP %lu", http_status);
            job_fail(job, T(NETWORK), L"detail", http_status ? code : L"no answer");
        }
        return 0;
    }
    if (!parse_manifest(text.data, text.n, &m)) {
        job_fail(job, T(NO_RELEASE), L"url", L"https://github.com/" LAUNCHER_REPO L"/releases");
        return 0;
    }
    wchar_t wtag[64], wname[MANIFEST_NAME_CHARS], wtop[MANIFEST_NAME_CHARS], wsha[65], escaped[512];
    widen(m.tag, wtag, 64), widen(m.tools.name, wname, MANIFEST_NAME_CHARS);
    widen(m.tools.top, wtop, MANIFEST_NAME_CHARS), widen(m.tools.sha, wsha, 65);
    size_t k = 0;   /* the '+' of a version is %2B in GitHub's links */
    for (const wchar_t *p = wname; *p && k + 4 < 512; p++) {
        if (*p == L'+') escaped[k++] = L'%', escaped[k++] = L'2', escaped[k++] = L'B';
        else escaped[k++] = *p;
    }
    escaped[k] = 0;
    wchar_t dl[PATH_CHARS];
    swprintf(url, PATH_CHARS * 2, L"%ls%ls/%ls",
             link(L"DKB_LAUNCHER_DOWNLOAD", L"https://github.com/" LAUNCHER_REPO L"/releases/download/", dl, PATH_CHARS),
             wtag, escaped);
    CreateDirectoryW(versions, NULL);
    CreateDirectoryW(downloads, NULL);
    if (!is_dir(versions) || !is_dir(downloads)) {
        job_fail(job, T(WRITE_FAILED), L"path", job->base);
        return 0;
    }
    wchar_t zip[PATH_CHARS], rel[PATH_CHARS], part[PATH_CHARS], top[PATH_CHARS], final[PATH_CHARS], check[PATH_CHARS];
    swprintf(zip, PATH_CHARS, L"%ls\\%ls.zip", downloads, wsha);
    if (!fetch_tools(job, &m, url, zip)) return 0;
#ifdef DKB_LAUNCHER_TEST
    wchar_t at[16];   /* DKB_LAUNCHER_CANCEL=unpack: the window closed just after the download (tests) */
    if (GetEnvironmentVariableW(L"DKB_LAUNCHER_CANCEL", at, 16) && !wcscmp(at, L"unpack")) InterlockedExchange(&cancelled, 1);
#endif
    if (cancelled) {   /* closed after the last block came: the checked zip is not unpacked */
        DeleteFileW(zip);
        return 0;
    }

    /* Unpacked by Windows' own tar.exe, run in the new folder with the zip as a relative ASCII path, so a user folder
       outside the ANSI code page cannot reach tar's narrow arguments */
    status(T(UNPACKING), -1);
    swprintf(part, PATH_CHARS, L"%ls\\%ls.part", versions, wtag);
    swprintf(final, PATH_CHARS, L"%ls\\%ls", versions, wtag);
    swprintf(top, PATH_CHARS, L"%ls\\%ls", part, wtop);
    remove_tree(part);
    if (!CreateDirectoryW(part, NULL)) {
        job_fail(job, T(WRITE_FAILED), L"path", part);
        return 0;
    }
    swprintf(rel, PATH_CHARS, L"..\\..\\downloads\\%ls.zip", wsha);
    DWORD code = run_tar(rel, part);
    if (cancelled) {   /* closed while tar ran: what it unpacked goes, nothing starts */
        remove_tree(part);
        DeleteFileW(zip);
        return 0;
    }
    char version[64];
    swprintf(check, PATH_CHARS, L"%ls\\VERSION", top);
    fetch_read_line(check, version, sizeof version);
    for (char *p = version; *p; p++)
        if (*p == ' ' || *p == '\t') { *p = 0; break; }
    swprintf(check, PATH_CHARS, L"%ls\\python\\pythonw.exe", top);
    if (code != 0 || strcmp(version, m.version) || !is_file(check)) {
        remove_tree(part);
        DeleteFileW(zip);
        job_fail(job, T(UNPACK_FAILED), L"path", final);
        return 0;
    }
    if (is_dir(final)) {   /* an incomplete folder of the same tag (it had no Python, or this would not run) */
        wchar_t trash[PATH_CHARS];
        swprintf(trash, PATH_CHARS, L"%ls.trash", final);
        remove_tree(trash);
        MoveFileW(final, trash);
        remove_tree(trash);
    }
    if (!MoveFileW(top, final)) {
        remove_tree(part);
        job_fail(job, T(WRITE_FAILED), L"path", final);
        return 0;
    }
    remove_tree(part);
    DeleteFileW(zip);
    char pointer[80];
    wchar_t current[PATH_CHARS];
    snprintf(pointer, sizeof pointer, "%s\n", m.tag);
    swprintf(current, PATH_CHARS, L"%ls\\current.txt", versions);
    if (!write_file(current, pointer)) {
        job_fail(job, T(WRITE_FAILED), L"path", current);
        return 0;
    }
    return 1;
}

static Job job;

static DWORD WINAPI install_thread(LPVOID user)
{
    (void)user;
    job.ok = install_job(&job);
    PostMessageW(window, WM_DONE, 0, 0);
    return 0;
}

static LRESULT CALLBACK window_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_STATUS: {
        LONG p = status_percent;
        SetWindowTextW(status_text, status_line);
        if (p < 0) SendMessageW(status_bar, PBM_SETMARQUEE, TRUE, 30);
        else SendMessageW(status_bar, PBM_SETMARQUEE, FALSE, 0), SendMessageW(status_bar, PBM_SETPOS, (WPARAM)p, 0);
        return 0;
    }
    case WM_CLOSE:   /* the download stops at its next block, an unpack once tar is done; then the window closes */
        InterlockedExchange(&cancelled, 1);
        return 0;
    case WM_DONE:   /* why it stopped, over the progress window, before it closes */
        if (!job.ok && job.error[0]) ask(h, job.error, MB_OK | MB_ICONERROR);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* Local\DKB-Recomp-launcher-<FNV-1a of the base folder, lower case>: one per base folder and user session */
static HANDLE base_mutex(const wchar_t *base)
{
    unsigned long long h = 1469598103934665603ull;
    for (const wchar_t *p = base; *p; p++) {
        wchar_t c = *p >= L'A' && *p <= L'Z' ? (wchar_t)(*p - L'A' + L'a') : *p;
        h = (h ^ (unsigned)c) * 1099511628211ull;
    }
    wchar_t name[96];
    swprintf(name, 96, L"Local\\DKB-Recomp-launcher-%016llx", h);
    return CreateMutexW(NULL, FALSE, name);
}

int fetch_install_latest(HINSTANCE inst, const wchar_t *base)
{
    wchar_t text[TEXT_CHARS];
    const wchar_t *pairs[] = {L"base", base, NULL};
    fill(text, TEXT_CHARS, T(CONFIRM), pairs);
    if (ask(NULL, text, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return 0;
    CreateDirectoryW(base, NULL);
    if (!is_dir(base)) {
        say_with(NULL, T(WRITE_FAILED), L"path", base);
        return 0;
    }
    /* A second launcher started meanwhile waits here until the first one's install is done, then starts that */
    HANDLE mutex = base_mutex(base);
    if (mutex) WaitForSingleObject(mutex, INFINITE);   /* WAIT_ABANDONED too: the other one was killed, this one goes on */
    if (current_installed(base)) {
        if (mutex) ReleaseMutex(mutex), CloseHandle(mutex);
        return 1;
    }
    wcsncpy(job.base, base, PATH_CHARS - 1);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = window_proc, wc.hInstance = inst, wc.lpszClassName = L"DkbLauncher";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    const int ww = 520, wh = 140;
    window = CreateWindowW(L"DkbLauncher", T(TITLE), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh,
                           NULL, NULL, inst, NULL);
    status_text = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, ww - 48, 22, window, NULL, inst, NULL);
    status_bar = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 16, 48, ww - 48, 20, window,
                               NULL, inst, NULL);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(status_text, WM_SETFONT, (WPARAM)font, TRUE);
    if (!headless()) ShowWindow(window, SW_SHOWNORMAL);
    HANDLE worker = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    if (worker) {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) TranslateMessage(&msg), DispatchMessageW(&msg);
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    window = NULL;
    if (mutex) ReleaseMutex(mutex), CloseHandle(mutex);
    return worker && job.ok;
}
