# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCode's toolsets for UE 5.8's ToolsetRegistry (card 0175). Imported only by Content/Python/init_unreal.py,
once it has found the registry: importing it anywhere else fails on 5.6 and 5.7."""

from toolset_registry.registration import Registration
from nodetocode.toolsets import n2c_toolset

_registration = Registration([
    n2c_toolset.NodeToCodeTools,
])
