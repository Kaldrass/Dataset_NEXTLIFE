# Experience DSIS solo

## Lancer

Depuis la racine du projet:

```powershell
python ExperimentDSIS\build_dsis_experiment.py --max-objects 10 --max-trials 120 --max-faces 150000 --seed 20260615
python -m http.server 8015 --bind 127.0.0.1
```

Puis ouvrir:

```text
http://127.0.0.1:8015/ExperimentDSIS/dsis_viewer.html
```

## Notation DSIS en distance

La page affiche deux rendus 960 x 960 sur le meme ecran:

- reference a gauche;
- distorsion a droite.

Les deux objets tournent lentement. Noter la distance perceptive entre la reference et la distorsion, de 0 a 5. Ici, 0 est le meilleur score:

- 0: aucune distance, identique ou quasi identique a la reference;
- 1: distance tres faible, differences a peine visibles;
- 2: distance faible, differences visibles mais l'objet reste tres proche;
- 3: distance moyenne, ressemblant, mais plusieurs changements nets;
- 4: distance forte, meme categorie possible, objet tres modifie;
- 5: distance maximale, plus de ressemblance exploitable.

Les resultats peuvent etre exportes en CSV ou JSON depuis la page.

Le bouton `Echelle` dans l'interface permet d'afficher ce rappel pendant l'experience. Les boutons 0 a 5 ont aussi une infobulle avec leur signification.

La note selectionnee est surlignee. Elle est sauvegardee des que le bouton est clique, puis restauree si l'on revient avec `Precedent`. Les reponses sont aussi conservees localement dans le navigateur; utiliser `Reset` pour repartir d'une session vide.

## Note sur la reference

Le script utilise une reference exploitable et complete pour l'affichage DSIS. Si l'original complet n'est pas disponible pour un objet, il peut utiliser une variante proxy faiblement degradee, selon la logique du generateur.

Le plan par defaut filtre aussi les objets au-dessus de 150000 faces pour garder une experience interactive. Ajuster `--max-faces` si besoin.

## Protocole complementaire

Le protocole sans reference pour evaluer le niveau de destruction perceptive est dans:

```text
ExperimentDSIS\protocole_degradation_sans_reference.md
```
