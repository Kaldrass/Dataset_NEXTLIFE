"""Prepare a selection and repair inventory without modifying reviews or assets."""
import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

from build_recognition_experiment import ROOT, metadata_for, normalize_label
from object_review import retained_object_ids


def proposed_action(row):
    if row["status"] == "retained":
        return "Inclure dans la sélection ; conserver la référence."
    if row["status"] == "excluded":
        return "Exclure des essais ; conserver les fichiers locaux."
    note = row.get("note", "").upper()
    actions = []
    if "RENTRE DANS LE SOL" in note:
        actions.append("Vérifier le placement au sol dans le viewer avant toute modification du modèle.")
    if any(word in note for word in ("TERRAIN", "SPHERE", "CONE", "SOL A ENLEVER", "MUR")):
        actions.append("Inspecter les éléments annexes ; préparer un nettoyage dans une copie après identification des parties à conserver.")
    if "BRILL" in note:
        actions.append("Comparer les paramètres MTL au rendu du viewer avant de modifier le matériau.")
    if "REORIENTER" in note:
        actions.append("Déterminer la rotation correcte visuellement et documenter la transformation.")
    if "MANQUANT" in note:
        actions.append("Vérifier la source complète ; évaluer réparation ou remplacement de la référence.")
    if "TROP LOURD" in note:
        actions.append("Mesurer la complexité et la fluidité ; tester une simplification dans une copie dérivée.")
    if "TROP GROS" in note:
        actions.append("Vérifier le cadrage et l'échelle dans le viewer.")
    return " ".join(actions) or "Inspection manuelle du motif et de la note avant correction."


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("review", type=Path)
    args = parser.parse_args()
    retained = retained_object_ids(args.review)
    raw = args.review.read_bytes()
    payload = json.loads(raw.decode("utf-8-sig"))
    metadata = json.loads((ROOT / "metadata.json").read_text(encoding="utf-8"))
    known = {p.name for p in (ROOT / "Objects/Originals").iterdir() if p.is_dir()}
    reviewed = {row["object_id"] for row in payload["objects"]}
    if reviewed - known:
        parser.error(f"Unknown object IDs: {sorted(reviewed - known)}")
    rows, by_class = [], defaultdict(Counter)
    for row in payload["objects"]:
        meta = metadata_for(metadata, row["object_id"])
        label = normalize_label(str(meta.get("imagenet_class", "")))
        by_class[label][row["status"]] += 1
        rows.append({"object_id": row["object_id"], "name": meta.get("name", row["object_id"]),
                     "class": label, "status": row["status"], "reason": row.get("reason", ""),
                     "note": row.get("note", ""), "proposed_action": proposed_action(row)})
    counts = Counter(row["status"] for row in rows)
    target = ROOT / "ExperimentSecurity/results" / ("review_summary_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    target.mkdir(parents=True, exist_ok=False)
    selection = {"source": str(args.review.resolve()), "source_sha256": hashlib.sha256(raw).hexdigest(),
                 "counts": dict(counts), "retained_object_ids": sorted(retained),
                 "unreviewed_object_ids": sorted(known-reviewed)}
    (target / "selection.json").write_text(json.dumps(selection, ensure_ascii=False, indent=2), encoding="utf-8")
    for filename, selected in [("all_decisions.csv", rows), ("repairs.csv", [r for r in rows if r["status"] == "repair"]),
                               ("excluded.csv", [r for r in rows if r["status"] == "excluded"])]:
        with (target / filename).open("x", encoding="utf-8-sig", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["object_id", "name", "class", "status", "reason", "note", "proposed_action"])
            writer.writeheader()
            writer.writerows(selected)
    lines = ["# Bilan du tri des objets", "", f"Source : `{args.review}`", "",
             f"Décisions : {dict(counts)}. Objets sans décision : {len(known-reviewed)}.", "",
             "Les commentaires ci-dessous sont conservés tels quels. Les actions sont des propositions, pas des réparations effectuées.", "",
             "`selection.json` inventorie les objets retenus. Pour générer les trials, fournir l'export original à `--object-review` ; les limites de faces et de nombre d'essais s'appliquent ensuite.", "",
             "## Répartition par superclasse", "", "| Classe | Retenus | À réparer | Écartés | À examiner |", "|---|---:|---:|---:|---:|"]
    for label, totals in sorted(by_class.items()):
        lines.append(f"| {label} | {totals['retained']} | {totals['repair']} | {totals['excluded']} | {totals['pending']} |")
    for row in rows:
        if row["status"] not in {"repair", "excluded"}:
            continue
        lines.extend(["", f"## {row['name']} — {row['class']}", "", f"Identifiant : `{row['object_id']}` ; statut : `{row['status']}`.", "",
                      f"Motif : {row['reason']}", "", "Note originale :", "", *["> " + line for line in row['note'].splitlines()], "",
                      "Action proposée : " + row["proposed_action"]])
    (target / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    assert hashlib.sha256(args.review.read_bytes()).hexdigest() == selection["source_sha256"]
    print(json.dumps(selection["counts"]))
    print(target)


if __name__ == "__main__":
    main()
