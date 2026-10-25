# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.load_blueprint on plain CPython with the `unreal` stub from conftest.py (card 0143).

UBlueprint::ParentClass is not exposed to Python: in a UE 5.8 editor `blueprint.parent_class` raises AttributeError
and get_editor_property('parent_class') raises "Failed to find property". load_blueprint read `blueprint.parent_class`,
so its documented example (load_blueprint(path) then data['name']) always returned success False. It now asks
BlueprintEditorLibrary.get_blueprint_parent_class (UE 5.8) and otherwise reads the asset's ParentClass registry tag
(UE 5.6 and 5.7 have no such function). The stand-ins below mimic what the editor returned for an Actor Blueprint.
"""

import importlib

import pytest

from conftest import UNREAL

PATH = '/Game/N2CTest/BP_Player'


class _Named:
    def __init__(self, name):
        self._name = name

    def get_name(self):
        return self._name


class _Blueprint(_Named):
    """Like unreal.Blueprint in the editor: no parent_class attribute"""

    def get_path_name(self):
        return PATH + '.BP_Player'

    def get_class(self):
        return _Named('Blueprint')


class _AssetData:
    def __init__(self, tags):
        self._tags = tags

    def get_tag_value(self, name):
        return self._tags.get(name)


@pytest.fixture
def blueprint_module(monkeypatch):
    monkeypatch.setattr(UNREAL, 'Blueprint', _Blueprint, raising=False)
    monkeypatch.setattr(UNREAL, 'load_asset', lambda path: _Blueprint('BP_Player') if path == PATH else None,
                        raising=False)
    return importlib.import_module('nodetocode.blueprint')


def test_load_blueprint_reads_the_parent_class_through_the_5_8_library(blueprint_module, monkeypatch):
    class BlueprintEditorLibrary:
        @staticmethod
        def get_blueprint_parent_class(blueprint):
            return _Named('Actor')

    monkeypatch.setattr(UNREAL, 'BlueprintEditorLibrary', BlueprintEditorLibrary, raising=False)
    result = blueprint_module.load_blueprint(PATH)
    assert result == {'success': True, 'error': None, 'data': {
        'name': 'BP_Player', 'path': PATH + '.BP_Player', 'class_name': 'Blueprint', 'parent_class': 'Actor'}}


@pytest.mark.parametrize('tag, expected', [
    ("/Script/CoreUObject.Class'/Script/Engine.Actor'", 'Actor'),
    ('/Script/UMG.UserWidget', 'UserWidget'),
    (None, ''),
])
def test_load_blueprint_falls_back_to_the_parent_class_tag(blueprint_module, monkeypatch, tag, expected):
    class BlueprintEditorLibrary:  # UE 5.6 and 5.7: no get_blueprint_parent_class
        pass

    class EditorAssetLibrary:
        @staticmethod
        def find_asset_data(path):
            assert path == PATH + '.BP_Player'
            return _AssetData({'ParentClass': tag} if tag else {})

    monkeypatch.setattr(UNREAL, 'BlueprintEditorLibrary', BlueprintEditorLibrary, raising=False)
    monkeypatch.setattr(UNREAL, 'EditorAssetLibrary', EditorAssetLibrary, raising=False)
    result = blueprint_module.load_blueprint(PATH)
    assert result['success'] is True, result
    assert result['data']['parent_class'] == expected


def test_load_blueprint_reports_a_missing_asset(blueprint_module):
    result = blueprint_module.load_blueprint('/Game/N2CTest/Nope')
    assert result == {'success': False, 'data': None, 'error': 'Blueprint not found at path: /Game/N2CTest/Nope'}
