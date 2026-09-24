import json
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import build_recognition_experiment as builder
from audit_original_objects import audit_obj, build_audit
from object_review import retained_object_ids


class ObjectReviewTests(unittest.TestCase):
    def test_duplicate_geometry_and_old_exclusion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "metadata.json").write_text("{}")
            for key in ["a", "b"]:
                folder = root / "Objects" / "Originals" / key
                folder.mkdir(parents=True)
                (folder / "model.obj").write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
            (root / "Old").mkdir()
            (root / "Old" / "bad.obj").write_text("invalid")
            report = build_audit(root, hash_geometry=True)
            self.assertEqual(report["counts"]["objects"], 2)
            self.assertEqual(report["objects"][0]["geometry_sha256"], report["objects"][1]["geometry_sha256"])
            self.assertTrue(all(any("OBJ identique" in issue for issue in row["issues"]) for row in report["objects"]))

    def test_empty_review_preserves_existing_trials(self):
        with tempfile.TemporaryDirectory() as tmp:
            review, output = Path(tmp) / "review.json", Path(tmp) / "trials.json"
            review.write_text('{"schema_version":1,"objects":[]}')
            output.write_text("existing trials")
            with patch.object(sys, "argv", ["builder", "--object-review", str(review), "--output", str(output)]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    builder.main()
            self.assertEqual(error.exception.code, 2)
            self.assertEqual(output.read_text(), "existing trials")

    def test_review_validation_and_empty_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "review.json"
            rows = [{"object_id": key, "status": status} for key, status in
                    [("a", "retained"), ("b", "repair"), ("c", "excluded"), ("d", "pending")]]
            path.write_text(json.dumps({"schema_version": 1, "objects": rows}))
            self.assertEqual(retained_object_ids(path), {"a"})
            path.write_text('{"schema_version":1,"objects":[]}')
            self.assertEqual(retained_object_ids(path), set())
            for payload in [{"objects": rows}, {"schema_version": 1, "objects": rows + [rows[0]]},
                            {"schema_version": 1, "objects": [{"object_id": "a", "status": "typo"}]}]:
                path.write_text(json.dumps(payload))
                with self.assertRaises(ValueError):
                    retained_object_ids(path)

    def test_trials_filter_before_object_limit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "metadata.json").write_text(json.dumps({key: {"imagenet_class": "tabby"} for key in ["a", "b"]}))
            for key in ["a", "b"]:
                folder = root / "Objects/Distorted/CombinedVariants" / key / "v"
                folder.mkdir(parents=True)
                (folder / "manifest.json").write_text(json.dumps({"object_id": key, "active_groups": []}))
            with patch.object(builder, "ROOT", root), patch.object(builder, "DISTORTED_DIR", root / "Objects/Distorted"), patch.object(builder, "find_original_model", return_value={"kind": "original"}), patch.object(builder, "combined_model_spec", return_value={"variant_id": "v"}):
                trials = builder.make_trials(1, 10, 42, 0, 6, {"b"})
                self.assertEqual([t["object_id"] for t in trials], ["b"])
                self.assertEqual(trials[0]["imagenet_class"], "Cat")
                self.assertEqual(builder.make_trials(1, 10, 42, 0, 6, set()), [])

    def test_audit_geometry_and_missing_texture(self):
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "model.obj"
            obj.write_text('mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
            (obj.parent / "material.mtl").write_text("newmtl base\nmap_Kd missing.png\n")
            report = audit_obj(obj, 0)
            self.assertEqual(report["faces"], 1)
            self.assertTrue(any("Texture absente" in issue for issue in report["issues"]))
            obj.write_text("v nan 0 0\nf 0 2 8\n")
            self.assertTrue(any("Géométrie invalide" in issue for issue in audit_obj(obj, 0)["issues"]))


if __name__ == "__main__":
    unittest.main()
