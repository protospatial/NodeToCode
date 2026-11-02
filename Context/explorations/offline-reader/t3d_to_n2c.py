"""Spike for card 0329: Blueprint clipboard text (T3D, what Ctrl+C on graph nodes puts on the clipboard) to partial
N2C JSON 2.0.0, with no editor. An exploration, not a product: see Context/explorations/offline-reader.md.

Stdlib only; run from the repository root:

    python -I Context/explorations/offline-reader/t3d_to_n2c.py <clipboard.t3d> [--name BP_Name] [--guids]

It reads every "Begin Object Class=... Name=..." node block: its properties (FunctionReference, VariableReference,
EventReference, MacroGraphReference, NodeComment, EnabledState, ...) and its "CustomProperties Pin (...)" lines (pin
id, name, direction, PinType.*, DefaultValue, LinkedTo). Node types come from the plugin's own class table
(Source/Private/Utils/N2CNodeTypeRegistry.cpp, parsed at start) and pin types from the K2 category the same way
N2CStableNames.cpp maps them. --guids adds each node's and pin's GUID ("ids") so the result can be compared with
get-blueprint-n2cjson. What clipboard text does not hold (titles, purity, latency, member owners of self members,
struct and enum definitions, macro bodies) is listed under "warnings".
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))

PIN_TYPES = {  # K2 pin category -> N2C pin type (N2CStableNames.cpp)
    'exec': None, 'bool': 'Boolean', 'int': 'Integer', 'int64': 'Integer64', 'float': 'Float', 'double': 'Double',
    'real': 'Real', 'string': 'String', 'name': 'Name', 'text': 'Text', 'object': 'Object', 'class': 'Class',
    'interface': 'Interface', 'struct': 'Struct', 'enum': 'Enum', 'delegate': 'Delegate',
    'mcdelegate': 'MulticastDelegate', 'fieldpath': 'FieldPath', 'wildcard': 'Wildcard', 'softobject': 'SoftObject',
    'softclass': 'SoftClass',
}


def node_types():
    """{class name without K2Node_: N2C type}, from the plugin's registry source"""
    path = os.path.join(REPO, 'Source', 'Private', 'Utils', 'N2CNodeTypeRegistry.cpp')
    text = open(path, encoding='utf-8').read() if os.path.isfile(path) else ''
    return dict(re.findall(r'RegisterNodeType\(FName\(TEXT\("(\w+)"\)\), EN2CNodeType::(\w+)\)', text))


def split_top(text, sep=','):
    """Split on sep outside quotes and parentheses"""
    out, depth, quote, cur, i = [], 0, False, [], 0
    while i < len(text):
        c = text[i]
        if quote:
            cur.append(c)
            if c == '\\' and i + 1 < len(text):
                cur.append(text[i + 1])
                i += 1
            elif c == '"':
                quote = False
        elif c == '"':
            quote = True
            cur.append(c)
        elif c == '(':
            depth += 1
            cur.append(c)
        elif c == ')':
            depth -= 1
            cur.append(c)
        elif c == sep and depth == 0:
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    if ''.join(cur).strip():
        out.append(''.join(cur))
    return out


def unquote(v):
    v = v.strip()
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
        return bytes(v[1:-1], 'utf-8').decode('unicode_escape') if '\\' in v else v[1:-1]
    return v


def parse_struct(v):
    """'(A=1,B="x",C=(D=2))' -> dict, nested; anything else stays a string"""
    v = v.strip()
    if not (v.startswith('(') and v.endswith(')')):
        return unquote(v)
    out = {}
    for part in split_top(v[1:-1]):
        if '=' in part:
            k, _, val = part.partition('=')
            out[k.strip()] = parse_struct(val)
        elif part.strip():
            out.setdefault('_items', []).append(part.strip())
    return out


def object_name(path):
    """"/Script/CoreUObject.Class'/Script/Engine.Actor'" -> Actor; '/Game/X/S_Item.S_Item' -> S_Item"""
    if not path or path == 'None':
        return ''
    inner = re.search(r"'([^']*)'", path)
    p = inner.group(1) if inner else path
    return re.split(r'[./:]', p.rstrip('"'))[-1]


