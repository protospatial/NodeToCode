"""
Script management for NodeToCode Python environment.

Provides CRUD operations for Python scripts with dual-path search:
- Plugin bundled scripts: <plugin folder>/Content/Python/scripts/ (read-only), found beside this package
- Project user scripts: Content/Python/scripts/ (read-write)

Scripts are indexed in script_registry.json for fast search and discovery.
All list/search/get operations search BOTH locations, with project scripts
taking precedence over plugin scripts with the same name.
Save/delete operations only affect project scripts.

Usage:
    import nodetocode as n2c

    # List available scripts (includes both plugin and project scripts)
    scripts = n2c.list_scripts()

    # Search for scripts
    matches = n2c.search_scripts("health system")

    # Get script code and metadata
    script = n2c.get_script("create_health_system")
    # script["source"] will be "plugin" or "project"

    # Execute a saved script
    result = n2c.run_script("create_health_system", initial_health=100.0)

    # Save a new script (always saves to project folder)
    n2c.save_script(
        "my_script",
        "print('Hello!')",
        "A simple hello script",
        tags=["example", "hello"]
    )

    # Delete a script (only project scripts can be deleted)
    n2c.delete_script("my_script")
"""

import os
import json
import re
import shutil
from datetime import datetime
from typing import Dict, Any, Optional, List

import unreal

from .utils import make_success_result, make_error_result, log_info, log_warning, log_error


# Script storage paths
def _get_project_scripts_dir() -> str:
    """Get the project-level scripts directory (user scripts)."""
    return os.path.join(unreal.Paths.project_content_dir(), "Python", "scripts")


def _get_plugin_scripts_dir() -> str:
    """
    Get the plugin's bundled scripts directory: Content/Python/scripts beside this package.

    Resolved from this file rather than from the project's Plugins folder, so it holds wherever the plugin is
    installed: <Project>/Plugins/<any folder name>, Engine/Plugins/Marketplace/<folder> (Fab and Launcher installs)
    or any other plugin search path.
    """
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "scripts"))


def _get_scripts_dir() -> str:
    """Get the primary scripts storage directory (project-level for user scripts)."""
    return _get_project_scripts_dir()


def _get_registry_path() -> str:
    """Get the project script registry JSON file path."""
    return os.path.join(_get_project_scripts_dir(), "script_registry.json")


def _get_plugin_registry_path() -> str:
    """Get the plugin's bundled script registry JSON file path."""
    return os.path.join(_get_plugin_scripts_dir(), "script_registry.json")


def _ensure_scripts_dir() -> bool:
    """
    Ensure the scripts directory structure exists.

    Returns:
        True if directory exists or was created, False on error
    """
    scripts_dir = _get_scripts_dir()
    try:
        if not os.path.exists(scripts_dir):
            os.makedirs(scripts_dir)
            log_info(f"Created scripts directory: {scripts_dir}")

        # Ensure default category exists
        general_dir = os.path.join(scripts_dir, "general")
        if not os.path.exists(general_dir):
            os.makedirs(general_dir)

        return True
    except Exception as e:
        log_error(f"Failed to create scripts directory: {e}")
        return False


class RegistryUnreadableError(Exception):
    """The project script registry exists but cannot be read or parsed, so nothing may be written over it."""


def _default_registry() -> Dict[str, Any]:
    """The registry used when no registry file exists yet."""
    return {
        "version": "1.0",
        "scripts": {},
        "categories": ["general"],
        "stats": {
            "total_scripts": 0,
            "last_updated": datetime.utcnow().isoformat() + "Z"
        }
    }


def _parse_registry(text: str) -> Dict[str, Any]:
    """
    Parse registry JSON and check its shape.

    Raises:
        ValueError: when the text is not JSON or not a registry object
    """
    registry = json.loads(text)
    if not isinstance(registry, dict):
        raise ValueError("the top level is not a JSON object")
    scripts = registry.setdefault("scripts", {})
    if not isinstance(scripts, dict) or not all(isinstance(info, dict) for info in scripts.values()):
        raise ValueError("'scripts' is not an object of script entries")
    if not isinstance(registry.get("categories", []), list):
        raise ValueError("'categories' is not a list")
    if not isinstance(registry.get("stats", {}), dict):
        raise ValueError("'stats' is not an object")
    return registry


