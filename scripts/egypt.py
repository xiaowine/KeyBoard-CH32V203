#!/usr/bin/env python3
"""
egypt (Python) - create call graph from GCC RTL dump

This is a lightweight Python3 reimplementation of the original
`egypt` Perl script bundled in this extension. It reads GCC RTL
"expand" files and emits a Graphviz DOT callgraph to stdout.

Usage examples:
  python3 egypt.py --ext .expand path/to/obj
  python3 egypt.py *.expand | dot -Tpng -o callgraph.png

Options supported (subset compatible with original Perl script):
  --callers, --callees, --omit, --include-external,
  --cluster-by-file, --summarize-callers, --ext

Note: this script focuses on reproducing behaviour used by the
extension: recursively scanning supplied paths for files matching
the given extension and emitting a DOT callgraph.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import json
from collections import defaultdict
from typing import Dict, List, Set


def dot_string(s: str) -> str:
    return '"' + s.replace('"', '\\"') + '"'


def split_function_list(items: List[str]) -> List[str]:
    out = []
    for it in items:
        if '(' in it or ')' in it:
            out.append(it)
        else:
            out.extend([x for x in it.split(',') if x != ''])
    return out


def find_target_files(paths: List[str], ext: str) -> List[str]:
    out = []
    if not paths:
        # default: scan current directory
        paths = ['.']
    for p in paths:
        if os.path.isfile(p):
            if p.endswith(ext):
                out.append(p)
        elif os.path.isdir(p):
            for root, _, files in os.walk(p):
                for fn in files:
                    if fn.endswith(ext):
                        out.append(os.path.join(root, fn))
        else:
            # allow shell wildcards resolved by shell
            # if nothing matches, ignore
            pass
    return out


def parse_files(files: List[str], ext: str):
    funcs: Dict[str, dict] = {}
    curfunc = None
    mangle_map: Dict[str, str] = {}

    call_re = re.compile(r'\(call.*"([^"]+)"')
    sym_re = re.compile(r'\(symbol_ref[^\"]*"([^\"]*)"')
    func1_re = re.compile(r'^;; Function (\S+)\s*$')
    func2_re = re.compile(r'^;; Function (.*?)\s+\((\S+)(?:,.*)?\).*$', re.DOTALL)

    for fn in files:
        curfunc = None
        try:
            with open(fn, 'r', encoding='utf-8', errors='replace') as fh:
                parts = fn.split('.')
                if len(parts) >= 3:
                    source_fn = '.'.join(parts[:-2])
                else:
                    source_fn = fn

                for line in fh:
                    line = line.rstrip('\n')
                    m = func1_re.match(line)
                    if m:
                        curfunc = m.group(1)
                        funcs.setdefault(curfunc, {})
                        funcs[curfunc]['file'] = source_fn
                        continue
                    m = func2_re.match(line)
                    if m:
                        unmangled = m.group(1)
                        mangled = m.group(2)
                        # attempt to fix common C2/D2 -> C1/D1 discrepancy
                        m2 = re.match(r'(_ZN(\d+))(.*)', mangled)
                        if m2:
                            prefix, c2_pos_str, suffix = m2.group(1), m2.group(2), m2.group(3)
                            try:
                                c2_pos = int(c2_pos_str)
                                if c2_pos + 1 < len(suffix):
                                    sub = suffix[c2_pos:c2_pos+2]
                                    if sub in ('C2', 'D2'):
                                        # replace the second char with '1'
                                        suffix = suffix[:c2_pos+1] + '1' + suffix[c2_pos+2:]
                                        mangled = prefix + suffix
                            except Exception:
                                pass
                        curfunc = mangled
                        funcs.setdefault(curfunc, {})
                        funcs[curfunc]['file'] = source_fn
                        if unmangled != mangled:
                            mangle_map[unmangled] = mangled
                            funcs[curfunc].setdefault('attrs', {})['label'] = unmangled
                        continue

                    cm = call_re.search(line)
                    if cm:
                        callee = cm.group(1)
                        if curfunc is not None:
                            save_call(funcs, curfunc, callee, 'call')
                        continue
                    sm = sym_re.search(line)
                    if sm:
                        callee = sm.group(1)
                        if curfunc is not None and callee != '':
                            save_call(funcs, curfunc, callee, 'ref')
        except Exception as e:
            print(f"warning: failed to read {fn}: {e}", file=sys.stderr)

    return funcs, mangle_map


def save_call(funcs: Dict[str, dict], caller: str, callee: str, reftype: str):
    callee = re.sub(r'^\^+', '', callee)
    funcs.setdefault(caller, {})
    calls = funcs[caller].setdefault('calls', {})
    if callee not in calls:
        calls[callee] = reftype
    if reftype == 'call' and callee not in funcs:
        funcs[callee] = {}


def mangle(name: str, funcs: Dict[str, dict], mangle_map: Dict[str, str]):
    if name in mangle_map:
        return mangle_map[name]
    elif name in funcs:
        return name
    else:
        print(f'warning: unknown function "{name}" ignored', file=sys.stderr)
        return None


def dfs(funcs: Dict[str, dict], f: str, key: str, visited: Set[str]):
    visited.add(f)
    if f in funcs and key in funcs[f]:
        for g in list(funcs[f][key].keys()):
            if g not in visited:
                dfs(funcs, g, key, visited)


def functions_reachable_from(funcs: Dict[str, dict], fs: List[str], key: str) -> Set[str]:
    setn = set()
    for f in fs:
        if f is not None:
            dfs(funcs, f, key, setn)
    return setn


def make_reverse_index(funcs: Dict[str, dict]):
    for f, attrs in list(funcs.items()):
        calls = attrs.get('calls', {})
        for g, t in calls.items():
            funcs.setdefault(g, {})
            funcs[g].setdefault('sllac', {})[f] = t


def cull(funcs: Dict[str, dict], keep: Set[str]):
    for f in list(funcs.keys()):
        if f not in keep:
            del funcs[f]
        else:
            calls = funcs[f].get('calls', {})
            for g in list(calls.keys()):
                if g not in keep:
                    del calls[g]


def main():
    parser = argparse.ArgumentParser(description='egypt (Python) - generate callgraph DOT from GCC RTL expand files')
    parser.add_argument('--omit', action='append', default=[], help='omit function(s) (comma separated or multiple uses)')
    parser.add_argument('--callers', action='append', default=[], help='show only callers of given function(s)')
    parser.add_argument('--callees', action='append', default=[], help='show only callees of given function(s)')
    parser.add_argument('--include-external', action='store_true', help='include external functions')
    parser.add_argument('--cluster-by-file', action='store_true', help='cluster nodes by source file')
    parser.add_argument('--summarize-callers', type=int, help='summarize functions with many callers')
    parser.add_argument('--ext', default='.253r.expand', help='file extension to search for')
    parser.add_argument('paths', nargs='*', help='files or directories to scan')
    args = parser.parse_args()

    omit = split_function_list(args.omit)
    callers_of = split_function_list(args.callers)
    callees_of = split_function_list(args.callees)

    target_files = find_target_files(args.paths, args.ext)
    funcs, mangle_map = parse_files(target_files, args.ext)

    # mangle specified names
    omit_mangled = []
    for o in omit:
        m = mangle(o, funcs, mangle_map)
        if m:
            omit_mangled.append(m)
    callers_mangled = [m for c in callers_of if (m := mangle(c, funcs, mangle_map))]
    callees_mangled = [m for c in callees_of if (m := mangle(c, funcs, mangle_map))]

    for o in omit_mangled:
        if o in funcs:
            del funcs[o]

    omit_map = {k: True for k in omit_mangled}

    gen_counter = 0

    if callees_mangled:
        keep = functions_reachable_from(funcs, callees_mangled, 'calls')
        cull(funcs, keep)
    if callers_mangled:
        make_reverse_index(funcs)
        keep = functions_reachable_from(funcs, callers_mangled, 'sllac')
        cull(funcs, keep)

    if args.summarize_callers is not None:
        n_incoming_calls = defaultdict(int)
        for caller in funcs:
            for callee in funcs[caller].get('calls', {}):
                n_incoming_calls[callee] += 1
        for caller in list(funcs.keys()):
            for callee in list(funcs[caller].get('calls', {}).keys()):
                if n_incoming_calls.get(callee, 0) >= args.summarize_callers:
                    del funcs[caller]['calls'][callee]
        # Add summary nodes
        for callee, n_calls in list(n_incoming_calls.items()):
            if n_calls >= args.summarize_callers:
                node_id = f'node{gen_counter:08d}'
                gen_counter += 1
                funcs[node_id] = {'is_summary': True, 'attrs': {'shape': 'plaintext', 'label': f"{n_calls} callers"}, 'calls': {callee: 'call'}}

    def show_call_p(reftype, callee):
        return (callee in funcs and 'file' in funcs[callee]) or (args.include_external and reftype == 'call' and callee not in omit_map)

    # Build JSON structure (default) or DOT (if --dot specified)
    nodes = []
    edges = []
    funcs_by_file = defaultdict(list)

    for f in sorted(funcs.keys()):
        if not args.include_external and not ('file' in funcs[f] or funcs[f].get('is_summary')):
            continue
        if funcs[f].get('is_summary'):
            callees = list(funcs[f].get('calls', {}).keys())
            if len(callees) != 1:
                continue
            if not show_call_p('call', callees[0]):
                continue
        a = funcs[f].get('attrs', {})
        node = {
            'id': f,
            'attrs': a or {},
        }
        if 'file' in funcs[f]:
            node['file'] = funcs[f]['file']
            funcs_by_file[funcs[f]['file']].append(f)
        if funcs[f].get('is_summary'):
            node['is_summary'] = True
        nodes.append(node)

    for caller in sorted(funcs.keys()):
        for callee in sorted(funcs[caller].get('calls', {}).keys()):
            reftype = funcs[caller]['calls'][callee]
            if not show_call_p(reftype, callee):
                continue
            style = 'solid' if reftype == 'call' else 'dotted'
            edges.append({'source': caller, 'target': callee, 'style': style})

    result = {
        'nodes': nodes,
        'edges': edges,
        'files': {fn: lst for fn, lst in funcs_by_file.items()},
        'meta': {
            'include_external': bool(args.include_external),
            'cluster_by_file': bool(args.cluster_by_file),
            'summarize_callers': args.summarize_callers,
            'ext': args.ext,
        }
    }

    if getattr(args, 'dot', False):
        # backward-compatible: emit DOT if requested
        print('digraph callgraph {')
        for n in nodes:
            attrs = n.get('attrs', {})
            attrs_str = ' '.join(f"{k}={dot_string(str(v))}" for k, v in sorted(attrs.items()))
            if attrs_str:
                print(f"{dot_string(n['id'])} [{attrs_str}];")
            else:
                print(f"{dot_string(n['id'])};")
        for e in edges:
            print(f"{dot_string(e['source'])} -> {dot_string(e['target'])} [style={e['style']}];")
        if args.cluster_by_file:
            for fn in sorted(funcs_by_file.keys()):
                print(f"subgraph {dot_string('cluster_' + fn)} {{")
                print(f"label={dot_string(fn)}")
                for func in funcs_by_file[fn]:
                    print(f"{dot_string(func)};")
                print('}')
        print('}')
    else:
        sys.stdout.write(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
