# Diagnostic 0.5 en cours

Host Windows : correction MainWindow::SetInputSubsystem, binaire compilé 28/10/2026 20:00.
Module : clés SCBlock 32 bits, padding 32 bits non nul confirmé dans live-block-table-v5.bin ; tests corrigés et réussis. Lecteur live validé avec DLL externe sur processus Eden réel, Aligatueur 243/258, coordonnées -86.055,-708.571.
Pokédex : 12 entrées, images 128 px, aperçus preview-v5/dex.png.
Paquet 0.5 construit Windows+Android et installé.
Essais 0.5 : jeu plante avant écran titre, PC=0, R30=0x81884D9C. Logs signalent aussi des lectures BKTR sans base avant démarrage. Aucune écriture mémoire dans le module.
Comparaison en cours : le dossier installé 0.5 contient temporairement le contenu complet 0.4. Le contenu 0.5 est conservé dans dualscreen-v5-test-backup ; package-v5-test-backup.json. Restaurer ce contenu ou réinstaller un paquet final après le diagnostic.
Sauvegarde du jeu jamais modifiée par les outils.
