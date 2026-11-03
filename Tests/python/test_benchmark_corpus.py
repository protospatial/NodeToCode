# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The benchmark corpus manifest (card 0364): Benchmarks/corpus/manifest.json validates against manifest.schema.json,
every shape file it names exists and every shape file is named, every corpus asset is committed, and no external
(EULA) sample asset is. The Blueprints themselves are checked in the editor by NodeToCode.Benchmark.Corpus.

The schema check is a small validator for the keywords manifest.schema.json uses (type, required, properties,
additionalProperties, items, minItems, enum, pattern), so it needs nothing outside the standard library.
"""
import json
import os
import re

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
CORPUS = os.path.join(REPO, 'Benchmarks', 'corpus')
CONTENT = os.path.join(REPO, 'Tests', 'NodeToCodeTestContent', 'Content')

TYPES = {'object': dict, 'array': list, 'string': str, 'integer': int, 'boolean': bool, 'null': type(None)}


def load(name):
    with open(os.path.join(CORPUS, name), encoding='utf-8') as f:
        return json.load(f)


def validate(value, schema, path='$'):
    """The errors of value against schema, as 'path: message' strings"""
    errors = []
    types = schema.get('type')
    if types is not None:
        names = types if isinstance(types, list) else [types]
        ok = any(isinstance(value, TYPES[name]) and not (name == 'integer' and isinstance(value, bool)) for name in names)
        if not ok:
            return ['%s: expected %s, got %s' % (path, '/'.join(names), type(value).__name__)]
    if 'enum' in schema and value not in schema['enum']:
        errors.append('%s: %r is not one of %r' % (path, value, schema['enum']))
    if isinstance(value, str) and 'pattern' in schema and not re.search(schema['pattern'], value):
        errors.append('%s: %r does not match %s' % (path, value, schema['pattern']))
    if isinstance(value, dict):
        for key in schema.get('required', []):
            if key not in value:
                errors.append('%s: missing %s' % (path, key))
        properties = schema.get('properties', {})
        for key, item in value.items():
            if key in properties:
                errors += validate(item, properties[key], '%s.%s' % (path, key))
            elif schema.get('additionalProperties') is False:
                errors.append('%s: unexpected %s' % (path, key))
    if isinstance(value, list):
        if len(value) < schema.get('minItems', 0):
            errors.append('%s: fewer than %d items' % (path, schema['minItems']))
        if 'items' in schema:
            for index, item in enumerate(value):
                errors += validate(item, schema['items'], '%s[%d]' % (path, index))
    return errors


def test_the_manifest_validates_against_its_schema():
    errors = validate(load('manifest.json'), load('manifest.schema.json'))
    assert not errors, '\n'.join(errors)


def test_the_validator_catches_a_broken_manifest():
    schema = load('manifest.schema.json')
    broken = load('manifest.json')
    broken['entries'][0]['source'] = 'somewhere'
    del broken['entries'][0]['graphs']
    broken['extra'] = 1
    errors = validate(broken, schema)
    assert any('source' in e for e in errors) and any('missing graphs' in e for e in errors) and any('unexpected extra' in e for e in errors)


def test_every_shape_file_is_named_once_and_exists():
    manifest = load('manifest.json')
    named = [entry['shape'] for entry in manifest['entries'] if entry['shape']]
    assert len(named) == len(set(named))
    on_disk = sorted('shapes/' + name for name in os.listdir(os.path.join(CORPUS, 'shapes')) if name.endswith('.json'))
    assert sorted(named) == on_disk


def test_every_shape_file_describes_its_entry():
    for entry in load('manifest.json')['entries']:
        if not entry['shape']:
            continue
        shape = load(entry['shape'])
        assert shape['name'] == entry['id']
        assert entry['asset_path'].endswith('/%s.%s' % (entry['id'], entry['id']))
        for key in ('cpp_class', 'cpp_parent', 'parent', 'properties', 'functions', 'events', 'calls'):
            assert key in shape, '%s has no %s' % (entry['shape'], key)


def test_corpus_and_perf_assets_are_committed_and_samples_are_not():
    manifest = load('manifest.json')
    for entry in manifest['entries']:
        package = entry['asset_path'].split('.')[0]
        assert package.startswith('/NodeToCodeTestContent/'), entry['asset_path']
        path = os.path.join(CONTENT, *package[len('/NodeToCodeTestContent/'):].split('/')) + '.uasset'
        assert os.path.isfile(path), path
    assert {sample['name'] for sample in manifest['external']} == {'Lyra', 'Cropout'}
    for sample in manifest['external']:
        assert not os.path.isdir(os.path.join(CONTENT, sample['name'])), 'EULA sample content must not be committed'


def test_the_corpus_covers_the_shapes_the_card_names():
    covers = ' '.join(entry['covers'] for entry in load('manifest.json')['entries'] if entry['source'] == 'corpus').lower()
    for shape in ('component', 'beginplay', 'tick', 'pure', 'local variable', 'foreachloop', 'whileloop', 'switch',
                  'delay', 'timeline', 'dispatcher', 'interface', 'cast', 'struct', 'macro', 'collapsed',
                  'enhanced input', 'widget', 'child'):
        assert shape in covers, shape
    corpus = [entry for entry in load('manifest.json')['entries'] if entry['source'] == 'corpus']
    assert 20 <= len(corpus) <= 30
