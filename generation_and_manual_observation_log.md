# Journal de generation et protocole d'observation manuelle

Date locale: 2026-06-11 15:25:12 +02:00
Racine analysee: C:\D\These\BDD\Dataset_NEXTLIFE

## Journal des actions

### Prise en compte du projet

Actions realisees:

- Lecture de la racine du projet.
- Lecture de README.md.
- Inspection de DistortionConfig\unified_generation_config.json.
- Inventaire des scripts Python disponibles.
- Comptage des branches existantes dans Objects\Distorted.

Constats principaux:

- Le README documente le point d'entree: python Scripts\orchestrate_distortion_generation.py
- Ce fichier est absent dans la copie analysee.
- Le dossier Scripts contient seulement distortion_pipeline\methods\mesh et distortion_pipeline\methods\texture, sans fichier Python actif.
- Les seuls scripts Python trouves sont sous Old\Scripts.
- Les scripts herites utilisent des chemins absolus vers D:\These\BDD\Dataset_NEXTLIFE\Dataset_NEXTLIFE\..., distincts de la racine analysee C:\D\These\BDD\Dataset_NEXTLIFE.
- La configuration active indique dry_run: true, overwrite_existing: false, max_objects: 1, max_recipes: 10.
- Le jeu actif contient deja 125 dossiers dans Objects\Originals, 125 dans Objects\Distorted\MeshVariants, 125 dans Objects\Distorted\TextureVariants et 125 dans Objects\Distorted\CombinedVariants.

### Commandes de generation lancees

#### 1. Point d'entree documente

Commande:

```powershell
python Scripts\orchestrate_distortion_generation.py
```

Resultat:

- Echec: le fichier Scripts\orchestrate_distortion_generation.py n'existe pas dans cette copie.
- Aucune generation active n'a pu etre lancee via le point d'entree documente.

#### 2. Script texture herite

Commande:

```powershell
python Old\Scripts\generate_texture_distortions.py
```

Resultat:

- Le script s'est execute contre son ancienne racine absolue: D:\These\BDD\Dataset_NEXTLIFE\Dataset_NEXTLIFE.
- Le fichier D:\These\BDD\Dataset_NEXTLIFE\Dataset_NEXTLIFE\DistortionConfig\texture_distortions.json a ete mis a jour le 2026-06-11 a 15:23:56.
- Cette execution ne cible pas directement la racine analysee C:\D\These\BDD\Dataset_NEXTLIFE.

#### 3. Script geometrie herite

Commande:

```powershell
python Old\Scripts\generate_geometry_drc.py
```

Resultat:

- Le script a commence a travailler sur l'ancienne racine absolue D:\These\BDD\Dataset_NEXTLIFE\Dataset_NEXTLIFE.
- La commande a ete interrompue par la limite de commande de 10 secondes pendant une serie Draco sur 198685_jar_with_dragon_design.
- Aucun processus python, blender ou draco_encoder n'est reste actif apres l'interruption.
- Le CSV D:\These\BDD\Dataset_NEXTLIFE\Dataset_NEXTLIFE\DistortionConfig\geometry_generation_log.csv n'a pas ete mis a jour aujourd'hui; sa derniere modification observee date du 2026-03-19 17:54:12.

### Conclusion technique

La pipeline active decrite par le README est incomplete dans cette copie: les scripts principaux manquent. Les scripts herites peuvent se lancer, mais ils ciblent une ancienne arborescence absolue et ne doivent pas etre consideres comme une generation propre de la racine C:\D\These\BDD\Dataset_NEXTLIFE.

Pour relancer proprement la generation dans cette copie, il faut restaurer ou recreer au minimum:

- Scripts\orchestrate_distortion_generation.py
- Scripts\generate_distorted_variants.py
- les modules de Scripts\distortion_pipeline
- les wrappers Scripts\generate_<method>.py

## Protocole DSIS pour observations manuelles

### Objectif

Obtenir des resultats modestes mais exploitables sur la lisibilite visuelle et le niveau de securite perceptive des objets 3D distordus. L'objectif est de relier une observation humaine structuree a une metrique de securite multi-niveaux.

### Principe DSIS adapte aux objets 3D

1. Presenter l'objet original comme reference.
2. Presenter ensuite une version distordue du meme objet, dans la meme scene, avec le meme point de vue, le meme eclairage et la meme echelle.
3. Demander a l'observateur de noter la degradation, l'identifiabilite et la fuite d'information.
4. Repeter sur un echantillon reduit mais equilibre d'objets, de scenes, de types de distortion et de niveaux.

