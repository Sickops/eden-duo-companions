# Z-A — état de la version 0.3.0 (28 octobre 2026)

## Livré

- Paquet Windows x64 + Android ARM64 :
  `dist-za/0100F43008C44000-PokemonLegendsZA-0.3.0.dsmod.zip` (331 062 octets).
- SHA-256 : `f197709199bb8c29bf9e48d0bf32362d4bd5ff7d75ba9ed958cdc961536dfbd6`.
- Six onglets : Équipe, Pokédex, Boîte, Carte, Sac, Dresseur.
- La correction des images est dans le module : pas de nouvelle compilation de l'émulateur nécessaire.

## Cause de la panne des images et de la carte

La version de base extraite utilise `data.trpfd` et `data.trpfs` à la racine.
Le romfs du jeu mis à jour en 2.0.2 les place sous `arc/`. La version 0.2.0 ne
cherchait qu'à la racine et conservait son premier échec. La 0.3.0 détecte les
deux dispositions et reprend le chargement après deux secondes si le romfs
n'était pas encore disponible. Un indicateur de chargement apparaît dans l'en-tête.

Reproduction avec le vrai lecteur NCA/BKTR d'Eden : base
`461e1e40fb6a016a4e6cb76e7d5b482d.nca` + mise à jour
`4a70b3ff963bfe412681185cea68bf55.nca`. Le romfs fusionné mesure 5 989 104 840 octets ;
`arc/data.trpfd` mesure 9 877 520 octets et `arc/data.trpfs` 4 753 821 072 octets.
Aucune extraction de l'archive entière n'est nécessaire : les lectures restent par plages.

## Pokédex et Sac

- Les 1 025 espèces nationales sont accessibles dans les neuf générations.
- Listes de 18 entrées, pagination bornée, sélection visible de la génération et du filtre.
- Filtres : Tous, Capturés, Manquants, Vus seulement, En stock.
- L'historique du Pokédex et le stock actuel (équipe + toutes les boîtes) sont distincts.
- Le Sac lit les 3000 emplacements d'objets de 16 octets du bloc KItem, sans interpréter
  les indicateurs de poches situés après ces enregistrements comme des objets.
- Liste de 16 objets par page : nom français, quantité, catégorie, favori et état équipé.
- Dans la copie de sauvegarde actuelle : dresseur adam, équipe de six, 221 Pokémon en boîte,
  242 capturés, 264 vus, 212 types d'objets possédés ou équipés.
- Les tables de noms sont décodées via leurs en-têtes et offsets, sans positions figées propres
  à la version de base. La police couvre maintenant Latin-1 et Latin Extended-A (352 glyphes).

## Vérifications effectuées

- Test synthétique de sauvegarde : états Pokédex, stock actuel et inventaire ; objets sans
  quantité ignorés, objets équipés conservés, poche invalide et données de queue ignorées.
- Banc ABI sur la DLL extraite du ZIP final : neuf générations complètes, 1 025 entrées
  uniques, filtres corrects, pages bornées ; 212 objets accessibles sans doublons et répartition
  complète dans les catégories. Changement/absence/corruption de sauvegarde et navigation
  des boîtes restent vérifiés.
- Test de ressources de base : noms d'espèces et d'objets, icônes et carte ; récupération après
  indisponibilité initiale du romfs vérifiée.
- Rendu des six pages du ZIP final avec le vrai moteur d'Eden, les deux NCA fusionnées et une
  copie de la sauvegarde actuelle. Les six icônes d'équipe sont exigées par le banc de rendu.
- Aperçus examinés : `za-validation/release-preview-v3/` ; filtres Gen 6 / Manquants et CT
  examinés dans `za-validation/filtered-preview/`.
- Compilation MSVC Windows puis export Android NDK r30 depuis Windows.
- ELF Android AArch64 : trois exports attendus ; libc, libm et libdl uniquement.
- Deux sommes SHA-256 de bibliothèques conformes au manifeste ; sept membres dans le ZIP,
  aucun fichier de jeu ni clé fourni, licence de police incluse.

## Limites et validation encore manquante

- L'ouverture d'Eden via Computer Use a expiré faute d'autorisation. Le test interactif dans
  une fenêtre n'est pas revendiqué. Les preuves ci-dessus proviennent du banc utilisant le
  lecteur NCA et le rendu réels d'Eden, pas d'un faux lecteur de répertoire uniquement.
- L'export Android n'a pas encore été exécuté sur appareil réel.
- La carte affiche le plan et les coordonnées sauvegardées ; le repère joueur reste à calibrer.
- Les données suivent les sauvegardes, pas la mémoire de jeu en direct.
- Les espèces absentes du catalogue graphique du jeu n'ont pas d'icône, même si elles figurent
  dans la liste nationale complète.

## Installation

Réimporter le ZIP versionné dans **File > Install Dual Screen Mod...**, puis relancer le jeu.
Le nom du compagnon est conservé : l'installateur remplace la version 0.2.0.
Guide : `eden-duo-companions-super-mario-odyssey-1.0.0/packages/PokemonLegendsZA/README.md`.
Les journaux et fixtures de développement restent dans `za-validation/` et ne sont pas distribués.
