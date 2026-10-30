# Pokémon Legends Z-A — compagnon Eden Duo 0.9.0

Développé et compilé sous Windows, puis exporté vers Android ARM64 avec le NDK r30 (API24, c++_static). Les archives Windows et Android sont distinctes : importer celle de sa plateforme. Hôte Eden Duo runtime 19+ avec lecture de sauvegarde et commandes virtuelles.

## Installation

Dans Eden, installer l’archive `.dsmod.zip` correspondant à sa plateforme, activer le second écran puis relancer le jeu. Les archives sont dans `companions/dist-za/` et portent le suffixe `windows-x86_64` ou `android-arm64-v8a`.

Title ID : `0100F43008C44000`. Les données en direct nécessitent l’exécutable **Z-A 2.0.2**, build `B1F12FD919EAE86A`. Installer une mise à jour ne suffit pas : elle doit être activée pour le jeu lancé. Le build `7222E13ECF6ADB32` correspond à 1.0.0/1.0.1 : la sauvegarde reste lisible mais les PV, le pointeur et Envoyer ne sont pas pris en charge. Si Eden transmet un identifiant périmé, le lecteur peut reconnaître 2.0.2 en vérifiant neuf pointeurs relatifs dans trois tables indépendantes du programme ; il refuse toute autre structure.

## Fonctionnalités

- Équipe : sprites normaux/chromatiques selon chaque Pokémon, noms au-dessus du niveau, XP, PV de combat confirmés, fiche des quatre attaques avec icônes, types écrits et couleurs. Le bouton flèche carré est séparé des attaques. Envoyer sélectionne le Pokémon par les commandes du jeu avant de l’envoyer ; fermer les menus en jeu pour l’utiliser.
- Pokédex : catalogues d’Illumis et de Méga-Dimension issus des ressources du jeu, liste défilante à gauche et fiche à droite, filtres capturés/manquants/vus/en stock. Le catalogue du DLC nécessite ses ressources activées.
- Boîtes : portraits, navigation, sélection d’un Pokémon puis d’un membre de l’équipe à échanger. Ouvrir les boîtes dans le jeu ; le contrôleur vérifie les identités, le curseur et la prise avant de confirmer par les boutons du jeu.
- Sac : liste défilante, grandes images, quantités, descriptions et catégories ; les CT affichent le nom, le type et la description de leur attaque.
- Missions : principales, Extras et secondaires, actives mises en évidence, terminées grisées, liste défilante de grandes cartes. Les détails et objectifs peuvent défiler verticalement pour conserver tout le texte.
- Carte : plan d’Illumis, contours verts et numéros des vingt zones, neuf centres Pokémon et position du joueur en direct à l’extérieur. Toucher une zone affiche ses Pokémon, niveaux et indications de capture à droite. Les centres ouvrent une fiche et l’accès à la carte du jeu. Le choix automatique du centre pour le voyage rapide est encore en cours de validation.
- Bords arrondis sur les cartes, boutons et panneaux des six pages.

Les PV et la position sont lus en mémoire sans écriture ; les lectures transitoires des PV conservent la dernière valeur dont l’identité et le checksum sont valides. La sélection et l’envoi utilisent les commandes virtuelles d’Eden. Le sac suit la sauvegarde ; les boîtes utilisent aussi un relevé mémoire validé pendant un échange. Les textes et images sont décodés à l’exécution depuis le romfs personnel du joueur ; aucun art extrait du jeu n’est inclus dans les archives. La police DejaVu Sans est fournie avec sa licence.

## Validation et limites

La 0.9.0 corrige le crash au démarrage de la 0.8.3 : les relevés CPU utilisent les pages mémoire directement, sans provoquer un téléchargement GPU pendant la recherche. Les tests couvrent les pages non mappées, les données transitoires, les catalogues régionaux, les descriptions d’objets/CT, les échanges avec retour d’état et les signatures de version. Android 0.8.2 avait été validé par le joueur (PV 6/6, position, sélection et envoi) ; le nouveau paquet nécessite un nouvel essai sur téléphone.

Les échanges automatiques et le voyage rapide restent à valider en situation sur les deux plateformes. Une commande refusée ou un contexte non reconnu arrête la séquence. Aucun Pokémon n’est déplacé par écriture directe de sauvegarde ou de mémoire.

## Origine des données

Les métadonnées factuelles des CT et des rencontres sont dérivées de [PKHeX](https://github.com/kwsch/PKHeX), sous GPL-3.0 ; le générateur et l’attribution sont conservés avec les sources. Les noms, textes et illustrations sont lus dans les ressources personnelles du jeu.

## Recompiler

Depuis la racine du dossier :

```powershell
.\companions\build-za-release.ps1 -Python 'chemin\vers\python.exe' -Ndk 'C:\android\ndk\android-ndk-r30'
```

Ajouter `-WindowsOnly` pour compiler et empaqueter uniquement Windows.
