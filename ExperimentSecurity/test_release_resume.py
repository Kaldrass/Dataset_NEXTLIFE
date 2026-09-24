"""Check interruption recovery without running Blender or changing real assets."""
import contextlib
import csv
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import regenerate_reviewed_dataset as runner


class ResumeTests(unittest.TestCase):
    def fixture(self, root):
        release = root / "Objects/Releases/test"
        release.mkdir(parents=True)
        review = {"schema_version": 1, "objects": [{"object_id": x, "status": "retained"} for x in ("a", "b")]}
        runner.write_json(release / "review.json", review)
        runner.write_json(release / "generation_config.json", {"execution": {"object_whitelist": ["a", "b"]}})
        source = release / "Originals/a/model.obj"
        source.parent.mkdir(parents=True)
        source.write_bytes(b"original")
        runner.write_json(release / "source_hashes.json", {"Originals/a/model.obj": hashlib.sha256(b"original").hexdigest()})
        self.complete(release, "a")
        partial = release / "Distorted/TextureVariants/b/partial.txt"
        partial.parent.mkdir(parents=True)
        partial.write_text("keep me")
        return release

    def complete(self, release, oid):
        report = release / "reports" / oid
        report.mkdir(parents=True, exist_ok=True)
        with (report / "test.generation_log.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=["variant_id", "status", "manifest_path"])
            writer.writeheader()
            for i in range(28):
                manifest = release / "Distorted/CombinedVariants" / oid / str(i) / "manifest.json"
                runner.write_json(manifest, {})
                writer.writerow({"variant_id": str(i), "status": "ok", "manifest_path": str(manifest)})

    def run_resume(self, root, release):
        argv = ["runner", str(release / "review.json"), "--release", str(release), "--resume"]
        with patch.object(runner, "ROOT", root), patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()):
            runner.main()

    def test_reuses_completed_and_archives_partial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            release = self.fixture(root)
            untouched = release / "Distorted/CombinedVariants/a/0/manifest.json"
            before = untouched.stat().st_mtime_ns
            def generate(command, **kwargs):
                config = json.loads(Path(command[-1]).read_text())
                self.assertEqual(config["execution"]["object_whitelist"], ["b"])
                self.assertFalse((release / "Distorted/TextureVariants/b").exists())
                self.complete(release, "b")
                return SimpleNamespace(returncode=0)
            with patch.object(runner.subprocess, "run", side_effect=generate) as subprocess_run:
                self.run_resume(root, release)
            self.assertEqual(subprocess_run.call_count, 1)
            self.assertEqual(untouched.stat().st_mtime_ns, before)
            self.assertEqual(next(release.glob("previous_attempt_*/Distorted/TextureVariants/b/partial.txt")).read_text(), "keep me")
            results = json.loads((release / "generation_results.json").read_text())
            self.assertEqual({r["object_id"] for r in results}, {"a", "b"})
            with patch.object(runner.subprocess, "run") as subprocess_run:
                self.run_resume(root, release)
                subprocess_run.assert_not_called()

    def test_changed_snapshot_stops_before_moving_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            release = self.fixture(root)
            (release / "Originals/a/model.obj").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Frozen source changed"):
                self.run_resume(root, release)
            self.assertTrue((release / "Distorted/TextureVariants/b/partial.txt").exists())
            self.assertEqual(list(release.glob("previous_attempt_*")), [])


if __name__ == "__main__":
    unittest.main()
