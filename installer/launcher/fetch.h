/* SPDX-License-Identifier: GPL-3.0-or-later
   The entry points of fetch.c, the DKB launcher's first install (see fetch.c for its sources and licence). */
#ifndef DKB_FETCH_H
#define DKB_FETCH_H
#include <windows.h>

/* The first line of a small text file, ASCII, at most cap - 1 characters; empty when there is none */
void fetch_read_line(const wchar_t *path, char *out, int cap);

/* The latest release's tools into base\versions, with a progress window, after the player agreed. 1 when
   versions\current.txt names an unpacked version (this run's, or one another launcher installed meanwhile); 0 when
   it did not get there (cancelled, or it already said why) */
int fetch_install_latest(HINSTANCE inst, const wchar_t *base);

#endif
