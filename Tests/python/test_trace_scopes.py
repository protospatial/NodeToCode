# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The Unreal Insights CPU scopes (card 0358) keep their names and their functions. Scripts and the performance cards
read the N2C_* timers by name, and Context/development-workflow/AGENTS-development-workflow.md lists them, so a renamed
or moved scope must fail here rather than silently drop out of a trace.
"""

import os
import re

import pytest

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCE = os.path.join(REPO, 'Source', 'Private')

# (file, function definition that must hold the scope, scope name)
SCOPES = [
    ('Core/N2CNodeCollector.cpp', 'bool FN2CNodeCollector::CollectNodesFromGraph(', 'N2C_Capture_CollectNodes'),
    ('Core/N2CCaptureContext.cpp', 'bool FN2CCaptureContext::CaptureNodes(', 'N2C_Capture_BuildStruct'),
    ('Core/N2CCaptureContext.cpp', 'bool FN2CCaptureContext::CaptureGraphs(', 'N2C_Capture_BuildStruct'),
    ('Core/N2CSerializer.cpp', 'FString FN2CSerializer::ToJson(const FN2CBlueprint&', 'N2C_Serialize_ToJson'),
    ('Core/N2CSerializer.cpp', 'FString FN2CSerializer::ToJson(const FN2CCapture&', 'N2C_Serialize_ToJson'),
    ('Utils/Validators/N2CBlueprintValidator.cpp', 'bool FN2CBlueprintValidator::Validate(const FN2CBlueprint& Blueprint, TArray<FN2CDiagnostic>&', 'N2C_Validate'),
    ('Core/Services/N2CTokenEstimationService.cpp', 'FN2CTokenEstimate FN2CTokenEstimationService::GetEstimate(', 'N2C_TokenEstimate'),
    ('Core/Services/N2CTokenEstimationService.cpp', 'FString FN2CTokenEstimationService::SerializeGraphToJson(', 'N2C_TokenEstimate_Serialize'),
    ('MCP/Tools/N2CToolCatalog.cpp', 'FN2CToolOutcome FN2CToolCatalog::InvokeNow(', 'N2C_Mcp_Execute'),
    ('MCP/Tools/N2CMcpToolBase.cpp', 'FMcpToolCallResult RunAsToolCall(', 'N2C_Mcp_GameThreadWork'),
]


def body_start(text, signature):
    """The first lines of the function's body: from its opening brace, a few statements in"""
    at = text.find(signature)
    assert at >= 0, 'missing ' + signature
    brace = text.find('{', at)
    return text[brace:brace + 400]


@pytest.mark.parametrize('path, signature, scope', SCOPES, ids=[s[2] + ':' + s[1].split('(')[0].split(' ')[-1] for s in SCOPES])
def test_each_scope_opens_its_function(path, signature, scope):
    with open(os.path.join(SOURCE, path), encoding='utf-8') as f:
        text = f.read()
    assert '#include "ProfilingDebugging/CpuProfilerTrace.h"' in text
    assert re.search(r'TRACE_CPUPROFILER_EVENT_SCOPE\(%s\);' % re.escape(scope), body_start(text, signature)), \
        '%s does not open with TRACE_CPUPROFILER_EVENT_SCOPE(%s)' % (signature, scope)


def test_the_tool_scope_also_names_the_tool():
    with open(os.path.join(SOURCE, 'MCP/Tools/N2CToolCatalog.cpp'), encoding='utf-8') as f:
        text = f.read()
    assert 'TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Name);' in body_start(text, 'FN2CToolOutcome FN2CToolCatalog::InvokeNow(')


def test_the_profiling_section_lists_every_scope():
    with open(os.path.join(REPO, 'Context', 'development-workflow', 'AGENTS-development-workflow.md'), encoding='utf-8') as f:
        text = f.read()
    assert '## Profiling (Unreal Insights)' in text
    for scope in sorted({s[2] for s in SCOPES}):
        assert '`%s`' % scope in text, scope + ' is not documented'
