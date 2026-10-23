"""
DataTable population utilities for Unreal Engine.

Populate DataTables from various data sources programmatically.
Supports JSON and CSV input formats.

IMPORTANT: The DataTable must already exist with a row struct that has columns
matching the CSV/JSON data. Column names in the data must match the struct's
property names. To create a new DataTable with a custom struct, use:
    from scripts.data.struct_creator import create_datatable_row_struct

Usage:
    from scripts.data.datatable_populator import populate_from_json, get_datatable_info

    # Add or update rows (existing rows are kept; nothing is saved to disk)
    json_data = '[{"Name": "Sword", "Damage": 10}, {"Name": "Axe", "Damage": 15}]'
    populate_from_json('/Game/DT_Items', json_data)

    # Replace every row and save the asset
    populate_from_json('/Game/DT_Items', json_data, clear_existing=True, save=True)

    # Get info about a DataTable
    info = get_datatable_info('/Game/DT_Items')

    # Export DataTable to CSV
    result = export_to_csv('/Game/DT_Items')
    csv_string = result['data']['csv_string']

Functions return standardized dicts: {success: bool, data: {...}, error: str|None}
"""

import unreal
import csv
import io
import json
from typing import List, Dict, Any, Optional, Tuple


def populate_from_json(
    datatable_path: str,
    json_string: str,
    clear_existing: bool = False,
    save: bool = False
) -> Dict[str, Any]:
    """
    Add or update DataTable rows from a JSON string.

    The JSON must be an array of objects, one per row. Each object needs a
    "Name" field with the row name ("RowName" is accepted as an alias). If the
    table sets ImportKeyField, that field holds the row name instead.

    By default the rows are merged into the table: existing rows are kept, a
    row whose name already exists is updated (fields you give replace the old
    values, fields you leave out keep them), and new names are added. With
    clear_existing=True the table is replaced by exactly the given rows.

    The change is made in memory and the asset is marked dirty. It is written
    to disk only when save=True. If the import does not produce the expected
    rows, the previous rows are restored and nothing is saved.

    Args:
        datatable_path: Path to the DataTable asset
        json_string: JSON array of row objects
        clear_existing: If True, replace all rows; if False (default), merge by row name
        save: If True, save the asset to disk after a successful fill (default False)

    Returns:
        {success, data: {datatable_path, rows_in_json, rows_added, rows_updated,
         row_count, cleared_existing, saved}, error}

    Example JSON:
        [
            {"Name": "Sword", "Damage": 10, "Weight": 2.5},
            {"Name": "Axe", "Damage": 15, "Weight": 4.0}
        ]
    """
    if not datatable_path:
        return _make_error("DataTable path cannot be empty")

    if not json_string:
        return _make_error("JSON string cannot be empty")

    try:
        try:
            data = json.loads(json_string)
        except json.JSONDecodeError as e:
            return _make_error(f"Invalid JSON: {e}")

        if not isinstance(data, list):
            return _make_error("JSON must be an array of objects")

        datatable, error = _load_datatable(datatable_path)
        if error:
            return _make_error(error)

        key_field = _get_key_field(datatable)
        new_rows = []
        for index, row in enumerate(data):
            if not isinstance(row, dict):
                return _make_error(f"Row {index} is not a JSON object")
            row = dict(row)
            if key_field not in row and key_field == "Name" and "RowName" in row:
                row["Name"] = row.pop("RowName")
            name = row.get(key_field)
            if name is None or str(name).strip() == "":
                return _make_error(f"Row {index} has no '{key_field}' field for the row name")
            row[key_field] = str(name).strip()
            new_rows.append(row)

        backup_json = datatable.export_to_json_string()
        if clear_existing:
            existing_rows = []
        else:
            try:
                existing_rows = json.loads(backup_json) if backup_json else []
            except json.JSONDecodeError as e:
                return _make_error(f"Could not read the existing rows: {e}")
            if not isinstance(existing_rows, list):
                return _make_error("Could not read the existing rows: export is not an array")

        merged, added, updated = _merge_rows(existing_rows, new_rows, lambda r: r.get(key_field))
        expected_names = [str(r[key_field]) for r in merged]

        ok = datatable.fill_from_json_string(json.dumps(merged))
        return _finish_fill(
            datatable_path, ok, expected_names, backup_json, save,
            {
                "rows_in_json": len(data),
                "rows_added": added,
                "rows_updated": updated,
                "cleared_existing": bool(clear_existing),
            })

    except Exception as e:
        return _make_error(f"Error populating DataTable: {e}")