def _copy_corrupt_registry(registry_path: str) -> Optional[str]:
    """
    Copy a registry that does not parse to <file>.corrupt-<UTC yyyymmdd-hhmmss>, the name the plugin's other state
    files use. The bad file itself stays in place. A copy with the same bytes already there is reused, so repeated
    calls do not pile up copies.

    Returns:
        The copy's path, or None when the copy failed
    """
    try:
        with open(registry_path, 'rb') as f:
            content = f.read()
        folder, base = os.path.split(registry_path)
        prefix = base + ".corrupt-"
        for entry in sorted(os.listdir(folder)):
            if entry.startswith(prefix):
                existing = os.path.join(folder, entry)
                try:
                    with open(existing, 'rb') as f:
                        if f.read() == content:
                            return existing
                except OSError:
                    continue
        stamp = datetime.utcnow().strftime("%Y%m%d-%H%M%S")
        candidate = f"{registry_path}.corrupt-{stamp}"
        suffix = 1
        while os.path.exists(candidate):
            candidate = f"{registry_path}.corrupt-{stamp}-{suffix}"
            suffix += 1
        shutil.copyfile(registry_path, candidate)
        return candidate
    except OSError as e:
        log_error(f"Could not copy the unreadable script registry {registry_path}: {e}")
        return None


def _finish_interrupted_write(registry_path: str) -> None:
    """
    Finish a save that stopped after <file>.tmp was complete but before it replaced <file>: when the registry is
    missing and a temporary file that parses is beside it, move that file into place.
    """
    temp_path = registry_path + ".tmp"
    if os.path.exists(registry_path) or not os.path.exists(temp_path):
        return
    try:
        with open(temp_path, 'r', encoding='utf-8') as f:
            _parse_registry(f.read())
        os.replace(temp_path, registry_path)
        log_warning(f"Finished an interrupted save of {registry_path} from {temp_path}")
    except (OSError, ValueError) as e:
        log_warning(f"Left the incomplete temporary registry {temp_path} as it is: {e}")


def _load_single_registry(registry_path: str, writable: bool = False) -> Dict[str, Any]:
    """
    Load a single script registry from JSON.

    Args:
        registry_path: Path to the registry JSON file
        writable: True for the project registry, which this module saves. A project registry that cannot be read
                  or parsed raises instead of loading as empty, so no save can write over it; one that does not
                  parse is also copied to <file>.corrupt-<timestamp>. The plugin's bundled registry is read-only:
                  a bad one is logged and treated as empty.

    Returns:
        Registry dictionary or empty default structure

    Raises:
        RegistryUnreadableError: writable is True and the file exists but cannot be read or parsed
    """
    if writable:
        _finish_interrupted_write(registry_path)

    if not os.path.exists(registry_path):
        return _default_registry()

    try:
        with open(registry_path, 'r', encoding='utf-8') as f:
            text = f.read()
    except OSError as e:
        message = (f"Could not read the script registry {registry_path} ({e}). It was not changed, and scripts "
                   f"cannot be saved or deleted until it can be read.")
        log_error(message)
        if writable:
            raise RegistryUnreadableError(message) from e
        return _default_registry()

    try:
        return _parse_registry(text)
    except ValueError as e:  # json.JSONDecodeError and UnicodeDecodeError are ValueErrors too
        if not writable:
            log_error(f"The bundled script registry {registry_path} does not parse ({e}); its scripts are not listed.")
            return _default_registry()
        copy_path = _copy_corrupt_registry(registry_path)
        kept = f" A copy was kept as {copy_path}." if copy_path else ""
        message = (f"The script registry {registry_path} does not parse ({e}). It was left as it is and nothing is "
                   f"saved to it until it is repaired or removed.{kept} Fix the JSON, or delete the file to start "
                   f"a new registry (the .py files in the scripts folder are not affected).")
        log_error(message)
        raise RegistryUnreadableError(message) from e