def parse_blocks(text):
    """Every node block: {'class', 'name', 'props': {k: raw}, 'pins': [dict]}; nested sub-objects are skipped"""
    nodes, stack = [], []
    for raw in text.splitlines():
        line = raw.strip()
        m = re.match(r'Begin Object(?: Class=(\S+))? Name="?([^"\s]+)"?', line)
        if m:
            stack.append({'class': object_name(m.group(1)) if m.group(1) else '', 'name': m.group(2), 'props': {}, 'pins': []})
            continue
        if line == 'End Object':
            block = stack.pop() if stack else None
            if block and not stack:
                nodes.append(block)
            continue
        if not stack or len(stack) > 1:
            continue
        block = stack[-1]
        if line.startswith('CustomProperties Pin '):
            block['pins'].append(parse_struct(line[len('CustomProperties Pin '):]))
        elif '=' in line:
            k, _, v = line.partition('=')
            block['props'][k.strip()] = v.strip()
    return nodes


def guid(hex32):
    h = (hex32 or '').strip()
    return '%s-%s-%s-%s-%s' % (h[0:8], h[8:12], h[12:16], h[16:20], h[20:32]) if len(h) == 32 else h


def convert(text, name='', with_guids=False):
    types = node_types()
    blocks = [b for b in parse_blocks(text) if b['class'].startswith(('K2Node_', 'EdGraphNode_Comment'))]
    warnings = []
    comments = [b for b in blocks if b['class'] == 'EdGraphNode_Comment']
    blocks = [b for b in blocks if b['class'] != 'EdGraphNode_Comment']
    pin_ids, nodes, by_name = {}, [], {}
    n_pin = 0
    for i, b in enumerate(blocks, 1):
        cls = b['class']
        short = cls[len('K2Node_'):]
        ntype = types.get(short, 'Unknown')
        node = {'id': 'N%d' % i, 'type': ntype, 'class': cls}
        if with_guids:
            node['ids'] = {'short': node['id'], 'guid': guid(b['props'].get('NodeGuid'))}
        ref = None
        for key in ('FunctionReference', 'VariableReference', 'EventReference', 'DelegateReference'):
            if key in b['props']:
                ref = parse_struct(b['props'][key])
                break
        if isinstance(ref, dict):
            if ref.get('MemberParent'):
                node['member_parent'] = object_name(ref['MemberParent'])
            elif ref.get('bSelfContext') == 'True':
                if name:
                    node['member_parent'] = name
                else:
                    warnings.append({'code': 'SELF_MEMBER', 'path': node['id'],
                                     'message': 'a member of the Blueprint itself: clipboard text does not name the '
                                                'Blueprint (pass --name)'})
            if ref.get('MemberName'):
                node['member_name'] = ref['MemberName']
        if 'CustomFunctionName' in b['props']:
            node['member_name'] = unquote(b['props']['CustomFunctionName'])
            if name:
                node['member_parent'] = name
        if 'MacroGraphReference' in b['props']:
            mg = parse_struct(b['props']['MacroGraphReference'])
            graph = mg.get('MacroGraph', '') if isinstance(mg, dict) else ''
            node['member_parent'] = object_name(graph.split(':')[0]) if ':' in graph else ''
            node['member_name'] = object_name(graph)
        if 'StructType' in b['props']:
            node['member_name'] = object_name(b['props']['StructType'])
            inner = re.search(r"'([^']*)'", b['props']['StructType'])
            if inner and not inner.group(1).startswith('/Script/'):
                node['member_parent'] = inner.group(1).rsplit('.', 1)[0]  # a user struct: its package path
        if 'TargetType' in b['props']:
            node['member_name'] = object_name(b['props']['TargetType'])
        if 'Enum' in b['props']:
            node['member_name'] = object_name(b['props']['Enum'])
        if b['props'].get('NodeComment'):
            node['comment'] = unquote(b['props']['NodeComment'])
        if b['props'].get('EnabledState') == 'Disabled':
            node['disabled'] = True
        node['input_pins'], node['output_pins'] = [], []
        for p in b['pins']:
            if p.get('bHidden') == 'True' or p.get('bOrphanedPin') == 'True':
                continue
            n_pin += 1
            pin = {'id': 'P%d' % n_pin, 'name': unquote(p.get('PinName', ''))}
            if with_guids:
                pin['ids'] = {'short': pin['id'], 'guid': guid(p.get('PinId')), 'name': pin['name']}
            friendly = re.findall(r'"((?:[^"\\]|\\.)*)"', p.get('PinFriendlyName', ''))
            if friendly and friendly[-1] and friendly[-1] != pin['name']:
                pin['label'] = friendly[-1]  # NSLOCTEXT(ns, key, "source") / INVTEXT("source"): the source string
            cat = unquote(p.get('PinType.PinCategory', ''))
            sub_obj = object_name(p.get('PinType.PinSubCategoryObject', ''))
            sub = unquote(p.get('PinType.PinSubCategory', ''))
            ptype = PIN_TYPES.get(cat, 'Wildcard')
            if cat == 'byte':
                ptype = 'Enum' if 'Enum' in p.get('PinType.PinSubCategoryObject', '') else 'Byte'
            if ptype:
                pin['type'] = ptype
                if sub_obj or (sub and sub != 'self'):
                    pin['sub_type'] = sub_obj or sub
            if 'DefaultValue' in p and unquote(p['DefaultValue']) != '':
                pin['default_value'] = unquote(p['DefaultValue'])
            elif 'DefaultObject' in p and p['DefaultObject'] not in ('None', ''):
                pin['default_value'] = object_name(p['DefaultObject'])
            links = p.get('LinkedTo')
            linked = links.get('_items', []) if isinstance(links, dict) else []
            if linked:
                pin['connected'] = True
            if p.get('PinType.bIsReference') == 'True':
                pin['is_reference'] = True
            if p.get('PinType.bIsConst') == 'True':
                pin['is_const'] = True
            container = p.get('PinType.ContainerType', 'None')
            if container == 'Array':
                pin['is_array'] = True
            elif container == 'Set':
                pin['is_set'] = True
            elif container == 'Map':
                pin['is_map'] = True
                vt = parse_struct(p.get('PinType.PinValueType', '()'))
                if isinstance(vt, dict):
                    pin['value_type'] = PIN_TYPES.get(unquote(vt.get('TerminalCategory', '')), 'Wildcard')
            out = p.get('Direction') == '"EGPD_Output"' or unquote(p.get('Direction', '')) == 'EGPD_Output'
            (node['output_pins'] if out else node['input_pins']).append(pin)
            pin_ids[(b['name'], (p.get('PinId') or '').strip())] = (node['id'], pin['id'], out, ptype is None, linked)
        # "pure" is defined by the pins: a node with no exec pin (events and entries always have one)
        if all('type' in pin for pin in node['input_pins'] + node['output_pins']) and \
                (node['input_pins'] or node['output_pins']):
            node['pure'] = True
        by_name[b['name']] = node
        nodes.append(node)

    execution, data = [], {}
    for (owner, _), (nid, pid, out, is_exec, linked) in pin_ids.items():
        if not out:
            continue
        for item in linked:
            target_node, _, target_pin = item.partition(' ')
            hit = pin_ids.get((target_node, target_pin.strip()))
            if not hit:
                warnings.append({'code': 'LINK_OUTSIDE', 'path': '%s.%s' % (nid, pid),
                                 'message': 'wired to %s, which is not in the copied text' % target_node})
                continue
            if is_exec:
                execution.append('%s.%s->%s.%s' % (nid, pid, hit[0], hit[1]))
            else:
                key = '%s.%s' % (nid, pid)
                target = '%s.%s' % (hit[0], hit[1])
                data[key] = target if key not in data else (data[key] if isinstance(data[key], list) else [data[key]]) + [target]
    graph = {'name': 'Pasted', 'graph_type': 'EventGraph', 'nodes': nodes,
             'flows': {'execution': sorted(execution, key=lambda s: [int(x) for x in re.findall(r'\d+', s)]), 'data': data}}
    if comments:
        graph['comments'] = [{'text': unquote(c['props'].get('NodeComment', ''))} for c in comments]
    warnings.append({'code': 'NOT_IN_CLIPBOARD', 'path': '',
                     'message': 'no node titles, pin labels, pure/latent flags, unused/unreachable analysis, graph name '
                                'and type, Blueprint name, member variables, struct or enum definitions, macro or '
                                'collapsed graph bodies, timeline templates'})
    return {'version': '2.0.0', 'metadata': {'name': name, 'blueprint_type': '', 'blueprint_class': name},
            'graphs': [graph], 'structs': [], 'enums': [], 'warnings': warnings}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('t3d')
    ap.add_argument('--name', default='')
    ap.add_argument('--guids', action='store_true', help='add node and pin GUIDs ("ids") for comparison')
    args = ap.parse_args()
    text = open(args.t3d, encoding='utf-8-sig', errors='replace').read()
    json.dump(convert(text, args.name, args.guids), sys.stdout, indent=1)
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