def populate_from_csv(
    datatable_path: str,
    csv_string: str,
    clear_existing: bool = False,
    save: bool = False
) -> Dict[str, Any]:
    """
    Add or update DataTable rows from a CSV string.

    The first line must be the header. The first column holds the row name
    (its header is usually "Name" or "---"); the other headers must match the
    row struct's property names.

    By default the rows are merged into the table: existing rows are kept, a
    row whose name already exists is updated (columns you give replace the old
    values, columns you leave out keep them), and new names are added. With
    clear_existing=True the table is replaced by exactly the given rows.

    The change is made in memory and the asset is marked dirty. It is written
    to disk only when save=True. If the import does not produce the expected
    rows, the previous rows are restored and nothing is saved.

    Args:
        datatable_path: Path to the DataTable
        csv_string: CSV data with headers
        clear_existing: If True, replace all rows; if False (default), merge by row name
        save: If True, save the asset to disk after a successful fill (default False)

    Returns:
        {success, data: {datatable_path, rows_in_csv, rows_added, rows_updated,
         row_count, cleared_existing, saved}, error}

    Example CSV:
        Name,Damage,Weight
        Sword,10,2.5
        Axe,15,4.0
    """
    if not datatable_path:
        return _make_error("DataTable path cannot be empty")

    if not csv_string:
        return _make_error("CSV string cannot be empty")

    try:
        new_header, new_rows, error = _parse_csv(csv_string)
        if error:
            return _make_error(f"Invalid CSV: {error}")

        datatable, error = _load_datatable(datatable_path)
        if error:
            return _make_error(error)

        backup_json = datatable.export_to_json_string()
        if clear_existing:
            header = ["---"] + new_header[1:]
            existing_rows = []
        else:
            old_header, existing_rows, error = _parse_csv(datatable.export_to_csv_string() or "---")
            if error:
                return _make_error(f"Could not read the existing rows: {error}")
            header = ["---"] + old_header[1:] + [c for c in new_header[1:] if c not in old_header[1:]]

        merged, added, updated = _merge_rows(existing_rows, new_rows, lambda r: r.get(0))

        out = io.StringIO()
        writer = csv.writer(out, lineterminator="\n")
        writer.writerow(header)
        for row in merged:
            writer.writerow([row.get(0, "")] + [row.get(col, "") for col in header[1:]])

        expected_names = [str(r[0]) for r in merged]
        ok = datatable.fill_from_csv_string(out.getvalue())
        return _finish_fill(
            datatable_path, ok, expected_names, backup_json, save,
            {
                "rows_in_csv": len(new_rows),
                "rows_added": added,
                "rows_updated": updated,
                "cleared_existing": bool(clear_existing),
            })

    except Exception as e:
        return _make_error(f"Error populating DataTable from CSV: {e}")


def get_datatable_info(datatable_path: str) -> Dict[str, Any]:
    """
    Get information about a DataTable.

    Args:
        datatable_path: Path to the DataTable

    Returns:
        {success, data: {row_struct, row_names, row_count}, error}
    """
    if not datatable_path:
        return _make_error("DataTable path cannot be empty")

    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(datatable_path):
            return _make_error(f"DataTable does not exist: {datatable_path}")

        datatable = unreal.EditorAssetLibrary.load_asset(datatable_path)
        if datatable is None:
            return _make_error(f"Failed to load DataTable: {datatable_path}")

        if not isinstance(datatable, unreal.DataTable):
            return _make_error(f"Asset is not a DataTable: {datatable_path}")

        # Get row names
        row_names = datatable.get_row_names()

        # Get struct info using the correct method
        row_struct_name = ""
        try:
            row_struct = datatable.get_row_struct()
            if row_struct:
                row_struct_name = str(row_struct.get_name())
        except:
            pass

        return _make_success({
            "datatable_path": datatable_path,
            "row_struct": row_struct_name,
            "row_names": [str(name) for name in row_names] if row_names else [],
            "row_count": len(row_names) if row_names else 0
        })

    except Exception as e:
        return _make_error(f"Error getting DataTable info: {e}")


