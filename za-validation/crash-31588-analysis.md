## Crash du 29 octobre, PID 31588

Le jeu utilisait 0.5.0. Dump local : eden.exe.31588.dmp. Exception c0000005 ; memcpy source 0x600, taille 0x130. Le premier appelant symbolisé est DeviceMemoryManager<MaxwellDeviceTraits>::ReadBlockUnsafe+0xd1, suivi du cache de tampons uniformes Vulkan.

Le désassemblage de l'ancien exécutable confirme que +0xd1 est le retour du memcpy dans le chemin du cache t_slot. Le pointeur est testé puis relu séparément. Map/Unmap vident le même cache sans synchronisation avec le lecteur. Ceci explique une source nulle + décalage 0x600 ; ce scénario est fortement étayé, sans reproduction contrôlée du calendrier des threads.

Correction : suppression du cache partagé de quatre traductions, conservation de la résolution directe dans tracked_entries et du chemin WalkBlock pour les plages plus longues/non mappées. Reconstruction Eden en cours. Cette correction ne prouve pas l'absence d'autres causes de plantage.

Outil local inspect_crash.cpp : lecture du minidump et symbolisation PDB. La liste des adresses de pile est une recherche de pointeurs, pas un déroulement complet des frames. Le premier retour d'appel et son désassemblage corroborent le chemin fautif.
