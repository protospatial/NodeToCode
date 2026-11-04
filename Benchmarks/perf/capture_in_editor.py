# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Runs inside a headless editor (run_perf.py starts it with -ExecutePythonScript and -trace=cpu,memory). Reads the run's
config from the file N2C_PERF_CONFIG names, measures, writes <out_dir>/editor_results.json and the captured documents,
and quits the editor.

Per perf fixture (card 0359), through nodetocode.get_blueprint_n2cjson (the whole Blueprint, the path the MCP
get-blueprint-n2cjson tool and the Python bridge share): one warm-up, then RUNS timed reads; the process working set
and peak working set before and after; the agent document (ids {short, guid} and the guids table) and the translation
form of it (ids.short as "id", no guids table: what a translation prompt carries), each minified and pretty. Then
BP_N2CPerf_5k's F_Chain_0 at Max Translation Depth 0-5, and the tag search with every function graph of the
fixtures tagged (tags removed again afterwards).
"""
import ctypes
import ctypes.wintypes
import json
import os
import statistics
import time
import traceback

import unreal
import nodetocode

RUNS = 5
TAG = 'N2CPerfBench'
CATEGORY = 'N2CPerfBench'


class _Counters(ctypes.Structure):
    _fields_ = [('cb', ctypes.wintypes.DWORD), ('PageFaultCount', ctypes.wintypes.DWORD),
                ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
                ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t)]


def memory():
    """(working set, peak working set) of this process in bytes, or (None, None) off Windows"""
    try:
        counters = _Counters()
        counters.cb = ctypes.sizeof(_Counters)
        kernel32 = ctypes.windll.kernel32
        kernel32.GetCurrentProcess.restype = ctypes.wintypes.HANDLE
        kernel32.K32GetProcessMemoryInfo.argtypes = [ctypes.wintypes.HANDLE, ctypes.POINTER(_Counters), ctypes.wintypes.DWORD]
        kernel32.K32GetProcessMemoryInfo.restype = ctypes.wintypes.BOOL
        if kernel32.K32GetProcessMemoryInfo(kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            return counters.WorkingSetSize, counters.PeakWorkingSetSize
    except Exception:
        pass
    return None, None


def translation_form(document):
    """The agent document as a translation prompt carries it: "ids" {short, guid} back to "id", no "guids" table"""
    def walk(value):
        if isinstance(value, dict):
            out = {}
            for key, item in value.items():
                if key == 'guids':
                    continue
                if key == 'ids' and isinstance(item, dict) and 'short' in item:
                    out['id'] = item['short']
                    continue
                out[key] = walk(item)
            return out
        if isinstance(value, list):
            return [walk(item) for item in value]
        return value
    return walk(document)


def node_count(document):
    return sum(len(graph.get('nodes', [])) for graph in document.get('graphs', []))


def timed_reads(path, graph_name='', depth=-1):
    """(the document, [seconds per run], memory before, memory after) for RUNS reads after one warm-up"""
    def read():
        result = nodetocode.get_blueprint_n2cjson(path, graph_name=graph_name, depth=depth)
        if not result.get('success'):
            raise RuntimeError('%s: %s' % (path, result.get('error')))
        return result['data']['n2c_json']
    read()
    before = memory()
    times = []
    document = None
    for _ in range(RUNS):
        start = time.perf_counter()
        document = read()
        times.append(time.perf_counter() - start)
    return document, times, before, memory()


def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def main():
    config = json.load(open(os.environ['N2C_PERF_CONFIG'], encoding='utf-8'))
    out = config['out_dir']
    results = {'engine': unreal.SystemLibrary.get_engine_version(), 'runs': RUNS, 'fixtures': [], 'depth': [], 'tags': None,
               'errors': []}
    for fixture in config['fixtures']:
        try:
            document, times, before, after = timed_reads(fixture['path'])
            translation = translation_form(document)
            texts = {
                'agent_min': json.dumps(document, separators=(',', ':'), ensure_ascii=False),
                'agent_pretty': json.dumps(document, indent=2, ensure_ascii=False),
                'translation_min': json.dumps(translation, separators=(',', ':'), ensure_ascii=False),
                'translation_pretty': json.dumps(translation, indent=2, ensure_ascii=False),
            }
            for kind, text in texts.items():
                write(os.path.join(out, '%s.%s.json' % (fixture['name'], kind)), text)
            results['fixtures'].append({
                'name': fixture['name'], 'nodes': node_count(document), 'graphs': len(document.get('graphs', [])),
                'read_ms': [round(t * 1000.0, 3) for t in times], 'read_ms_median': round(statistics.median(times) * 1000.0, 3),
                'bytes': {kind: len(text.encode('utf-8')) for kind, text in texts.items()},
                'working_set_delta': (after[0] - before[0]) if before[0] is not None else None,
                'peak_working_set_delta': (after[1] - before[1]) if before[1] is not None else None,
            })
        except Exception:
            results['errors'].append(traceback.format_exc())

    # Max Translation Depth 0-5 on the 5k fixture's call chain
    chain = config.get('chain')
    for depth in range(0, 6) if chain else []:
        try:
            document, times, _, _ = timed_reads(chain['path'], graph_name=chain['graph'], depth=depth)
            text = json.dumps(translation_form(document), separators=(',', ':'), ensure_ascii=False)
            write(os.path.join(out, 'depth%d.translation_min.json' % depth), text)
            results['depth'].append({'depth': depth, 'graphs': len(document.get('graphs', [])), 'nodes': node_count(document),
                                     'read_ms_median': round(statistics.median(times) * 1000.0, 3),
                                     'translation_min_bytes': len(text.encode('utf-8'))})
        except Exception:
            results['errors'].append(traceback.format_exc())

    # The tag search with every function graph of the fixtures tagged, then the tags removed again
    library = unreal.N2CTagBlueprintLibrary
    added = []
    try:
        for fixture in config['fixtures']:
            blueprint = unreal.load_asset(fixture['path'])
            listed = nodetocode.list_blueprint_graphs(fixture['path'])
            for graph in (listed.get('data') or {}).get('graphs', []):
                if graph.get('kind') != 'function':
                    continue
                info, ok, error = library.tag_blueprint_graph(blueprint, graph['name'], TAG, CATEGORY, '')
                if ok:
                    added.append((str(info.graph_guid), fixture['path']))
                else:
                    results['errors'].append('tag %s %s: %s' % (fixture['name'], graph['name'], error))
        library.search_tags(TAG, True)
        times = []
        found = 0
        for _ in range(RUNS):
            start = time.perf_counter()
            found = len(library.search_tags(TAG, True))
            times.append(time.perf_counter() - start)
        all_times = []
        for _ in range(RUNS):
            start = time.perf_counter()
            library.get_all_tags()
            all_times.append(time.perf_counter() - start)
        results['tags'] = {'tagged_graphs': len(added), 'found': found,
                           'search_ms_median': round(statistics.median(times) * 1000.0, 3),
                           'get_all_tags_ms_median': round(statistics.median(all_times) * 1000.0, 3)}
    except Exception:
        results['errors'].append(traceback.format_exc())
    finally:
        for guid, path in added:
            try:
                library.remove_tag(guid, TAG, CATEGORY, path)
            except Exception:
                results['errors'].append(traceback.format_exc())

    write(os.path.join(out, 'editor_results.json'), json.dumps(results, indent=2))


try:
    main()
finally:
    unreal.SystemLibrary.quit_editor()
