/* dkb-release.txt for the Bananza launcher's first install (manifest.h). MIT (LICENSE-MIT), as launcher.c.

   The format and the reference reader are installer/update.py parse_manifest; this reader accepts exactly what that one
   accepts, so a manifest the window would refuse never reaches a download here (tests/installer_update_test.py
   ManifestParsersTest feeds both the same files):

       dkb-release 1
       version <the release's VERSION, 0.4.2+abc1234>
       tag v<its numbers>
       tools <sha256> <bytes> <zip name> <top folder in the zip>
       launcher <sha256> <bytes> <exe name>

   Lines end at \n; words are split at the whitespace Python's str.split() splits at (space, \t to \r, \x1c to \x1f).
   The first line is the header and nothing else; blank lines after it are skipped; a first word seen twice, a missing
   or longer field and any byte above 127 refuse the file. Lines with another first word are left for later formats
   (at most 64 different first words). */
#include <string.h>

#include "manifest.h"

#define TOOLS_LIMIT (8ull << 30)      /* installer/update.py TOOLS_LIMIT */
#define LAUNCHER_LIMIT (16ull << 20)  /* and LAUNCHER_LIMIT */
#define MAX_KEYS 64

typedef struct { const char *p; size_t n; } Word;
typedef struct { const char *p[4]; size_t n[4]; int count; } Numbers;

static int space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f); }
static int digit(char c) { return c >= '0' && c <= '9'; }
static int alnum(char c) { return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is(Word w, const char *s) { return w.n == strlen(s) && !memcmp(w.p, s, w.n); }

/* The words of [p, end), the first cap of them into out; returns how many there are */
static size_t split(const char *p, const char *end, Word *out, size_t cap)
{
    size_t k = 0;
    while (p < end) {
        while (p < end && space((unsigned char)*p)) p++;
        if (p == end) break;
        const char *start = p;
        while (p < end && !space((unsigned char)*p)) p++;
        if (k < cap) out[k].p = start, out[k].n = (size_t)(p - start);
        k++;
    }
    return k;
}

/* One to four dotted numbers at the start of [p, p + n) into out: the length they cover, 0 when p starts with none */
static size_t numbers(const char *p, size_t n, Numbers *out)
{
    size_t i = 0;
    out->count = 0;
    for (;;) {
        size_t start = i;
        while (i < n && digit(p[i])) i++;
        if (i == start) return 0;
        out->p[out->count] = p + start, out->n[out->count] = i - start, out->count++;
        if (out->count == 4 || i + 1 >= n || p[i] != '.' || !digit(p[i + 1])) return i;
        i++;
    }
}

/* \d+(\.\d+){0,3}(\+[0-9A-Za-z.]+)?, at most 63 characters (installer/update.py VERSION_RE) */
static int version_ok(Word w, Numbers *num)
{
    size_t i = w.n <= 63 ? numbers(w.p, w.n, num) : 0;
    if (!i) return 0;
    if (i == w.n) return 1;
    if (w.p[i] != '+' || i + 1 == w.n) return 0;
    for (i++; i < w.n; i++)
        if (!alnum(w.p[i]) && w.p[i] != '.') return 0;
    return 1;
}

static int tag_ok(Word w, Numbers *num)
{
    return w.n >= 2 && w.n <= 31 && w.p[0] == 'v' && numbers(w.p + 1, w.n - 1, num) == w.n - 1;
}

int valid_tag(const char *tag)
{
    Numbers num;
    Word w = {tag, strlen(tag)};
    return tag_ok(w, &num);
}

/* a and b as numbers of any length: 0 when equal */
static int number_cmp(const char *a, size_t an, const char *b, size_t bn)
{
    while (an > 1 && *a == '0') a++, an--;
    while (bn > 1 && *b == '0') b++, bn--;
    if (an != bn) return an < bn ? -1 : 1;
    return memcmp(a, b, an);
}

/* installer/update.py compare() == 0: the same numbers, the shorter padded with zeros (0.4 is 0.4.0) */
static int same_release(const Numbers *x, const Numbers *y)
{
    for (int i = 0; i < 4; i++)
        if (number_cmp(i < x->count ? x->p[i] : "0", i < x->count ? x->n[i] : 1, i < y->count ? y->p[i] : "0",
                       i < y->count ? y->n[i] : 1))
            return 0;
    return 1;
}

/* installer/update.py NAME_RE: [A-Za-z0-9][A-Za-z0-9._+-]{0,127} */
static int name_ok(Word w)
{
    if (!w.n || w.n > 128 || !alnum(w.p[0])) return 0;
    for (size_t i = 1; i < w.n; i++)
        if (!alnum(w.p[i]) && w.p[i] != '.' && w.p[i] != '_' && w.p[i] != '+' && w.p[i] != '-') return 0;
    return 1;
}

static int ends_with(Word w, const char *suffix)   /* ASCII, ignoring case */
{
    size_t n = strlen(suffix);
    if (w.n < n) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = w.p[w.n - n + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != suffix[i]) return 0;
    }
    return 1;
}