### Conditions d'observation

- Meme ecran pendant toute la session.
- Resolution, luminosite, scene, point de vue et echelle fixes.
- Pas de zoom manuel, sauf condition separee appelee inspection libre.
- Ordre randomise des objets et des distortions.
- Pause courte toutes les 10 a 15 comparaisons.
- L'observateur ne voit ni le nom de la distortion ni ses parametres.
- Idealement 3 observateurs minimum; sinon, 2 passes separees dans le temps.

### Echantillonnage minimal recommande

Version courte:

- 5 objets.
- 4 familles de distortions.
- 3 niveaux par famille.
- 1 vue principale et 1 vue detail.

Version preferable:

- 10 objets.
- 3 familles texture: resize/JPEG, blur, encryption ou block obscuration.
- 3 familles mesh: simplification, quantization, encryption ou data hiding.
- 3 niveaux par famille quand disponibles.
- 3 vues fixes par objet: face, trois-quarts, detail.

### Fiche de notation

Pour chaque paire original/distordu:

- Degradation visuelle globale, note 1 a 5:
  - 5: imperceptible ou quasi imperceptible.
  - 4: visible mais faible.
  - 3: visible et moyenne.
  - 2: forte.
  - 1: tres forte ou destructrice.
- Identifiabilite de l'objet, note 1 a 5:
  - 5: objet reconnu immediatement.
  - 4: reconnu avec faible hesitation.
  - 3: categorie reconnue, details incertains.
  - 2: categorie incertaine.
  - 1: objet non identifiable.
- Fuite d'information sensible, note 1 a 5:
  - 5: details sensibles lisibles.
  - 4: beaucoup de details lisibles.
  - 3: details partiellement lisibles.
  - 2: peu de details exploitables.
  - 1: details non exploitables.
- Artefact dominant: texture floue, blocs visibles, couleurs alterees, geometrie simplifiee, trous/surfaces cassees, silhouette modifiee, autre.
- Commentaire court: une phrase maximum.

### Metrique de securite perceptive

Score propose:

```text
S = 0.45 * (6 - fuite_information)
  + 0.35 * (6 - identifiabilite)
  + 0.20 * (6 - degradation_visuelle_globale)
```

Plus S est eleve, plus la protection perceptive est forte.

### Niveaux proposes

- Original: aucune protection perceptive; objet et details pleinement accessibles.
- Transparent: distortion faible; usage visuel conserve, securite faible.
- Suffisant: distortion visible; les details sensibles commencent a etre limites, objet encore exploitable.
- Confidentiel: distortion forte; reconnaissance ou details sensibles fortement reduits.
- Opaque: distortion tres forte; objet difficilement identifiable, details sensibles non exploitables.

Si seuls 4 niveaux sont souhaites, fusionner Opaque avec Confidentiel.

### Seuils initiaux

- S < 1.8: Original ou Transparent.
- 1.8 <= S < 2.8: Transparent.
- 2.8 <= S < 3.6: Suffisant.
- 3.6 <= S < 4.4: Confidentiel.
- S >= 4.4: Opaque.

Ces seuils sont provisoires et devront etre recalibres apres une campagne pilote.

### Analyse minimale exploitable

Pour chaque distortion, produire:

- moyenne du score S;
- ecart-type du score S;
- moyenne de l'identifiabilite;
- moyenne de la fuite d'information;
- nombre d'observations;
- niveau attribue selon les seuils;
- un exemple representatif par niveau.

Un resultat est exploitable si:

- au moins 15 observations existent pour une famille de distortion;
- au moins 3 objets differents sont presents;
- l'ecart-type du score S reste inferieur ou egal a 1.0, ou les divergences sont discutees manuellement.

### Gabarit CSV conseille

```csv
observer_id,session_id,date,object_id,scene_id,view_id,distortion_family,distortion_profile,reference_path,distorted_path,degradation_1_5,identifiability_1_5,information_leakage_1_5,dominant_artifact,comment,security_score,security_level
```

### Suite recommandee

1. Restaurer ou recreer la pipeline active de generation.
2. Generer un sous-ensemble pilote reproductible.
3. Produire les rendus originaux/distordus avec vues fixes.
4. Faire une premiere campagne DSIS sur 5 objets.
5. Ajuster les seuils des niveaux.
6. Etendre progressivement le nombre d'objets et de familles de distortions.