def _load_registry() -> Dict[str, Any]:
    """
    Load and merge script registries from both plugin and project directories.

    Plugin scripts are loaded first, then project scripts are merged on top.
    Project scripts with the same name will override plugin scripts.

    Returns:
        Merged registry dictionary
    """
    # Load plugin's bundled scripts first
    plugin_registry = _load_single_registry(_get_plugin_registry_path())

    # Mark plugin scripts with their source
    for name, info in plugin_registry.get("scripts", {}).items():
        info["_source"] = "plugin"
        # Store the base directory for resolving paths
        info["_base_dir"] = _get_plugin_scripts_dir()

    # Load project's user scripts (raises RegistryUnreadableError when the file exists but does not load)
    project_registry = _load_single_registry(_get_registry_path(), writable=True)

    # Mark project scripts with their source
    for name, info in project_registry.get("scripts", {}).items():
        info["_source"] = "project"
        info["_base_dir"] = _get_project_scripts_dir()

    # Merge: project scripts override plugin scripts with same name
    merged_scripts = {}
    merged_scripts.update(plugin_registry.get("scripts", {}))
    merged_scripts.update(project_registry.get("scripts", {}))

    # Merge categories
    merged_categories = list(set(
        plugin_registry.get("categories", ["general"]) +
        project_registry.get("categories", ["general"])
    ))

    return {
        "version": "1.0",
        "scripts": merged_scripts,
        "categories": sorted(merged_categories),
        "stats": {
            "total_scripts": len(merged_scripts),
            "last_updated": datetime.utcnow().isoformat() + "Z"
        }
    }


def _load_project_registry() -> Dict[str, Any]:
    """
    Load only the project's script registry (not merged with plugin).

    Use this for save/delete operations that should only affect project scripts.

    Returns:
        Project registry dictionary

    Raises:
        RegistryUnreadableError: the file exists but cannot be read or parsed
    """
    return _load_single_registry(_get_registry_path(), writable=True)


