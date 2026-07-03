# Protocole no-reference: degradation et destruction perceptive

## Objectif

Evaluer chaque objet distordu sans afficher la reference. La note ne mesure plus la ressemblance avec un original, mais le niveau de destruction perceptive: l'objet reste-t-il reconnaissable, sa categorie reste-t-elle devinable, et des details exploitables subsistent-ils?

Ce protocole complete l'experience DSIS solo. Il sert a estimer un niveau de protection perceptive utilisable quand la reference n'est pas disponible pour l'observateur.

## Presentation

- Un seul objet distordu est affiche a la fois.
- L'objet tourne lentement sur 360 degres, dans une fenetre 960 x 960.
- La scene, l'eclairage, la vitesse de rotation et l'echelle de rendu restent fixes.
- Le nom de l'objet, sa categorie et la famille de distortion ne sont pas affiches pendant la notation.
- L'ordre des objets et distortions est randomise.
- Une pause est faite toutes les 10 a 15 observations.

## Question posee

Evaluer le niveau de destruction visuelle de l'objet en fonction de sa reconnaissabilite.

## Echelle de destruction 0-5

- 0: Aucune destruction visible. L'objet est intact ou quasi intact.
- 1: Destruction faible. L'objet est immediatement reconnaissable et les details principaux restent lisibles.
- 2: Destruction moderee. L'objet est reconnaissable, mais plusieurs details deviennent incertains.
- 3: Destruction forte. La categorie generale reste reconnaissable, mais l'identite precise et les details sont peu fiables.
- 4: Destruction tres forte. La categorie est seulement devinable avec hesitation; les details exploitables sont rares.
- 5: Destruction opaque. L'objet n'est pas reconnaissable et les details exploitables sont absents.

## Correspondance avec les niveaux de securite perceptive

- 0: Original.
- 1: Transparent.
- 2: Suffisant bas.
- 3: Suffisant haut.
- 4: Confidentiel.
- 5: Opaque.

Si quatre niveaux seulement sont conserves, regrouper 4 et 5 dans Confidentiel.

## Donnees a collecter

Pour chaque stimulus:

- observer_id
- session_id
- date
- object_id masque
- scene_id
- distortion_family masquee pendant la notation
- distortion_profile masque pendant la notation
- distorted_path
- destruction_0_5
- recognizable_category, oui/non/incertain
- recognizable_object, oui/non/incertain
- dominant_artifact
- comment
- elapsed_ms

## Gabarit CSV

```csv
observer_id,session_id,date,stimulus_id,object_id,scene_id,distortion_family,distortion_profile,distorted_path,destruction_0_5,recognizable_category,recognizable_object,dominant_artifact,comment,elapsed_ms
```

## Analyse minimale

Pour chaque famille de distortion:

- moyenne de destruction;
- ecart-type;
- pourcentage des notes 4 ou 5;
- taux de categorie reconnaissable;
- taux d'objet precis reconnaissable;
- niveau de securite perceptive attribue.

Une famille est consideree exploitable dans un premier resultat si elle contient au moins 15 observations, au moins 3 objets differents, et si les divergences fortes sont documentees par commentaire.

## Interpretation

Le score no-reference peut etre utilise comme une estimation directe de protection:

```text
D = destruction_0_5
```

Lecture:

- D proche de 0: protection nulle.
- D proche de 2: degradation visible mais usage perceptif encore possible.
- D proche de 3: compromis interessant entre reconnaissance globale et masquage des details.
- D proche de 4 ou 5: protection forte, mais perte importante d'utilisabilite visuelle.

Pour croiser avec l'experience DSIS, comparer `D` avec `distance_DSIS_0_5`. Les cas ou les deux divergent doivent etre inspectes manuellement: ils indiquent souvent une distortion qui preserve la silhouette mais detruit les textures, ou inversement.
