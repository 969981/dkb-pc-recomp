"""Original GS system-input and subgroup admission controls; no Vulkan loader.

Uses actual reflection, interface validation and pipe_miss from production.
Complete positive modules are independently spirv-val checked. Negative parser
controls are never submitted to Vulkan and are not claimed to be valid modules.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import geometry_pipeline_test as pipeline
import spirv_interface_test as interface

ROOT = Path(__file__).resolve().parents[1]


def module(stage, system=(), operation=None, caps=(), decorate=None, scope=3):
    words = [0x07230203, 0x00010300, 0, 128, 0]
    def op(code, *args):
        words.extend([(len(args) + 1) << 16 | code, *args])
    for cap in dict.fromkeys((1,) + ((2,) if stage == 3 else ()) + tuple(caps)):
        op(17, cap)
    op(14, 0, 1)
    variables = ([20] if stage == 3 else []) + [21] + [30+i for i in range(len(system))]
    op(15, stage, 100, 0x6e69616d, 0, *variables)
    if stage == 3:
        for mode, args in ((19, ()), (0, (1,)), (26, (3,)), (29, ())):
            op(16, 100, mode, *args)
    elif stage == 4:
        op(16, 100, 7)
    if stage == 3:
        op(71, 20, 11, 0)
    op(71, 21, 30 if stage == 4 else 11, 0)
    for i, (builtin, kind, storage) in enumerate(system):
        op(71, 30+i, 11, builtin)
        if decorate:
            op(71, 30+i, *decorate)
    op(19, 1); op(33, 2, 1); op(22, 3, 32); op(23, 4, 3, 4)
    op(21, 5, 32, 0); op(23, 6, 5, 4); op(43, 5, 7, 0); op(43, 5, 8, 1)
    op(43, 3, 9, 0); op(43, 3, 10, 0x3f800000); op(44, 4, 11, 9, 9, 9, 10)
    op(32, 12, 3, 4); op(28, 13, 4, 8); op(32, 14, 1, 13)
    op(20, 15); op(41, 15, 16); op(43, 5, 17, scope)
    op(21, 18, 32, 1); op(28, 19, 5, 8)
    if any(kind == 'uint16' for _, kind, _ in system):
        op(21, 22, 16, 0)
    if any(kind == 'uint64' for _, kind, _ in system):
        op(21, 23, 64, 0)
    op(23, 24, 5, 3)
    if stage == 3:
        op(59, 14, 20, 1)
    op(59, 12, 21, 3)
    types = {'uint': 5, 'mask': 6, 'float': 3, 'signed': 18, 'array': 19,
             'uint16': 22, 'uint64': 23, 'short_mask': 24}
    for i, (_, kind, storage) in enumerate(system):
        op(32, 40+i, storage, types[kind]); op(59, 40+i, 30+i, storage)
    op(54, 1, 100, 0, 2); op(248, 101)
    for i, (_, kind, storage) in enumerate(system):
        if storage == 1:
            op(61, types[kind], 60+i, 30+i)
    if operation:
        code = {'vote': 334, 'ballot': 339, 'shuffle': 345, 'relative': 347, 'unsupported': 349}[operation]
        result_type = 15 if operation == 'vote' else 6 if operation == 'ballot' else 5
        op(code, result_type, 80, 17, 16 if operation in ('vote', 'ballot') else 7,
           *(() if operation in ('vote', 'ballot') else (7,)))
    for _ in range(3 if stage == 3 else 1):
        op(62, 21, 11)
        if stage == 3:
            op(218)
    if stage == 3:
        op(219)
    op(253); op(56)
    return struct.pack('<%dI' % len(words), *words)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--spirv-val', required=True)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/tests/geometry-subgroups')
    parser.add_argument('--opt', choices=('0', '2'), default='2')
    parser.add_argument('--validate', action='store_true')
    parser.add_argument('--pipeline-source', type=Path, help='optional saved production source for an unchanged baseline control')
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.relative_to((ROOT / 'build').resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    lane = (41, 'uint', 1)
    mask = (4420, 'mask', 1)
    cases = {'vs': module(0), 'ps': module(4), 'plain': module(3),
             'lane': module(3, [lane], caps=[61]), 'mask': module(3, [mask], caps=[64]),
             'both': module(3, [lane, mask], caps=[62, 64, 65]),
             'declaration': module(3, caps=[61, 62, 64, 65]),
             'vote': module(3, operation='vote', caps=[61, 62]),
             'ballot': module(3, operation='ballot', caps=[61, 64]),
             'shuffle': module(3, operation='shuffle', caps=[61, 65]),
             'relative': module(3, operation='relative', caps=[61, 66]),
             'unsupported': module(3, operation='unsupported', caps=[61, 63]),
             'scope': module(3, operation='vote', caps=[61, 62], scope=2),
             'extension': module(3, caps=[4423]),
             'missing_cap': module(3, [lane]),
             'uint16': module(3, [(41, 'uint16', 1)]),
             'uint64': module(3, [(41, 'uint64', 1)]),
             'short_mask': module(3, [(4420, 'short_mask', 1)], caps=[61, 64])}
    positive = set(cases) - {'unsupported', 'scope', 'extension', 'missing_cap', 'uint16', 'uint64', 'short_mask'}
    for name, system, decor in (
        ('signed', [(41, 'signed', 1)], None), ('float', [(41, 'float', 1)], None),
        ('array', [(41, 'array', 1)], None), ('mask_scalar', [(4420, 'uint', 1)], None),
        ('output', [(41, 'uint', 3)], None), ('duplicate', [lane, lane], None),
        ('unknown', [(4416, 'mask', 1)], None), ('flat', [lane], (14,)),
        ('location', [lane], (30, 2)), ('component', [lane], (31, 0)),
        ('invariant', [lane], (18,))):
        cases[name] = module(3, system, caps=[61, 64], decorate=decor)
    for name, data in cases.items():
        path = args.out / f'{name}.spv'
        path.write_bytes(data)
        if args.validate and name in positive:
            pipeline.run([args.spirv_val, '--target-env', 'vulkan1.3', path], args.out / f'{name}-val.log')
    source = (args.pipeline_source or ROOT / 'runtime/vk_pipeline.c').read_text()
    if args.pipeline_source:
        view = args.out / 'saved-source/runtime'
        view.mkdir(parents=True)
        (view / 'vk_pipeline.c').write_text(source)
        (view / 'gfx_shader.c').write_bytes((ROOT / 'runtime/gfx_shader.c').read_bytes())
        pipeline.ROOT = view.parent
    # Baseline uses exactly the same assertions; absent new property storage is
    # disclosed as an inert provider, not a replacement implementation.
    feature = ('#define HAS_SUBGROUP_GUARD 1\n' if 'subgroup_operations;' in source else
               '#define GEOMETRY_SUBGROUP_BASELINE 1\n')
    body = (ROOT / 'tests/geometry_pipeline.c').read_text()
    generated = (feature + pipeline.pipeline_source() + '\n' + pipeline.shader_adapter() +
                 '\nstatic void fixture_resources(GfxShader*s,const char*n){(void)s;(void)n;}\n' +
                 '#define main unexecuted_legacy_main\n' + body + '\n#undef main\n' +
                 (ROOT / 'tests/geometry_subgroup.c').read_text() + '\n' +
                 interface.function(source, 'static Pipe *pipe_miss('))
    path = args.out / 'fixture.c'
    path.write_text(generated)
    exe = args.out / 'fixture.exe'
    pipeline.run([args.cc, '-std=gnu11', f'-O{args.opt}', '-ffunction-sections', '-fdata-sections',
                  '-I', ROOT / 'runtime', '-I', args.deps / 'include', '-I', args.deps / 'include/vma',
                  path, '-Wl,--gc-sections,--exclude-all-symbols', '-o', exe], args.out / 'compile.log')
    result = subprocess.run([str(exe), str(args.out)], capture_output=True, text=True, timeout=30)
    (args.out / 'test.log').write_text(result.stdout + result.stderr)
    manifest = {'result': result.stdout.strip(), 'exit_code': result.returncode, 'opt': args.opt,
                'pipeline_source_sha256': hashlib.sha256((args.pipeline_source or ROOT / 'runtime/vk_pipeline.c').read_bytes()).hexdigest(),
                'validated_modules': len(positive) if args.validate else 0,
                'providers': ['existing pipeline CPU module/pipeline spies and fixed device limits',
                              'real pipe_miss; unexpected layout requests are counted and fail with NULL; workers abort',
                              'pipe_add records the real rejection without a Vulkan object',
                              'baseline missing subgroup-property fields use an inert test provider'],
                'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                  for name in ('runtime/vk_pipeline.c', 'tests/geometry_subgroup.c',
                                               'tests/geometry_subgroup_test.py')}}
    (args.out / 'handoff.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