def _write_registry_file(registry_path: str, text: str) -> None:
    """
    Write the registry through <file>.tmp and os.replace, keeping the previous file as <file>.bak, so a crash leaves
    either the old registry or the new one, never a truncated one (the plugin's other state files use the same
    pattern).
    """
    temp_path = registry_path + ".tmp"
    try:
        with open(temp_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        if os.path.exists(registry_path):
            try:
                shutil.copyfile(registry_path, registry_path + ".bak")
            except OSError as e:
                log_warning(f"Could not refresh the backup {registry_path}.bak: {e}")
        os.replace(temp_path, registry_path)
    except BaseException:
        try:
            os.remove(temp_path)
        except OSError:
            pass
        raise


def _save_registry(registry: Dict[str, Any]) -> bool:
    """
    Save the script registry to JSON (project registry only).

    Refuses (returns False) when the registry file on disk cannot be read or parsed, so a registry that loaded as
    empty can never replace one with entries.

    Args:
        registry: Registry dictionary to save

    Returns:
        True on success, False on error
    """
    if not _ensure_scripts_dir():
        return False

    registry_path = _get_registry_path()
    try:
        _load_single_registry(registry_path, writable=True)
    except RegistryUnreadableError:
        return False

    # Remove internal metadata fields before saving; unknown top-level fields are kept
    clean_registry = {k: v for k, v in registry.items() if not k.startswith("_")}
    clean_registry.update({
        "version": registry.get("version", "1.0"),
        "scripts": {},
        "categories": registry.get("categories", ["general"]),
        "stats": dict(registry.get("stats", {}))
    })

    # Only save project scripts, exclude plugin scripts and internal fields
    for name, info in registry.get("scripts", {}).items():
        if info.get("_source") != "plugin":
            # Create a clean copy without internal fields
            clean_info = {k: v for k, v in info.items() if not k.startswith("_")}
            clean_registry["scripts"][name] = clean_info

    # Update stats
    clean_registry["stats"]["total_scripts"] = len(clean_registry.get("scripts", {}))
    clean_registry["stats"]["last_updated"] = datetime.utcnow().isoformat() + "Z"

    try:
        _write_registry_file(registry_path, json.dumps(clean_registry, indent=2))
        return True
    except Exception as e:
        log_error(f"Failed to save script registry: {e}")
        return False


_SAFE_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
_RESERVED_NAMES = {"init_unreal", "__init__"}


def _check_identifier(value: str, what: str) -> Optional[str]:
    """Return an error message when value is not a safe script or category name, else None."""
    if not _SAFE_IDENTIFIER.match(value or ""):
        return f"Invalid {what} '{value}': use letters, digits and underscores, starting with a letter or underscore (64 characters at most)"
    if value.lower() in _RESERVED_NAMES:
        return f"Invalid {what} '{value}': this name is reserved"
    return None


def _is_inside(base_dir: str, target: str) -> bool:
    """True when target resolves (symlinks and junctions followed) to a path inside base_dir."""
    base = os.path.realpath(base_dir)
    try:
        return os.path.commonpath([base, os.path.realpath(target)]) == base
    except ValueError:  # different drives
        return False


def _sanitize_name(name: str) -> str:
    """
    Sanitize a script name to be a valid filename.

    Args:
        name: Script name to sanitize

    Returns:
        Sanitized name safe for filesystem
    """
    # Replace spaces and special chars with underscores
    sanitized = re.sub(r'[^\w\-]', '_', name.lower())
    # Remove consecutive underscores
    sanitized = re.sub(r'_+', '_', sanitized)
    # Remove leading/trailing underscores
    sanitized = sanitized.strip('_')
    return sanitized or "unnamed_script"


def _match_query(text: str, query: str) -> float:
    """
    Calculate a simple match score between text and query.

    Args:
        text: Text to search in
        query: Search query

    Returns:
        Match score from 0.0 to 1.0
    """
    if not text or not query:
        return 0.0

    text_lower = text.lower()
    query_lower = query.lower()

    # Exact match
    if query_lower == text_lower:
        return 1.0

    # Contains full query
    if query_lower in text_lower:
        return 0.8

    # Word match - check if all query words appear
    query_words = query_lower.split()
    text_words = text_lower.split()

    matching_words = sum(1 for qw in query_words if any(qw in tw for tw in text_words))
    if matching_words == len(query_words):
        return 0.6
    elif matching_words > 0:
        return 0.3 * (matching_words / len(query_words))

    return 0.0


def list_scripts(category: Optional[str] = None, limit: int = 20) -> Dict[str, Any]:
    """
    List available scripts with optional category filter.

    Args:
        category: Filter by category (None for all)
        limit: Max scripts to return (default 20)

    Returns:
        {success, data: {scripts: [...], total: int, categories: [...]}, error}

    Each script entry contains: name, description, category, tags, usage_count
    """
    try:
        registry = _load_registry()
        scripts = registry.get("scripts", {})
        categories = registry.get("categories", ["general"])

        # Filter by category if specified
        if category:
            filtered = {
                name: info for name, info in scripts.items()
                if info.get("category", "general") == category
            }
        else:
            filtered = scripts

        # Sort by usage_count (most used first), then by name
        sorted_scripts = sorted(
            filtered.items(),
            key=lambda x: (-x[1].get("usage_count", 0), x[0])
        )

        # Limit results
        limited = sorted_scripts[:limit]

        # Format output (exclude full code, just metadata)
        script_list = []
        for name, info in limited:
            script_list.append({
                "name": name,
                "description": info.get("description", ""),
                "category": info.get("category", "general"),
                "tags": info.get("tags", []),
                "source": info.get("_source", "project"),
                "usage_count": info.get("usage_count", 0),
                "last_used": info.get("last_used"),
            })

        return make_success_result({
            "scripts": script_list,
            "total": len(filtered),
            "returned": len(script_list),
            "categories": categories
        })

    except Exception as e:
        log_error(f"list_scripts failed: {e}")
        return make_error_result(str(e))


def search_scripts(query: str, limit: int = 10) -> Dict[str, Any]:
    """
    Search scripts by name, description, or tags.

    Args:
        query: Search text (case-insensitive)
        limit: Max results (default 10)

    Returns:
        {success, data: {matches: [...], query: str, total: int}, error}

    Each match contains: name, description, category, tags, relevance score
    """
    if not query or not query.strip():
        return make_error_result("Search query cannot be empty")

    try:
        registry = _load_registry()
        scripts = registry.get("scripts", {})

        # Score each script
        scored = []
        for name, info in scripts.items():
            # Check name
            name_score = _match_query(name, query)

            # Check description
            desc_score = _match_query(info.get("description", ""), query)

            # Check tags
            tags = info.get("tags", [])
            tag_score = max((_match_query(tag, query) for tag in tags), default=0.0)

            # Combined score (name weighted highest)
            combined = max(name_score * 1.0, desc_score * 0.8, tag_score * 0.9)

            if combined > 0.1:  # Threshold for inclusion
                scored.append({
                    "name": name,
                    "description": info.get("description", ""),
                    "category": info.get("category", "general"),
                    "tags": tags,
                    "source": info.get("_source", "project"),
                    "relevance": round(combined, 2),
                    "usage_count": info.get("usage_count", 0),
                })

        # Sort by relevance, then usage
        scored.sort(key=lambda x: (-x["relevance"], -x["usage_count"]))

        # Limit results
        matches = scored[:limit]

        return make_success_result({
            "matches": matches,
            "query": query,
            "total": len(scored),
            "returned": len(matches)
        })

    except Exception as e:
        log_error(f"search_scripts failed: {e}")
        return make_error_result(str(e))


def get_script(name: str) -> Dict[str, Any]:
    """
    Load full script code and metadata by name.

    Searches both plugin bundled scripts and project user scripts.

    Args:
        name: Script name (without .py extension)

    Returns:
        {success, data: {name, code, description, tags, category, parameters, source, ...}, error}
    """
    if not name or not name.strip():
        return make_error_result("Script name cannot be empty")

    try:
        registry = _load_registry()
        scripts = registry.get("scripts", {})

        # Find script (case-insensitive)
        name_lower = name.lower()
        script_info = None
        actual_name = None

        for script_name, info in scripts.items():
            if script_name.lower() == name_lower:
                script_info = info
                actual_name = script_name
                break

        if not script_info:
            return make_error_result(f"Script '{name}' not found")

        # Get the base directory for this script (plugin or project)
        base_dir = script_info.get("_base_dir", _get_scripts_dir())
        script_path = os.path.join(base_dir, script_info.get("path", ""))

        if not _is_inside(base_dir, script_path):
            log_warning(f"get_script refused a registry path outside the scripts folder: {script_info.get('path')}")
            return make_error_result(f"Script '{actual_name}' points outside the scripts folder")

        if not os.path.exists(script_path):
            return make_error_result(f"Script file not found: {script_path}")

        with open(script_path, 'r', encoding='utf-8') as f:
            code = f.read()

        return make_success_result({
            "name": actual_name,
            "code": code,
            "description": script_info.get("description", ""),
            "tags": script_info.get("tags", []),
            "category": script_info.get("category", "general"),
            "parameters": script_info.get("parameters", []),
            "path": script_info.get("path"),
            "source": script_info.get("_source", "project"),
            "created": script_info.get("created"),
            "last_used": script_info.get("last_used"),
            "usage_count": script_info.get("usage_count", 0),
        })

    except Exception as e:
        log_error(f"get_script failed: {e}")
        return make_error_result(str(e))


def run_script(name: str, **kwargs) -> Dict[str, Any]:
    """
    Execute a saved script with optional parameters.

    Parameters can be passed as keyword arguments and will be available
    as local variables in the script execution context.

    Args:
        name: Script name to execute
        **kwargs: Parameters to pass to the script as local variables

    Returns:
        {success, data: <script result or None>, error}

    Example:
        result = run_script("create_health", max_hp=100.0, regen_rate=5.0)
    """
    # Get the script
    script_result = get_script(name)
    if not script_result["success"]:
        return script_result

    script_data = script_result["data"]
    code = script_data["code"]

    try:
        # Create execution context with parameters
        exec_globals = {
            '__builtins__': __builtins__,
            'unreal': unreal,
        }
        exec_locals = dict(kwargs)  # Parameters available as locals
        exec_locals['params'] = kwargs  # Also available as 'params' dict

        # Execute the script
        exec(code, exec_globals, exec_locals)

        # Check for a 'result' variable set by the script
        script_output = exec_locals.get('result', None)

        # Update usage stats
        _update_usage_stats(name)

        return make_success_result({
            "script_name": name,
            "result": script_output,
            "parameters_used": list(kwargs.keys()) if kwargs else []
        })

    except Exception as e:
        log_error(f"run_script failed for '{name}': {e}")
        return make_error_result(f"Script execution failed: {e}")


def _update_usage_stats(name: str) -> None:
    """Update usage count and last_used timestamp for a project script."""
    try:
        # Only update stats for project scripts
        project_registry = _load_project_registry()
        if name in project_registry.get("scripts", {}):
            project_registry["scripts"][name]["usage_count"] = project_registry["scripts"][name].get("usage_count", 0) + 1
            project_registry["scripts"][name]["last_used"] = datetime.utcnow().isoformat() + "Z"
            _save_registry(project_registry)
    except Exception as e:
        log_warning(f"Failed to update usage stats: {e}")


def save_script(
    name: str,
    code: str,
    description: str,
    tags: Optional[List[str]] = None,
    category: str = "general",
    parameters: Optional[List[Dict[str, Any]]] = None
) -> Dict[str, Any]:
    """
    Save a new script to the library.

    Args:
        name: Script name (will be sanitized to valid filename)
        code: Python source code
        description: Brief description for search
        tags: Optional list of tags for categorization
        category: Category for organization (default "general")
        parameters: Optional list of parameter definitions
                   [{"name": "param", "type": "string", "required": True}]

    Returns:
        {success, data: {name, path, category, created}, error}

    Example:
        save_script(
            "create_health",
            "unreal.log(f'Health: {params.get(\"hp\", 100)}')",
            "Creates health variables",
            tags=["health", "variables"],
            parameters=[{"name": "hp", "type": "float", "required": False}]
        )
    """
    if not name or not name.strip():
        return make_error_result("Script name cannot be empty")

    if not code or not code.strip():
        return make_error_result("Script code cannot be empty")

    if not description or not description.strip():
        return make_error_result("Script description cannot be empty")

    try:
        if not _ensure_scripts_dir():
            return make_error_result("Failed to create scripts directory")

        # Sanitize and normalize
        sanitized_name = _sanitize_name(name)
        category = (category or "").lower().strip() or "general"
        tags = [t.strip().lower() for t in (tags or []) if t and t.strip()]

        # Names become file and folder names: only plain identifiers, never a reserved module name
        error = _check_identifier(sanitized_name, "script name") or _check_identifier(category, "category")
        if error:
            return make_error_result(error)

        # Ensure category directory exists, inside the scripts folder
        scripts_dir = _get_scripts_dir()
        category_dir = os.path.join(scripts_dir, category)
        relative_path = f"{category}/{sanitized_name}.py"
        script_path = os.path.join(scripts_dir, relative_path)
        if not _is_inside(scripts_dir, category_dir) or not _is_inside(scripts_dir, script_path):
            return make_error_result(f"Category '{category}' resolves outside the scripts folder")
        # Load the project registry (not the plugin one) first: if it cannot be read, nothing is written at all
        registry = _load_project_registry()

        if not os.path.exists(category_dir):
            os.makedirs(category_dir)

        # Write script file
        with open(script_path, 'w', encoding='utf-8') as f:
            f.write(code)

        now = datetime.utcnow().isoformat() + "Z"

        registry["scripts"][sanitized_name] = {
            "path": relative_path,
            "description": description.strip(),
            "tags": tags,
            "category": category,
            "parameters": parameters or [],
            "created": now,
            "last_used": None,
            "usage_count": 0
        }

        # Add category if new
        if category not in registry.get("categories", []):
            registry.setdefault("categories", []).append(category)

        if not _save_registry(registry):
            return make_error_result("Failed to update script registry")

        log_info(f"Saved script: {sanitized_name} to {relative_path}")

        return make_success_result({
            "name": sanitized_name,
            "path": relative_path,
            "category": category,
            "created": now
        })

    except Exception as e:
        log_error(f"save_script failed: {e}")
        return make_error_result(str(e))


def delete_script(name: str) -> Dict[str, Any]:
    """
    Remove a script from the library.

    Only project (user) scripts can be deleted. Plugin bundled scripts are read-only.

    Args:
        name: Script name to delete

    Returns:
        {success, data: {deleted: bool, name: str}, error}
    """
    if not name or not name.strip():
        return make_error_result("Script name cannot be empty")

    try:
        registry = _load_registry()
        scripts = registry.get("scripts", {})

        # Find script (case-insensitive)
        name_lower = name.lower()
        actual_name = None
        script_info = None

        for script_name, info in scripts.items():
            if script_name.lower() == name_lower:
                actual_name = script_name
                script_info = info
                break

        if not actual_name:
            return make_error_result(f"Script '{name}' not found")

        # Prevent deletion of plugin bundled scripts
        if script_info.get("_source") == "plugin":
            return make_error_result(f"Cannot delete plugin bundled script '{actual_name}'. Plugin scripts are read-only.")

        # Delete the file from project scripts directory
        script_path = os.path.join(_get_project_scripts_dir(), script_info.get("path", ""))

        if not _is_inside(_get_project_scripts_dir(), script_path):
            log_warning(f"delete_script refused a registry path outside the scripts folder: {script_info.get('path')}")
            return make_error_result(f"Script '{actual_name}' points outside the scripts folder")

        if os.path.exists(script_path):
            os.remove(script_path)
            log_info(f"Deleted script file: {script_path}")

        # Remove from project registry only
        project_registry = _load_project_registry()
        if actual_name in project_registry.get("scripts", {}):
            del project_registry["scripts"][actual_name]

        if not _save_registry(project_registry):
            return make_error_result("Failed to update script registry")

        return make_success_result({
            "deleted": True,
            "name": actual_name
        })

    except Exception as e:
        log_error(f"delete_script failed: {e}")
        return make_error_result(str(e))


def get_script_stats() -> Dict[str, Any]:
    """
    Get statistics about the script library.

    Returns:
        {success, data: {total_scripts, categories, most_used, recently_used}, error}
    """
    try:
        registry = _load_registry()
        scripts = registry.get("scripts", {})

        # Find most used scripts
        most_used = sorted(
            scripts.items(),
            key=lambda x: -x[1].get("usage_count", 0)
        )[:5]

        # Find recently used scripts
        recently_used = sorted(
            [(n, i) for n, i in scripts.items() if i.get("last_used")],
            key=lambda x: x[1].get("last_used", ""),
            reverse=True
        )[:5]

        return make_success_result({
            "total_scripts": len(scripts),
            "categories": registry.get("categories", ["general"]),
            "most_used": [{"name": n, "usage_count": i.get("usage_count", 0)} for n, i in most_used],
            "recently_used": [{"name": n, "last_used": i.get("last_used")} for n, i in recently_used],
            "last_updated": registry.get("stats", {}).get("last_updated")
        })

    except Exception as e:
        log_error(f"get_script_stats failed: {e}")
        return make_error_result(str(e))


def get_script_functions(script_name: str) -> Dict[str, Any]:
    """
    Extract function signatures from a script using AST parsing.
    Returns function names, parameters, types, and docstrings without full implementation.
    This is much more token-efficient than get_script() for discovering available functions.

    Searches both plugin bundled scripts and project user scripts.

    Args:
        script_name: Name of the script to analyze

    Returns:
        {success, data: {script, category, path, source, functions: [...]}, error}

    Each function entry contains:
        - name: Function name
        - parameters: List of {name, type_hint, default_value}
        - return_type: Return type annotation if present
        - docstring: Function documentation
        - line_number: Location in file
    """
    import ast
    import inspect

    if not script_name or not script_name.strip():
        return make_error_result("Script name cannot be empty")

    try:
        # Load registry to get metadata
        registry = _load_registry()
        scripts = registry.get("scripts", {})

        if script_name not in scripts:
            return make_error_result(f"Script '{script_name}' not found")

        script_info = scripts[script_name]

        # Get the base directory for this script (plugin or project)
        base_dir = script_info.get("_base_dir", _get_scripts_dir())
        script_path = os.path.join(base_dir, script_info.get("path", ""))

        if not _is_inside(base_dir, script_path):
            log_warning(f"get_script_functions refused a registry path outside the scripts folder: {script_info.get('path')}")
            return make_error_result(f"Script '{script_name}' points outside the scripts folder")

        if not os.path.exists(script_path):
            return make_error_result(f"Script file not found: {script_path}")

        # Read and parse the script
        with open(script_path, 'r', encoding='utf-8') as f:
            source_code = f.read()

        try:
            tree = ast.parse(source_code, filename=script_path)
        except SyntaxError as e:
            return make_error_result(f"Script has syntax errors: {e}")

        # Extract function definitions
        functions = []
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef):
                # Skip private functions (starting with _)
                if node.name.startswith('_'):
                    continue

                # Extract parameters
                params = []
                args = node.args

                # Regular arguments
                for i, arg in enumerate(args.args):
                    param_info = {
                        "name": arg.arg,
                        "type_hint": _ast_annotation_to_string(arg.annotation) if arg.annotation else None,
                        "default_value": None
                    }

                    # Check if this arg has a default value
                    defaults_offset = len(args.args) - len(args.defaults)
                    if i >= defaults_offset:
                        default_idx = i - defaults_offset
                        param_info["default_value"] = _ast_constant_to_string(args.defaults[default_idx])

                    params.append(param_info)

                # *args
                if args.vararg:
                    params.append({
                        "name": f"*{args.vararg.arg}",
                        "type_hint": _ast_annotation_to_string(args.vararg.annotation) if args.vararg.annotation else None,
                        "default_value": None
                    })

                # Keyword-only arguments
                for i, arg in enumerate(args.kwonlyargs):
                    param_info = {
                        "name": arg.arg,
                        "type_hint": _ast_annotation_to_string(arg.annotation) if arg.annotation else None,
                        "default_value": _ast_constant_to_string(args.kw_defaults[i]) if args.kw_defaults[i] else None
                    }
                    params.append(param_info)

                # **kwargs
                if args.kwarg:
                    params.append({
                        "name": f"**{args.kwarg.arg}",
                        "type_hint": _ast_annotation_to_string(args.kwarg.annotation) if args.kwarg.annotation else None,
                        "default_value": None
                    })

                # Extract return type
                return_type = None
                if node.returns:
                    return_type = _ast_annotation_to_string(node.returns)

                # Extract docstring
                docstring = ast.get_docstring(node) or ""

                functions.append({
                    "name": node.name,
                    "parameters": params,
                    "return_type": return_type,
                    "docstring": docstring.strip(),
                    "line_number": node.lineno
                })

        return make_success_result({
            "script": script_name,
            "category": script_info.get("category", "general"),
            "path": script_path,
            "source": script_info.get("_source", "project"),
            "description": script_info.get("description", ""),
            "tags": script_info.get("tags", []),
            "functions": functions,
            "function_count": len(functions)
        })

    except Exception as e:
        log_error(f"get_script_functions failed: {e}")
        return make_error_result(str(e))