static void copy(char *out, Word w)
{
    memcpy(out, w.p, w.n);
    out[w.n] = 0;
}

/* <sha256> <bytes> <name> [<top>] (installer/update.py parse_manifest asset) */
static int asset(const Word *w, size_t words, const char *suffix, unsigned long long limit, ManifestAsset *a)
{
    if (w[0].n != 64) return 0;
    for (size_t i = 0; i < 64; i++)
        if (!digit(w[0].p[i]) && !(w[0].p[i] >= 'a' && w[0].p[i] <= 'f')) return 0;
    unsigned long long size = 0;
    int over = 0;
    for (size_t i = 0; i < w[1].n; i++) {
        if (!digit(w[1].p[i])) return 0;
        if (!over) size = size * 10 + (unsigned)(w[1].p[i] - '0');
        over |= size > limit;   /* the rest only has to be digits: no overflow past the limit */
    }
    if (over || !size || !name_ok(w[2]) || !ends_with(w[2], suffix) || (words > 3 && !name_ok(w[3]))) return 0;
    copy(a->sha, w[0]);
    a->size = size;
    copy(a->name, w[2]);
    if (words > 3) copy(a->top, w[3]);
    return 1;
}

int parse_manifest(const char *text, size_t n, Manifest *m)
{
    Word keys[MAX_KEYS], words[6], version = {0}, tag = {0}, tools[4], launcher[3];
    size_t nkeys = 0;
    int header = 0, have = 0;
    memset(m, 0, sizeof *m);
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)text[i] > 127) return 0;
    for (const char *p = text, *end = text + n;;) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        size_t k = split(p, eol ? eol : end, words, 6);
        if (!header) {
            if (k != 2 || !is(words[0], "dkb-release") || !is(words[1], "1")) return 0;
            header = 1;
        } else if (k) {
            for (size_t j = 0; j < nkeys; j++)
                if (keys[j].n == words[0].n && !memcmp(keys[j].p, words[0].p, words[0].n)) return 0;   /* twice */
            if (nkeys == MAX_KEYS) return 0;
            keys[nkeys++] = words[0];
            if (is(words[0], "version")) {
                if (k != 2) return 0;
                version = words[1], have |= 1;
            } else if (is(words[0], "tag")) {
                if (k != 2) return 0;
                tag = words[1], have |= 2;
            } else if (is(words[0], "tools")) {
                if (k != 5) return 0;
                memcpy(tools, words + 1, sizeof tools), have |= 4;
            } else if (is(words[0], "launcher")) {
                if (k != 4) return 0;
                memcpy(launcher, words + 1, sizeof launcher), have |= 8;
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    Numbers vn, tn;
    if (have != 15 || !version_ok(version, &vn) || !tag_ok(tag, &tn) || !same_release(&vn, &tn) ||
        !asset(tools, 4, ".zip", TOOLS_LIMIT, &m->tools) || !asset(launcher, 3, ".exe", LAUNCHER_LIMIT, &m->launcher)) {
        memset(m, 0, sizeof *m);
        return 0;
    }
    copy(m->version, version);
    copy(m->tag, tag);
    return 1;
}
