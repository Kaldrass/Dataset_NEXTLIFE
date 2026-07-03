# Experience reconnaissance / securite visuelle

Cette experience affiche un seul objet distordu, manipulable librement en rotation et zoom.
Le sujet doit choisir:

- le label ImageNet correspondant parmi des distracteurs;
- un niveau de securite visuelle parmi `Original`, `Transparent`, `Suffisant`, `Confidentiel`.

Les classes ImageNet de chats sont regroupees sous le label `Cat`, car l'experience cherche l'espece reconnue et non la race.

L'objet est affiche dans la scene 3D associee a `scene_id` quand `Scenes/<scene_id>/scene.gltf` existe. Si une scene manque ou ne charge pas, l'interface utilise automatiquement un decor procedural de secours.

## Lancer

Depuis la racine du projet:

```powershell
python ExperimentSecurity\build_recognition_experiment.py --max-objects 25 --max-trials 200 --max-faces 150000 --choices 6 --seed 20260622
python -m http.server 8015 --bind 127.0.0.1
```

Puis ouvrir:

```text
http://127.0.0.1:8015/ExperimentSecurity/recognition_viewer.html
```

## Resultats

Les resultats sont exportables depuis la page en CSV ou JSON.

Pour agreger plusieurs sujets:

```powershell
python ExperimentSecurity\analyze_recognition_results.py path\to\subject1.csv path\to\subject2.json --out-dir ExperimentSecurity\results
```

Le script ecrit:

- `recognition_security_summary.csv`: taux de reconnaissance et repartition des niveaux par essai;
- `recognition_security_barplot_counts.csv`: table longue directement utilisable pour des barplots.

## Explorateur de la base

L'explorateur interne permet de parcourir les 125 objets et leurs variantes sans modifier l'experience participant. Il propose:

- une selection persistante des objets;
- un affichage par objet ou par distorsion;
- trois perimetres: les 9 profils de securite, les 28 profils canoniques ou toutes les variantes presentes;
- une comparaison dans la scene associee ou sur fond neutre;
- un classement local `Original`, `Transparent`, `Suffisant`, `Confidentiel` avec notes;
- un export CSV ou JSON des classements.

Regenerer le catalogue apres toute modification du dataset:

```powershell
python ExperimentSecurity\build_dataset_catalog.py
```

Puis ouvrir:

```text
http://127.0.0.1:8015/ExperimentSecurity/dataset_explorer.html
```
