# Reprise du compagnon Z-A — 28 octobre 2026

## Livré et vérifié

- Paquet 0.2.0 : `dist-za/0100F43008C44000.dsmod.zip`.
- Compilation MSVC Windows x64 puis export Android ARM64 avec le NDK r30, depuis Windows.
- Hôte Windows recompilé : `../eden-duo/build/bin/eden.exe`.
- Décodage romfs : TRPFD/TRPFS, trpak/Kraken, BNTX/BCn, carte SARC et noms français.
- Correspondance espèces/formes vers modèles lue dans le catalogue du jeu ; les identifiants
  des modèles de Kalos ne sont pas les numéros nationaux. Vérifié sur Marisson, Lépidonille,
  Prismillon, Fluvetin et Opermine, en plus des espèces des anciennes générations.
- Icônes et noms raccordés aux six membres de l'équipe et aux trente cases de chaque boîte.
- Statut, dresseur et secteur utilisent des liens de texte, au lieu des widgets numériques.
- Police DejaVu Sans et accents ; réduction des glyphes filtrée dans le moteur Windows.
- Grille des boîtes corrigée pour ne pas recouvrir le pied de page.
- Calcul chromatique corrigé : PID à 0x1C, distinct de la constante de chiffrement.
- Lectures de coordonnées bornées jusqu'à 0x30 ; sérialisation des rafraîchissements de sauvegarde.
- Archive contenant uniquement manifeste, métadonnées, police/licence et deux bibliothèques.

## Vérifications

- `dsmod-za-save` : passe, y compris les deux cas chromatiques synthétiques.
- `za_assets_test` : passe sur le romfs local ; carte 640×640 et icônes PNG produites.
- `dsmod-za-module-host-test` : passe sur la DLL extraite du ZIP livré et une copie de la vraie
  sauvegarde. 186 blocs, équipe de six, 57 capturés et 199 vus ; navigation, rafraîchissement,
  sauvegarde inchangée (32 octets relus), sauvegarde corrompue et absente vérifiés.
- `dsmod_preview` : cinq pages rendues par le vrai moteur d'Eden avec la DLL et le manifeste
  extraits du ZIP. Aperçus inspectés dans `za-validation/preview/`.
- Filtrage de texte : couverture alpha, teinte, découpage et rendu 1:1 vérifiés nativement.
- ELF Android AArch64 : trois exports ABI attendus ; dépendances libc/libm/libdl uniquement.
- SHA-256 des deux bibliothèques conformes au manifeste du ZIP.
- SHA-256 du ZIP : `4bcbaddd2557047bfb711cfc6c478cfa825544acfb4a69948b300b5e2d722ec2`.

Les journaux de cette reprise sont conservés dans `za-validation/`.
Les fichiers extraits et les copies de sauvegarde sont exclusivement des fixtures locales.
Les observations de tables binaires viennent des lecteurs locaux et du banc d'essai ; aucun
serveur REA connecté n'était disponible dans cette session et aucun résultat REA n'est revendiqué.

## Reste à valider / limites

- Pas encore d'essai de cette version dans une partie lancée ni sur un appareil Android réel.
  L'export Android est une bibliothèque de compagnon installable, pas un nouvel APK d'Eden.
- La carte affiche le plan et les coordonnées sauvegardées. Le repère précis du joueur demande
  encore une calibration du monde vers le plan ; l'ancien radar approximatif n'est pas affiché.
- Les données suivent les sauvegardes du joueur ; lecture de mémoire en direct non implémentée.
- Le Pokédex présente des compteurs nationaux, pas une encyclopédie régionale interactive.
- Ressources vérifiées sur le romfs de base local ; les deux builds déclarés ne sont pas
  chacun validés en jeu pendant cette reprise. Une espèce absente du catalogue n'a pas d'icône.

Voir `eden-duo-companions-super-mario-odyssey-1.0.0/packages/PokemonLegendsZA/README.md`
pour installer et recompiler.
