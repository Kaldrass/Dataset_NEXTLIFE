"""Validated, portable object decisions shared by the explorer and trial builder."""
import json
from pathlib import Path

STATUSES = {"pending", "retained", "repair", "excluded"}


def retained_object_ids(path: Path) -> set[str]:
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict) or payload.get("schema_version") != 1 or not isinstance(payload.get("objects"), list):
        raise ValueError("Expected an object review export (schema_version=1, objects list).")
    seen, retained = set(), set()
    for row in payload["objects"]:
        if not isinstance(row, dict):
            raise ValueError("Invalid object review entry.")
        oid = row.get("object_id")
        if not isinstance(oid, str) or not oid.strip() or oid in seen or row.get("status") not in STATUSES:
            raise ValueError("Invalid/duplicate object ID or unknown review status.")
        seen.add(oid)
        if row["status"] == "retained":
            retained.add(oid)
    return retained
