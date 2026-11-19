/* Test driver for installer/launcher/manifest.c (tests/installer_update_test.py ManifestParsersTest compiles it).

       launcher_manifest_driver <file>...       one line per file: "bad", or "ok" and the fields parse_manifest read
       launcher_manifest_driver --tag <tag>...  one line per tag: 1 when valid_tag takes it, else 0 */
#include <stdio.h>
#include <string.h>

#include "../installer/launcher/manifest.h"

int main(int argc, char **argv)
{
    static char buf[1 << 20];
    int tags = argc > 1 && !strcmp(argv[1], "--tag");
    for (int i = tags ? 2 : 1; i < argc; i++) {
        if (tags) {
            printf("%d\n", valid_tag(argv[i]));
            continue;
        }
        FILE *f = fopen(argv[i], "rb");
        size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
        if (f) fclose(f);
        Manifest m;
        if (parse_manifest(buf, n, &m))
            printf("ok %s %s %s %llu %s %s %s %llu %s\n", m.version, m.tag, m.tools.sha, m.tools.size, m.tools.name,
                   m.tools.top, m.launcher.sha, m.launcher.size, m.launcher.name);
        else
            printf("bad\n");
    }
    return 0;
}
