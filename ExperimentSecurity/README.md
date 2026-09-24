# Experience reconnaissance / securite visuelle

## Régénérer après un nettoyage des références

Le script `regenerate_reviewed_dataset.py` lit le tri final, copie les seuls objets
retenus dans une nouvelle version sous `Objects/Releases/`, recalcule leurs masques
UV et génère les 28 profils dans cette version. Les anciens assets sont conservés.
Un objet pilote doit réussir avant de lancer les autres. Les logs par objet et les
empreintes des fichiers source sont enregistrés dans la version produite.

```powershell
python ExperimentSecurity\regenerate_reviewed_dataset.py ExperimentSecurity\results\nextlife_object_review_final.json --workers 2
```

Après une interruption, conserver les mêmes variables d'environnement des
opérateurs et reprendre la version existante :

```powershell
python ExperimentSecurity\regenerate_reviewed_dataset.py ExperimentSecurity\results\nextlife_object_review_final.json --release Objects\Releases\NOM_DE_LA_VERSION --resume --workers 2
```

La reprise vérifie le tri et les empreintes des références figées, conserve les
objets ayant leurs 28 résultats réussis et archive les tentatives incomplètes
dans `previous_attempt_*` avant de les régénérer. Aucun asset n'est supprimé.

Les opérateurs Linux doivent être compatibles avec la distribution WSL installée.
Pour sélectionner des exécutables recompilés sans remplacer ceux du dépôt, définir
`NEXTLIFE_OPERATOR_BIN_DIR` (dossier contenant `aes_image_tool`, `masked_blur`,
`block_all_operation`) et `MESH_ENCRYPTION_EXE` (chemin de l'exécutable maillage).
Blender et Draco sont détectés par les chemins habituels ou les variables existantes.
Sous Ubuntu/WSL, les dépendances de compilation sont `libopencv-dev`,
`libcrypto++-dev`, `libboost-iostreams-dev`, `pkg-config` et `g++`.
`bash ExternalDistortions/build_local_operators.sh` crée les quatre exécutables
dans un nouveau dossier `.tmp/operators-*` sans remplacer les anciens binaires.

Une fois la génération complète et sans erreur :

```powershell
python ExperimentSecurity\activate_dataset_release.py Objects\Releases\NOM_DE_LA_VERSION
```

L'activation vérifie la sélection, les empreintes, la couverture et les chemins.
Elle sauvegarde les anciens catalogues/trials dans `activation_*/previous_*` avant
d'installer les nouveaux. Le catalogue pointe alors sur les références figées de
la version, et non sur `Objects/Originals/`. Pour appliquer de futures réparations,
repartir des références de travail et produire une nouvelle version.

`dataset_release.json`, ignoré par Git, mémorise les chemins actifs pour les futurs
builds du catalogue et des trials. L'activation prépare tous les trials disponibles,
sans plafond de faces/objets/essais : ce fichier est un inventaire complet, à
sous-échantillonner pour une session participant selon le protocole choisi.
Les comptes de faces du catalogue activé sont recalculés sur les références figées.
L'expérience DSIS historique n'est pas migrée automatiquement.

Les classements de distorsions et les réponses participant sont stockés par
version du dataset dans le navigateur. Les anciens résultats locaux sont
conservés, mais ne sont pas réutilisés pour les objets régénérés. Les nouveaux
exports portent le champ `dataset_release`. Les décisions de tri des objets
restent indépendantes de ces classements.

## Trier les objets originaux

1. Lancer l'audit technique, sans modifier les assets :

   ```powershell
   python ExperimentSecurity\audit_original_objects.py
   ```

   Les rapports JSON et CSV horodatés sont créés dans `ExperimentSecurity/results/`.
   L'audit parcourt uniquement `Objects/Originals/`, jamais `Old/`. Il compte les
   sommets/faces, détecte les OBJ absents, certains défauts de géométrie et les
   références MTL/textures absentes. Les options complexes MTL sont signalées
   pour contrôle manuel. Le seuil `--max-faces 150000` est une alerte de performance,
   pas une exclusion. Ce contrôle ne prouve ni la qualité du rendu, ni la
   reconnaissabilité, ni l'absence de doublons ou de parties manquantes.

2. Ouvrir l'outil dédié [object_triage.html](object_triage.html) via le serveur HTTP
   décrit ci-dessous. Importer le JSON de l'audit dans le panneau de décision.
   L'explorateur `dataset_explorer.html` conserve uniquement le classement des distorsions.
3. Examiner chaque original sous plusieurs angles, puis choisir **À examiner**,
   **Retenu**, **À réparer** ou **Écarté de l'expérience**. Enregistrer la décision.
   Un motif ou une note est obligatoire pour réparation/exclusion. Le tri porte
   sur l'objet, indépendamment des notes de sécurité propres aux variantes.
   Un original sans variante affichable reste accessible dans la liste pour le tri.
4. Exporter régulièrement **le tri JSON**. La sauvegarde locale dépend du navigateur
   et de l'adresse du serveur. L'import fusionne les objets non décidés et conserve
   les décisions locales déjà prises ; il ne remplace pas silencieusement celles-ci.
   L'audit importé doit être réimporté après rechargement de la page.
5. Placer l'export dans `ExperimentSecurity/results/nextlife_object_review.json`, puis :

   ```powershell
   python ExperimentSecurity\build_recognition_experiment.py --object-review ExperimentSecurity\results\nextlife_object_review.json --output ExperimentSecurity\results\recognition_trials_reviewed.json
   ```

   Seuls les objets `retained` peuvent produire des trials ; les limites habituelles
   de faces, objets et trials s'appliquent ensuite. Sans `--object-review`, le
   comportement historique est conservé. Une sélection vide/invalide ou sans trial
   exploitable produit une erreur avant toute écriture. Pour utiliser le résultat
   dans le viewer participant, fournir son chemin attendu avec
   `--output ExperimentSecurity\recognition_trials.json` (cela remplace le précédent
   fichier de trials). L'expérience DSIS reste indépendante et inchangée.

Le tri et les audits restent exclus de Git. Aucun objet n'est déplacé ou supprimé.
Après réparation, réauditer l'original et réexaminer la décision avant de le retenir.

### Revue rapide et normalisation

En fond neutre, le tri et l'explorateur cadrent automatiquement la géométrie
complète selon le champ de vision et le ratio du canvas, avec une marge de 12 %
dans le calcul de projection. Le cadrage est recalculé au chargement et au
redimensionnement ; rotation et zoom manuels restent possibles. Cela normalise
l'encombrement à l'écran, sans changer les fichiers ni les dimensions physiques.
Les décors inclus dans un OBJ sont également cadrés : ils doivent être nettoyés
séparément si l'objet principal apparaît trop petit. Les scènes contextuelles et
le cadrage du viewer participant ne sont pas modifiés par cet ajustement.

Depuis le 18 septembre 2026, le placement automatique au sol utilise le minimum
de la géométrie complète après rotation et mise à l'échelle, avec une marge de
0,015 unité du viewer. Les bornes robustes restent utilisées pour le calcul
d'échelle. Cela évite d'enterrer les pieds ou supports fins ignorés par les
quantiles. La correction concerne le tri, l'explorateur et le viewer participant.
Les placements explicites à l'origine dans les scènes conservent leurs coordonnées
calibrées. Les fichiers 3D et décisions de tri ne sont pas modifiés ; réexaminer
les objets signalés « rentre dans le sol » avant de changer leur statut.

L'outil dédié ouvre l'original, propose une file filtrable par classe, décision et
alertes d'audit, et passe au suivant après enregistrement (option désactivable).
Raccourcis hors des champs : `1` retenir, `2` préparer une réparation, `3` préparer
une exclusion, `4` à examiner, flèches précédent/suivant, `O` original, `V` rotation
de 90 degrés. Pour `2`/`3`, choisir le motif et enregistrer ; aucune exclusion sans motif.
Le sélecteur de version affiche famille, profil et identifiant des distorsions.
La décision reste celle de l'objet source. La rétention exige le chargement de
l'original pendant la visite actuelle ; cela ne remplace pas sa validation visuelle.
Les décisions précédentes sont récupérées via la même clé localStorage, à condition
de conserver l'adresse et le port. L'export ajoute un historique des révisions et la
version inspectée. Exporter avant de changer de navigateur ou d'adresse.

Pour rechercher aussi les OBJ strictement identiques :

```powershell
python ExperimentSecurity\audit_original_objects.py --hash-geometry
```

L'empreinte concerne le fichier OBJ seul, pas ses textures ; des quasi-doublons
peuvent lui échapper. Aucune décision de tri n'est appliquée automatiquement.
Voir [la revue de littérature et le protocole proposé](object_curation_protocol.md).

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
