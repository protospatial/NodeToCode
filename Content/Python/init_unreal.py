# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCode's editor startup script (card 0175).

On UE 5.8 or later with Epic's ToolsetRegistry plugin enabled, it registers the NodeToCodeTools toolset (Unreal MCP
and the in-editor assistant then list it) and its Python tests, as Epic's EditorToolset does. Anywhere else it does
nothing and logs nothing. It never raises: a failure is one warning, and the rest of the editor starts as usual.
"""

import unreal

_N2C_MIN_ENGINE = (5, 8)


def _n2c_toolset_registry_available():
    """True on UE 5.8+ when the ToolsetRegistry plugin is loaded (its classes and its Python package exist)."""
    try:
        version = tuple(int(p) for p in unreal.SystemLibrary.get_engine_version().split('-')[0].split('.')[:2])
    except Exception:
        return False
    if version < _N2C_MIN_ENGINE or not hasattr(unreal, 'ToolsetDefinition') or not hasattr(unreal, 'PythonTestRunner'):
        return False
    try:
        import toolset_registry  # noqa: F401
    except ImportError:
        return False
    return True


def _n2c_register_toolsets():
    from nodetocode import toolsets
    from nodetocode.toolsets import tests

    toolsets._registration.register()
    tests._test_runner = unreal.PythonTestRunner.create(
        'NodeToCode.Toolsets.PythonTests',
        unreal.PythonTestRunnerSearchOptions(root_module=tests.__name__))


if _n2c_toolset_registry_available():
    try:
        _n2c_register_toolsets()
    except Exception as e:  # never break editor startup
        unreal.log_warning(f'NodeToCode: NodeToCodeTools toolset not registered: {e}')
