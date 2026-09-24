# Nettoyer et normaliser NEXTLIFE : protocole de curation

Recherche documentaire du 10 septembre 2026. Les recommandations ci-dessous sont
une adaptation à NEXTLIFE, pas une norme publiée ni des seuils validés pour cette expérience.

## Ce que montrent les travaux

| Source primaire | Apport | Conséquence proposée pour NEXTLIFE |
| --- | --- | --- |
| Chang et al., 2015, [ShapeNet](https://arxiv.org/abs/1512.03012) | Organisation sémantique, annotations riches, effort de standardisation des objets. | Conserver les classes source et une superclasse expérimentale, notamment Cat ; documenter les conventions d'orientation. |
| Deitke et al., 2023, [Objaverse-XL](https://objaverse.allenai.org/objaverse-xl-paper.pdf) | Collection hétérogène à grande échelle et annotations esthétiques de rendus pour faciliter le filtrage. | Les scores de rendu peuvent aider à prioriser, mais ne définissent pas l'exploitabilité expérimentale. |
| Boborzi et al., 2025, [MeshFleet](https://arxiv.org/html/2503.14002v1), prépublication consultée | Étiquetage humain de rendus multivues, classifieur DINOv2/SigLIP, estimation d'incertitude et inspection finale. Les scores esthétiques génériques correspondent peu aux critères spécialisés étudiés. | Automatiser une présélection exige des exemples annotés selon NOS critères. Les résultats obtenus sur les véhicules ne prouvent pas une généralisation aux 13 classes NEXTLIFE. |
| Lin et al., 2025, [Objaverse++ — dépôt des auteurs](https://github.com/TCXX/ObjaversePlusPlus) | Annotations portant notamment sur structure géométrique et textures, pour une sélection adaptée aux besoins. | Distinguer défaut technique, qualité des matériaux et adéquation sémantique au lieu d'un score global unique. |
| Gebru et al., [Datasheets for Datasets](https://arxiv.org/abs/1803.09010), 2018/2021 | Documentation de la composition, de la collecte, des usages et de la maintenance. | Publier la politique de sélection, les exclusions, la provenance et les limites avec chaque version. |

Ces études portent principalement sur les données 3D et/ou la génération. Elles
ne valident pas directement un protocole de mesure de sécurité visuelle.

## Peut-on tout automatiser ?

Les fichiers absents, erreurs d'indices, coordonnées non finies, références de
textures manquantes, tailles et doublons binaires sont automatisables. L'audit
local réalise une partie de ces contrôles et signale les cas à examiner.
Un OBJ identique ne prouve pas un objet texturé identique ; un hash différent ne
prouve pas une géométrie différente.

Les composantes déconnectées, trous, auto-intersections, UV dégénérés et doublons
géométriques proches nécessiteraient des contrôles supplémentaires. Leur présence
n'est pas toujours un motif d'exclusion pour le rendu : une surface ouverte peut
être légitime. Ces contrôles ne sont pas implémentés par l'audit actuel.

La complétude sémantique, la reconnaissabilité et la pertinence de la classe demandent
une validation humaine. Des modèles multimodaux peuvent proposer des labels ou
prioriser une file d'attente, mais doivent être évalués sur des exemples annotés
localement. Avec 125 originaux, privilégier d'abord une revue assistée exhaustive
plutôt que l'entraînement d'un classifieur de qualité sans jeu de validation.

## Procédure proposée

1. **Figer les entrées.** Conserver les originaux ; noter version du catalogue,
   configuration de génération, identifiants, licences/provenance et empreintes.
   Ne jamais puiser des variantes dans Old/.
2. **Auditer.** Exécuter `audit_original_objects.py --hash-geometry`, importer le
   rapport dans `object_triage.html` et commencer par les alertes. Le seuil de
   150 000 faces est un indicateur configurable, pas une règle scientifique.
3. **Revoir les originaux.** Même fond neutre et même viewer ; inspecter au moins
   quatre directions avec la rotation de 90 degrés, et dessus/dessous si pertinent.
   Ces directions sont relatives à l'asset, pas des faces sémantiques déjà alignées.
   Vérifier : objet complet, label cohérent, matériaux attendus, navigation utilisable.
4. **Décider et tracer.** Retenu, à réparer, écarté ou à examiner ; motif/note pour
   les rejets et réparations. Faire revoir les cas ambigus par un collègue avec des
   annotations indépendantes, puis résoudre les désaccords et documenter les règles.
5. **Vérifier la référence perceptive.** Avant l'expérience, réaliser un petit pilote
   de reconnaissance des originaux sans afficher le label attendu. Définir le seuil
   d'acceptation et les conditions du pilote avant de consulter les réponses aux
   distorsions ; examiner les confusions par classe. Aucun seuil universel n'est
   établi ici.
6. **Contrôler les variantes séparément.** Leur famille/profil est visible dans
   l'outil. Distinguer échec de chargement et perte de reconnaissance voulue.
   Ne pas sélectionner les objets selon la réussite de leurs distorsions : cela
   pourrait biaiser les résultats de sécurité. Le classement perceptif des variantes
   reste dans `dataset_explorer.html` ; le tri dédié ne crée pas de note de sécurité.
7. **Normaliser dans une copie dérivée.** Fixer unités, axe vertical, orientation,
   centrage et convention de cadrage ; enregistrer les transformations et vérifier
   les UV/matériaux. Ces transformations ne sont pas effectuées automatiquement.
   Une simplification ou une réduction de texture altère déjà le signal de référence :
   la documenter comme prétraitement et régénérer les variantes à partir de cette
   référence figée. Ne pas confondre cadrage du viewer et normalisation des fichiers.
8. **Versionner la sélection.** Exporter le tri, compter les retenus par superclasse,
   documenter les déséquilibres et la couverture objet × méthode × niveau. Si des
   partitions sont utilisées, garder un objet et toutes ses variantes ensemble,
   ainsi que ses doublons connus. Distribuer les rapports avec la version de données.

## Ce qui est disponible dans le repo

- `object_triage.html/js/css` : interface dédiée, raccourcis, progression, filtres,
  choix original/variante et type de distorsion affiché.
- `object_review.js` : décisions locales, import/export compatible avec les anciennes
  décisions, historique des révisions et contexte de la version inspectée.
- `audit_original_objects.py` : contrôle technique et empreintes OBJ optionnelles,
  rapports horodatés sans modification des assets.
- `build_recognition_experiment.py --object-review ...` : sélection des seuls objets
  retenus, puis application des contraintes existantes de génération de trials.

Limites : le chargement réussi de l'OBJ ne garantit pas l'arrivée de chaque texture,
la mesure affichée de chargement n'est pas un benchmark FPS, et aucune validation
humaine n'est déduite automatiquement du fait d'avoir affiché l'original.