def export_to_json(datatable_path: str) -> Dict[str, Any]:
    """
    Export a DataTable to JSON string.

    Args:
        datatable_path: Path to the DataTable

    Returns:
        {success, data: {datatable_path, json_string}, error}
    """
    if not datatable_path:
        return _make_error("DataTable path cannot be empty")

    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(datatable_path):
            return _make_error(f"DataTable does not exist: {datatable_path}")

        datatable = unreal.EditorAssetLibrary.load_asset(datatable_path)
        if datatable is None:
            return _make_error(f"Failed to load DataTable: {datatable_path}")

        if not isinstance(datatable, unreal.DataTable):
            return _make_error(f"Asset is not a DataTable: {datatable_path}")

        # Export to JSON using the built-in function
        json_string = datatable.export_to_json_string()

        return _make_success({
            "datatable_path": datatable_path,
            "json_string": json_string
        })

    except Exception as e:
        return _make_error(f"Error exporting DataTable to JSON: {e}")


def export_to_csv(datatable_path: str) -> Dict[str, Any]:
    """
    Export a DataTable to CSV string.

    Args:
        datatable_path: Path to the DataTable

    Returns:
        {success, data: {datatable_path, csv_string}, error}
    """
    if not datatable_path:
        return _make_error("DataTable path cannot be empty")

    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(datatable_path):
            return _make_error(f"DataTable does not exist: {datatable_path}")

        datatable = unreal.EditorAssetLibrary.load_asset(datatable_path)
        if datatable is None:
            return _make_error(f"Failed to load DataTable: {datatable_path}")

        if not isinstance(datatable, unreal.DataTable):
            return _make_error(f"Asset is not a DataTable: {datatable_path}")

        # Export to CSV
        csv_string = datatable.export_to_csv_string()

        return _make_success({
            "datatable_path": datatable_path,
            "csv_string": csv_string
        })

    except Exception as e:
        return _make_error(f"Error exporting DataTable to CSV: {e}")


def list_datatables(
    folder_path: str,
    recursive: bool = True
) -> Dict[str, Any]:
    """
    Find all DataTables in a folder.

    Args:
        folder_path: Folder to search
        recursive: Include subfolders

    Returns:
        {success, data: {datatables: [...], count: int}, error}
    """
    if not folder_path:
        return _make_error("Folder path cannot be empty")

    try:
        asset_paths = unreal.EditorAssetLibrary.list_assets(
            folder_path,
            recursive,
            include_folder=False
        )

        if not asset_paths:
            return _make_success({
                "datatables": [],
                "count": 0,
                "folder_path": folder_path
            })

        datatables = []

        for asset_path in asset_paths:
            asset_data = unreal.EditorAssetLibrary.find_asset_data(asset_path)
            if asset_data and asset_data.is_valid():
                full_name = unreal.AssetRegistryHelpers.get_full_name(asset_data)
                if 'DataTable' in full_name:
                    datatables.append({
                        "path": asset_path,
                        "name": asset_path.split('/')[-1]
                    })

        return _make_success({
            "datatables": datatables,
            "count": len(datatables),
            "folder_path": folder_path
        })

    except Exception as e:
        return _make_error(f"Error listing DataTables: {e}")


# ============================================================================
# Private Helper Functions
# ============================================================================

def _load_datatable(datatable_path: str) -> Tuple[Optional[Any], Optional[str]]:
    """Load a DataTable with a row struct. Returns (datatable, error)."""
    if not unreal.EditorAssetLibrary.does_asset_exist(datatable_path):
        return None, f"DataTable does not exist: {datatable_path}"

    datatable = unreal.EditorAssetLibrary.load_asset(datatable_path)
    if datatable is None:
        return None, f"Failed to load DataTable: {datatable_path}"

    if not isinstance(datatable, unreal.DataTable):
        return None, f"Asset is not a DataTable: {datatable_path}"

    try:
        if datatable.get_row_struct() is None:
            return None, f"DataTable has no row struct: {datatable_path}"
    except Exception:
        pass

    return datatable, None


