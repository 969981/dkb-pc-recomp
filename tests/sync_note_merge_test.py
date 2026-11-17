"""Exercise the GPU part notes of runtime/vk_sync.c with slices recording on other threads.

A sliced pass's slices note their draws while the replay thread keeps noting into the part it records
(TOTK_PERF_SLOW_GPU_MS). Each slice notes into a note of its own (vk_sync_slice_note_begin) and the replay thread folds
them into the part that executes the slices (vk_sync_note_merge). The note functions come from vk_sync.c as they are;
no Vulkan loader or GPU is linked, and a held part is only the `held` ring entry the merge reads.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]


def source():
    sync = (ROOT / 'runtime/vk_sync.c').read_text()
    notes = sync[sync.index('#define NOTE_PAIRS'):]
    notes = notes[:notes.index('\n}\n', notes.index('static void note_fold(')) + 3]
    held = sync[sync.index('#define HELD_MAX'):sync.index('static int held_head, held_n;') + len('static int held_head, held_n;')]
    prelude = r'''
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *VkCommandBuffer;
typedef struct VkSyncNote VkSyncNote;   // vk.h
static int ts_cur = -1;                 // vk_sync.c: the pair of the part being recorded, -1 untimed
static unsigned checks, failures;
#define CHECK(c, message) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL: %s\n", message); } } while (0)
'''
    body = r'''
#define SLICES 4
#define SLICE_DRAWS 5000
#define PART_DRAWS 20000

static void *slice_thread(void *arg) {   // vk_exec.c par_record: one slice of indirect draws on a worker
    int s = (int)(intptr_t)arg;
    VkSyncNote *n = vk_sync_slice_note_begin(1);
    if (!vk_sync_noting()) return NULL;
    for (int i = 0; i < SLICE_DRAWS; i++) {
        vk_sync_note(1, 0x100 + (uint64_t)s, 0x200, 0);
        vk_sync_note_args(0x10000000ull * (uint64_t)(s + 1) + 20ull * (uint64_t)i);
    }
    vk_sync_slice_note_end();
    return n;
}

int main(void) {
    // Timed part: the replay thread notes its own draws while four slices note theirs on other threads.
    ts_cur = 0;
    vk_sync_note_write(0x10000000, 64, 0xabc);   // a storage write ahead of the pass, in the part
    pthread_t t[SLICES];
    for (int s = 0; s < SLICES; s++) pthread_create(&t[s], NULL, slice_thread, (void *)(intptr_t)s);
    for (int i = 0; i < PART_DRAWS; i++) vk_sync_note(0, 0x300, 0x400, (uint64_t)i);
    VkSyncNote *sn[SLICES];
    for (int s = 0; s < SLICES; s++) pthread_join(t[s], (void **)&sn[s]);
    CHECK(note.draws == PART_DRAWS && note.draws_ind == 0 && note.nargs == 1, "slices left the part's note alone");
    for (int s = 0; s < SLICES; s++) CHECK(sn[s] && sn[s]->draws_ind == SLICE_DRAWS && sn[s]->nargs == SLICE_DRAWS,
                                           "each slice noted its own draws and arguments");
    for (int s = 0; s < SLICES; s++) vk_sync_note_merge(sn[s], 0);
    CHECK(note.draws == PART_DRAWS && note.draws_ind == SLICES * SLICE_DRAWS, "merged counts");
    CHECK(note.nargs == 1 + SLICES * SLICE_DRAWS, "merged argument list");
    CHECK(note.args[0].size == 64 && note.args[0].a == 0xabc, "the part's storage write stays ahead of the slices' draws");
    int ordered = 1;
    for (int s = 0; s < SLICES; s++)
        for (int i = 0; i < SLICE_DRAWS; i++) {
            const NoteArg *e = &note.args[1 + s * SLICE_DRAWS + i];
            ordered &= e->addr == 0x10000000ull * (uint64_t)(s + 1) + 20ull * (uint64_t)i && e->a == 0x100 + (uint64_t)s &&
                       e->b == 0x200 && !e->size;
        }
    CHECK(ordered, "slice arguments in slice order, each with its slice's stages");
    CHECK(note.pairs == 1 + SLICES, "distinct stage pairs of the part and the slices");
    CHECK(note.max_work == PART_DRAWS - 1 && note.max_a == 0x300, "largest direct draw kept");
    CHECK(vk_sync_noting(), "the replay thread notes into the part after the slices ended");

    // More distinct pairs than a note keeps: the merge stops at NOTE_PAIRS, and a larger direct draw wins.
    VkSyncNote *wide = vk_sync_slice_note_begin(1);
    for (uint64_t k = 0; k < 2 * NOTE_PAIRS; k++) vk_sync_note(0, 0x1000 + k, 0, k == 3 ? 1u << 30 : 1);
    vk_sync_slice_note_end();
    vk_sync_note_merge(wide, 0);
    CHECK(note.pairs == NOTE_PAIRS, "pairs capped at NOTE_PAIRS");
    CHECK(note.max_work == 1u << 30 && note.max_a == 0x1003, "a slice's larger direct draw becomes the largest");

    // A pass whose part is untimed: the slice notes nothing and the merge has nothing to add.
    uint32_t draws = note.draws;
    VkSyncNote *off = vk_sync_slice_note_begin(0);
    CHECK(!off && !vk_sync_noting(), "an untimed slice does not note");
    vk_sync_note(0, 1, 2, 3), vk_sync_note_args(0x40), vk_sync_note_write(0x40, 4, 5);
    vk_sync_slice_note_end();
    vk_sync_note_merge(off, 0);
    CHECK(note.draws == draws, "an untimed slice's draws went nowhere");

    // A held pass: its slices go to the oldest held part, not to the part being recorded.
    held_n = 1, held_head = 0, held[0].ts = 5;
    VkSyncNote *h = vk_sync_slice_note_begin(1);
    vk_sync_note(1, 7, 8, 0), vk_sync_note_args(0x80);
    vk_sync_slice_note_end();
    vk_sync_note_merge(h, 1);
    CHECK(held[0].note.draws_ind == 1 && held[0].note.nargs == 1 && note.draws == draws, "held pass merged into the held part");
    held[0].ts = -1;   // an untimed held part drops the note
    h = vk_sync_slice_note_begin(1);
    vk_sync_note(1, 7, 8, 0);
    vk_sync_slice_note_end();
    vk_sync_note_merge(h, 1);
    CHECK(held[0].note.draws_ind == 1, "untimed held part: note dropped");

    // The part ended (ts_cur -1): a late merge is dropped, and the replay thread no longer notes.
    ts_cur = -1;
    h = vk_sync_slice_note_begin(1);
    vk_sync_note(0, 1, 1, 1);
    vk_sync_slice_note_end();
    vk_sync_note_merge(h, 0);
    CHECK(!vk_sync_noting() && note.draws == draws, "untimed part: nothing noted or merged");
    free(note.args), free(held[0].note.args);
    printf("%u checks, %u failed\n", checks, failures);
    return failures != 0;
}
'''
    return prelude + notes + '\n' + held + '\n' + function(sync, 'void vk_sync_note_merge(') + '\n' + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc'))
    args = parser.parse_args()
    if not args.cc:
        parser.error('pass --cc with a C compiler')
    out = ROOT / 'build/test/sync-note-merge'
    out.mkdir(parents=True, exist_ok=True)
    src = out / 'notes.c'
    src.write_text(source())
    failed = 0
    for opt in ('-O0', '-O2'):
        exe = out / ('notes' + opt + ('.exe' if os.name == 'nt' else ''))
        command = [args.cc, '-std=gnu11', opt, '-Wall', '-Wextra', '-Werror', '-pthread', str(src), '-o', str(exe)]
        run = subprocess.run(command, capture_output=True, text=True)
        (out / (opt + '-compile.log')).write_text(run.stdout + run.stderr)
        if run.returncode:
            print(run.stdout + run.stderr)
            return run.returncode
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        (out / (opt + '-run.log')).write_text(run.stdout + run.stderr)
        print(opt, run.stdout + run.stderr)
        failed |= run.returncode
    return failed


if __name__ == '__main__':
    raise SystemExit(main())
