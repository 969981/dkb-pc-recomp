/* dkb-release.txt, the release manifest the Bananza launcher installs from (installer/update.py parse_manifest is the
   reference; manifest.c accepts exactly what it accepts, tests/installer_update_test.py ManifestParsersTest). MIT
   (LICENSE-MIT), as launcher.c. */
#ifndef DKB_MANIFEST_H
#define DKB_MANIFEST_H
#include <stddef.h>

#define MANIFEST_NAME_CHARS 129   /* installer/update.py NAME_RE: at most 128 characters */

typedef struct {
    char sha[65];                   /* lowercase hex */
    unsigned long long size;
    char name[MANIFEST_NAME_CHARS];
    char top[MANIFEST_NAME_CHARS];  /* the tools zip's one top folder; empty for the launcher */
} ManifestAsset;

typedef struct {
    char version[64];   /* 0.4.2+abc1234 */
    char tag[32];       /* v0.4.2 */
    ManifestAsset tools, launcher;
} Manifest;

/* A release tag: v and one to four dotted numbers, at most 31 characters (installer/update.py TAG_RE) */
int valid_tag(const char *tag);

/* text, n bytes (NUL is an ordinary byte), as m; 0 for anything installer/update.py parse_manifest refuses */
int parse_manifest(const char *text, size_t n, Manifest *m);

#endif
