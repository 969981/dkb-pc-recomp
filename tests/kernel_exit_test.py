"""Windows x64 ExitThread regression using the real kernel landing sites and fiber exit control flow.

Usage: python tests/kernel_exit_test.py --cc /path/to/clang [--revision BASELINE]
No process mitigation is modified. A disabled SetContextIpValidation cannot reproduce its fail-fast, so the test also
checks that the saved target has an in-stack frame and unwind metadata, then exercises nested and fiber exits.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
    parser.add_argument('--revision')
    args = parser.parse_args()
    if os.name != 'nt' or not args.cc:
        parser.error('requires Windows x64 and --cc with Clang')
    revision = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options', args.revision+'^{commit}'],
                                       cwd=ROOT, text=True).strip() if args.revision else None

    def source(path):
        return subprocess.check_output(['git', 'show', f'{revision}:{path}'], cwd=ROOT, text=True) if revision else (ROOT/path).read_text()

    kernel, host, platform, probe = (source('runtime/'+p) for p in ('kernel.c', 'hostfn.c', 'platform/win32.c', 'kernel_probe.c'))
    out = ROOT/'build/tests/kernel-exit'/(revision[:12] if revision else 'working')
    out.mkdir(parents=True, exist_ok=True)
    prefix = kernel[kernel.index('#ifdef _WIN32'):kernel.index('#include <stdio.h>')] if '__USE_MINGW_SETJMP_NON_SEH' in kernel else '#include <setjmp.h>\n'
    (out/'kernel_exit_setjmp.inc').write_text(prefix)
    # Keep real bodies and add observation hooks only: after a successful initial setjmp and before longjmp.
    bodies = ''
    for signature in ('static void thread_finish(', 'static __attribute__((noreturn)) void exit_now(',
                      'static void thread_main(', 'static void guest_main(', 'static void fiber_resumed(',
                      'static void fiber_main(', 'static void fiber_switch_to(RtThread *t, const Cpu *from, Fiber *to) {'):
        bodies += function(kernel, signature)
    bodies = bodies.replace('{ longjmp(t->exit_jmp, 1); }', '{ test_exit_stack(t); longjmp(t->exit_jmp, 1); }')
    bodies, n = re.subn(r'(if \(!setjmp\(t->exit_jmp\)\) \{)', r'\1\n        test_target(t);', bodies)
    assert n == 1
    bodies = bodies.replace('if (!setjmp(tcur->exit_jmp)) fn(c);', 'if (!setjmp(tcur->exit_jmp)) { test_target(tcur); fn(c); }')
    assert bodies.count('test_target(') == 2 and bodies.count('test_exit_stack(') == 1
    begin = kernel.index('    case 0x0A: {   // ExitThread')
    end = kernel.index('    case 0x0B:', begin)
    bodies += 'static void svc_exit(Cpu *c) { (void)c; uint32_t imm = 0x0A; switch (imm) {\n' + kernel[begin:end] + 'default: abort(); } }\n'
    bodies += function(host, 'uint64_t rt_call(Cpu *c, uint64_t fn, int n, ...) {')
    for signature in ('static void exit_nested(', 'static void exit_entry(', 'static void fiber_exit_fn(', 'static void fiber_exit_thread('):
        bodies += function(probe, signature)
    (out/'kernel_exit_source.inc').write_text(bodies)
    start = platform.index('struct PlatFiber {')
    end = platform.index('\nint plat_thread_spawn(', start)
    (out/'kernel_exit_platform.inc').write_text(platform[start:end])
    results = []
    for optimization in ('-O0', '-O2'):
        exe = out/('kernel_exit'+optimization[1:]+'.exe')
        command = [args.cc, '-std=gnu11', optimization, '-g', '-Wall', '-Wextra', '-Werror',
                   '-fno-strict-aliasing', '-march=x86-64-v3', '-D_WIN32_WINNT=0x0A00',
                   '-I', str(ROOT/'runtime'), '-I', str(out), str(ROOT/'tests/kernel_exit.c'), '-o', str(exe)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        log = result.stdout+result.stderr
        (out/(optimization[1:]+'.log')).write_text(log)
        print(optimization+': '+log, end='')
        results.append({'optimization': optimization, 'exit': result.returncode, 'output': log})
    (out/'results.json').write_text(json.dumps({'revision': revision or 'working tree', 'runs': results}, indent=2)+'\n')
    return int(any(r['exit'] for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
