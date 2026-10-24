# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""Headless load check run by build.ps1 -Smoke inside the editor (-ExecutePythonScript).

Logs N2C_SMOKE PASS when the plugin's reflected N2C* types reach Python, which means the NodeToCode modules loaded.
build.ps1 looks for that line in the editor log.
"""
import unreal

classes = [c for c in dir(unreal) if c.startswith("N2C")]
unreal.log("N2C_SMOKE classes=%d sample=%s" % (len(classes), ",".join(sorted(classes)[:8])))
unreal.log("N2C_SMOKE " + ("PASS" if classes else "FAIL"))