def _get_key_field(datatable: Any) -> str:
    """The JSON field UE reads the row name from: ImportKeyField if set, else 'Name'."""
    try:
        key_field = str(datatable.get_editor_property("import_key_field") or "")
    except Exception:
        key_field = ""
    return key_field or "Name"


def _merge_rows(existing_rows: List[Dict], new_rows: List[Dict], key_of) -> Tuple[List[Dict], int, int]:
    """
    Merge new rows into existing rows by row name (case-insensitive, like FName).
    A new row with an existing name overlays its fields onto the old row.
    Returns (merged_rows, rows_added, rows_updated).
    """
    merged = [dict(r) for r in existing_rows]
    index = {str(key_of(r)).lower(): i for i, r in enumerate(merged)}
    original = set(index)
    touched = set()

    for row in new_rows:
        name = str(key_of(row)).lower()
        if name in index:
            merged[index[name]].update(row)
        else:
            index[name] = len(merged)
            merged.append(dict(row))
        touched.add(name)

    updated = len(touched & original)
    return merged, len(touched) - updated, updated


def _parse_csv(csv_string: str) -> Tuple[List[str], List[Dict], Optional[str]]:
    """
    Parse DataTable CSV. Returns (header, rows, error); each row is a dict with
    the row name under key 0 and the other cells under their header names.
    """
    lines = [r for r in csv.reader(io.StringIO(csv_string.lstrip("ï»¿"))) if any(c.strip() for c in r)]
    if not lines:
        return [], [], "no header line"

    header = [c.strip() for c in lines[0]]
    rows = []
    for number, cells in enumerate(lines[1:], start=2):
        name = cells[0].strip() if cells else ""
        if not name:
            return [], [], f"line {number} has no row name in the first column"
        if len(cells) > len(header):
            return [], [], f"line {number} has more cells than the header"
        row = {0: name}
        for col, value in zip(header[1:], cells[1:]):
            row[col] = value
        rows.append(row)
    return header, rows, None


def _finish_fill(datatable_path: str, fill_ok: bool, expected_names: List[str],
                 backup_json: str, save: bool, extra: Dict[str, Any]) -> Dict[str, Any]:
    """Check the fill produced the expected rows, restore on failure, optionally save."""
    datatable = unreal.EditorAssetLibrary.load_asset(datatable_path)
    actual = [str(n) for n in (datatable.get_row_names() or [])] if datatable else []

    problem = None
    if not fill_ok:
        problem = "the engine import reported failure"
    else:
        missing = sorted(set(n.lower() for n in expected_names) - set(n.lower() for n in actual))
        if missing or len(actual) != len(set(n.lower() for n in expected_names)):
            problem = (f"expected {len(set(n.lower() for n in expected_names))} rows, got {len(actual)}"
                       + (f"; missing {missing[:10]}" if missing else "")
                       + " (check the Output Log for LogDataTable/LogCSVImportFactory problems)")

    if problem:
        restored = False
        if datatable and backup_json:
            restored = bool(datatable.fill_from_json_string(backup_json))
        return _make_error(
            f"DataTable import failed for {datatable_path}: {problem}. "
            + ("The previous rows were restored; nothing was saved." if restored
               else "The previous rows could not be restored; nothing was saved."))

    datatable.modify()
    saved = False
    if save:
        saved = bool(unreal.EditorAssetLibrary.save_asset(datatable_path, only_if_is_dirty=False))
        if not saved:
            return _make_error(f"Rows were updated in memory but saving {datatable_path} failed")

    data = {"datatable_path": datatable_path}
    data.update(extra)
    data["row_count"] = len(actual)
    data["saved"] = saved
    return _make_success(data)


def _make_success(data: Any) -> Dict[str, Any]:
    """Create a success result."""
    return {
        "success": True,
        "data": data,
        "error": None
    }


def _make_error(message: str) -> Dict[str, Any]:
    """Create an error result."""
    return {
        "success": False,
        "data": None,
        "error": message
    }