def _ast_annotation_to_string(annotation) -> str:
    """Convert AST annotation node to string representation."""
    import ast

    if annotation is None:
        return None

    if isinstance(annotation, ast.Name):
        return annotation.id
    elif isinstance(annotation, ast.Constant):
        return repr(annotation.value)
    elif isinstance(annotation, ast.Subscript):
        # Handle generics like List[str], Dict[str, int]
        value = _ast_annotation_to_string(annotation.value)
        slice_val = _ast_annotation_to_string(annotation.slice)
        return f"{value}[{slice_val}]"
    elif isinstance(annotation, ast.Tuple):
        # Handle tuples in type hints
        elements = [_ast_annotation_to_string(el) for el in annotation.elts]
        return f"({', '.join(elements)})"
    elif isinstance(annotation, ast.Attribute):
        # Handle module.Class type hints
        value = _ast_annotation_to_string(annotation.value)
        return f"{value}.{annotation.attr}"
    else:
        # Fallback: try to unparse if available (Python 3.9+)
        try:
            return ast.unparse(annotation)
        except:
            return str(type(annotation).__name__)


def _ast_constant_to_string(node) -> str:
    """Convert AST constant/expression to string representation."""
    import ast

    if isinstance(node, ast.Constant):
        if isinstance(node.value, str):
            return repr(node.value)
        return str(node.value)
    elif isinstance(node, ast.Name):
        return node.id
    elif isinstance(node, ast.List):
        elements = [_ast_constant_to_string(el) for el in node.elts]
        return f"[{', '.join(elements)}]"
    elif isinstance(node, ast.Dict):
        pairs = [f"{_ast_constant_to_string(k)}: {_ast_constant_to_string(v)}"
                 for k, v in zip(node.keys, node.values)]
        return f"{{{', '.join(pairs)}}}"
    else:
        # Fallback
        try:
            return ast.unparse(node)
        except:
            return "..."
