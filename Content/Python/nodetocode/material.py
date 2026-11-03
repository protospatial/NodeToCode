"""
Material reads for NodeToCode (card 0453, decision 0009).

get_material_n2cjson runs the MCP get-material-n2cjson tool through invoke_tool, so Python gets the same document an
agent does: a Material or Material Function as N2C JSON, read from the saved asset.
"""

from typing import Dict, Any

from .utils import (
    make_error_result,
    log_error,
    _invoke,
)


def get_material_n2cjson(material_path: str, depth: int = -1, compact: bool = True, to_file: bool = False) -> Dict[str, Any]:
    """
    Read a Material or Material Function as N2C JSON by asset path, from the saved asset (unsaved edits in an open
    Material Editor are not included), as the MCP get-material-n2cjson tool returns it.

    Args:
        material_path: Asset path, e.g. '/Engine/BasicShapes/BasicShapeMaterial'
        depth: How deep material function calls are expanded (0-5); -1 uses the project's Max Translation Depth.
            Engine functions (/Engine/Functions) are only named at depth 0
        compact: Write the JSON without whitespace
        to_file: Write the document under Saved/NodeToCode/Exports and return its path instead

    Returns:
        dict: {
            'success': bool,
            'data': the N2C JSON document with 'target' {'assetPath', 'kind' (material | material-function),
                    'resolvedBy': 'path', 'workingCopy': False}; or, written to a file (to_file, or over about
                    20000 tokens): {'path', 'bytes', 'estimatedTokens', 'reason', 'nodeCount', 'target'},
            'error': str or None   # starts with a code: ASSET_NOT_FOUND, UNSUPPORTED_ASSET_TYPE, INVALID_ARGUMENT, ...
        }

    Example:
        doc = get_material_n2cjson('/Engine/EngineMaterials/WorldGridMaterial')['data']
        print(len(doc['graphs'][0]['nodes']))
    """
    try:
        if not material_path or not str(material_path).strip():
            return make_error_result('INVALID_ARGUMENT: material_path is required')
        arguments = {'materialPath': str(material_path).strip(), 'compact': bool(compact), 'toFile': bool(to_file)}
        if depth is not None and int(depth) >= 0:
            arguments['depth'] = int(depth)
        return _invoke('get_material_n2cjson', arguments)
    except Exception as e:
        log_error(f"get_material_n2cjson failed: {e}")
        return make_error_result(str(e))
