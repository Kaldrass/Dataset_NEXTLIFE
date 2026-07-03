import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path


SECURITY_ORDER = ["Original", "Transparent", "Suffisant", "Confidentiel"]


def read_answers(path: Path) -> list[dict]:
    if path.suffix.lower() == ".json":
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, list) else data.get("answers", [])
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def truthy(value) -> bool:
    return value is True or str(value).lower() == "true"


def write_csv(path: Path, rows: list[dict], headers: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(paths: list[Path]) -> tuple[list[dict], list[dict]]:
    grouped = defaultdict(list)
    for path in paths:
        for answer in read_answers(path):
            if not answer.get("trial_id"):
                continue
            grouped[answer["trial_id"]].append(answer)

    summary_rows = []
    choice_rows = []
    for trial_id, answers in sorted(grouped.items()):
        first = answers[0]
        complete = [item for item in answers if item.get("chosen_label") and item.get("security_level")]
        label_counts = Counter(item.get("chosen_label", "") for item in complete)
        security_counts = Counter(item.get("security_level", "") for item in complete)
        n = len(complete)
        correct = sum(1 for item in complete if truthy(item.get("label_correct")))

        base = {
            "trial_id": trial_id,
            "object_id": first.get("object_id", ""),
            "object_name": first.get("object_name", ""),
            "imagenet_class": first.get("imagenet_class", ""),
            "distortion_modality": first.get("distortion_modality", ""),
            "distortion_family": first.get("distortion_family", ""),
            "distortion_profile": first.get("distortion_profile", ""),
            "distorted_variant_id": first.get("distorted_variant_id", ""),
            "n_complete": n,
            "recognition_rate": f"{(correct / n):.6f}" if n else "",
        }
        for level in SECURITY_ORDER:
            base[f"security_{level}"] = security_counts.get(level, 0)
            base[f"security_{level}_rate"] = f"{(security_counts.get(level, 0) / n):.6f}" if n else ""
        summary_rows.append(base)

        for label, count in sorted(label_counts.items(), key=lambda item: (-item[1], item[0])):
            choice_rows.append(
                {
                    **{key: base[key] for key in base if key not in {"n_complete", "recognition_rate"}},
                    "choice_type": "label",
                    "choice": label,
                    "count": count,
                    "rate": f"{(count / n):.6f}" if n else "",
                }
            )
        for level in SECURITY_ORDER:
            count = security_counts.get(level, 0)
            choice_rows.append(
                {
                    **{key: base[key] for key in base if key not in {"n_complete", "recognition_rate"}},
                    "choice_type": "security",
                    "choice": level,
                    "count": count,
                    "rate": f"{(count / n):.6f}" if n else "",
                }
            )
    return summary_rows, choice_rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Aggregate NEXTLIFE recognition/security exports.")
    parser.add_argument("inputs", nargs="+", help="CSV or JSON exports from recognition_viewer.html")
    parser.add_argument("--out-dir", default="ExperimentSecurity/results")
    args = parser.parse_args()

    summary_rows, choice_rows = aggregate([Path(item) for item in args.inputs])
    out_dir = Path(args.out_dir)
    summary_headers = [
        "trial_id",
        "object_id",
        "object_name",
        "imagenet_class",
        "distortion_modality",
        "distortion_family",
        "distortion_profile",
        "distorted_variant_id",
        "n_complete",
        "recognition_rate",
        "security_Original",
        "security_Original_rate",
        "security_Transparent",
        "security_Transparent_rate",
        "security_Suffisant",
        "security_Suffisant_rate",
        "security_Confidentiel",
        "security_Confidentiel_rate",
    ]
    choice_headers = [
        "trial_id",
        "object_id",
        "object_name",
        "imagenet_class",
        "distortion_modality",
        "distortion_family",
        "distortion_profile",
        "distorted_variant_id",
        "choice_type",
        "choice",
        "count",
        "rate",
    ]
    write_csv(out_dir / "recognition_security_summary.csv", summary_rows, summary_headers)
    write_csv(out_dir / "recognition_security_barplot_counts.csv", choice_rows, choice_headers)
    print(f"Wrote {len(summary_rows)} trial summaries to {out_dir}")


if __name__ == "__main__":
    main()
