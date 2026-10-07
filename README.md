# Ville morte — scène OpenGL « Three Robots »

Projet de programmation graphique (OpenGL 3.3 core, C++17, GLFW / GLEW / GLM).

Une scène inspirée de *Three Robots* (*Love, Death & Robots*) : trois robots (XBOT 4000, LittleBot, 11-45-G) se tiennent sur l'étage d'une tour en ruine et contemplent un humain mort, présenté comme dans un musée. En contrebas, une ville morte : routes fissurées, trottoirs envahis par les mauvaises herbes, lampadaires hors d'usage, bâtiments en ruine, nuit, brouillard.

Le programme principal est [`Scene_AllModels.cpp`](Scene_AllModels.cpp). Les autres fichiers `Lighting_*.cpp` sont les exercices de cours sur l'éclairage (Phong, point, spot, matériaux…), conservés.

## Lancer

Dépendances : un compilateur C++17, GLFW 3, GLEW, GLM, `make`, `pkg-config`.

Sous Windows (MSYS2, shell « UCRT64 ») :

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glew mingw-w64-ucrt-x86_64-glfw \
          mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-make
mingw32-make BUILD=release Scene_AllModels.exe
./Scene_AllModels.exe
```

Sous Linux : `make BUILD=release && ./Scene_AllModels`. Le programme doit être lancé depuis la racine du dépôt (il charge `models/`, `textures/` et `shaders/` en chemins relatifs).

## Contrôles

| Action | Touche |
|---|---|
| Regarder autour | clic gauche dans la fenêtre pour capturer la souris, puis souris |
| Avancer / gauche / reculer / droite | `W` `A` `S` `D` (touches `Z` `Q` `S` `D` sur un clavier AZERTY) |
| Monter / descendre | `Z` / `X` (touches `W` / `X` sur AZERTY) |
| Sprint (vitesse ×3) | `Shift` |
| Zoom (champ de vision) | molette |
| Lampe torche (spot) | `F` |
| Fil de fer | `F1` |
| Plein écran | `F11` |
| Quitter | `Échap` |

## Ce que contient le projet

### Chargement de modèles 3D
- Chargeur **OBJ** et chargeur **GLB** écrit pour ce projet ([`GLB.cpp`](GLB.cpp) : meshes, textures intégrées, couleurs de sommets). Chaque modèle est chargé **une seule fois** puis dessiné autant de fois que nécessaire.
- La scène est décrite par une liste de `Prop` (mesh, position, échelle, rotation) : ajouter un objet = ajouter une ligne.
- Des modèles sont aussi **générés par code** : le robot 11-45-G (profil 2D balayé à plusieurs hauteurs), la bouteille (surface de révolution), les luminaires de plafond, les routes, trottoirs et touffes d'herbe.

### Composition de la scène
- Une même tour sert à **23 bâtiments** : `addBuilding` change ses proportions et son orientation, et applique l'un de six styles (`STANDING`, `MIRRORED` (échelle négative en X), `UPSIDE_DOWN`, `LEANING`, `COLLAPSED`, `STACKED`). Un seul mesh en mémoire, une ville variée.
- Rues, trottoirs et plus de 7000 touffes d'herbe sont produits procéduralement (graine fixe : la ville est identique à chaque lancement).

### Système d'éclairage (`shaders/city.frag`)
- Blinn-Phong : lumière **directionnelle** (lune), jusqu'à **64 lumières ponctuelles** (réverbères, néons de plafond, yeux des robots, impact du laser) et un **spot** (lampe torche attachée à la caméra).
- Il y a plus de lampes que de lumières possibles : à chaque image, on garde les plus proches de la caméra (`std::partial_sort`).
- Les réverbères n'éclairent que vers le bas (cône `cosCone`) ; brouillard exponentiel de la couleur du ciel.

### Textures et matériaux
- Textures diffuses (OBJ et GLB), atlas d'herbe avec découpe de l'alpha (`alphaCutout`), couleurs de sommets pour les modèles sans texture.
- Paramètres de matériau (spéculaire, brillance) ajustés par type de surface : os mat, bitume et trottoir très peu brillants, métal des lampadaires.

### Animation et interactivité
- Animations procédurales, pilotées par le temps : clignotement des lampes (fonction de hachage : longues phases stables entrecoupées de rafales, graine par lampe, certaines lampes mortes), balayage du laser de 11-45-G (sinus sur le tangage et le lacet), pulsation des LED, ciel animé.
- Caméra à la première personne, sprint, zoom, lampe torche, fil de fer, plein écran.

### Effets spéciaux
- Faisceau laser plan (`shaders/scan.frag`) : dissolution avec la distance, trait de balayage.
- Halos additifs en billboard (`shaders/glow.*`), sans écriture dans le depth buffer.
- Ciel procédural (`shaders/sky.frag`) : bruit fractal (fbm) et lune.
- Ampoules et écrans émissifs (shader sans éclairage, `shaders/bulb.*`).

### Mode démo (optionnel)
`Scene_AllModels --demo fichier.txt` fait suivre à la caméra un parcours scripté lu dans un fichier texte, une ligne par point de passage : `t  px py pz  cx cy cz  fov  lampe_torche  fil_de_fer`.
- Position et **direction du regard** (yaw / pitch) sont interpolées par des cubiques de Hermite à tangentes monotones : la caméra glisse à vitesse régulière, ne « dépasse » jamais entre deux points et s'arrête vraiment sur une pause. Un léger balancement lent imite une personne qui filme.
- Des segments « utilisation normale » rejouent des touches simulées avec la vraie règle de déplacement de `update()` : `free t0 t1`, `key W|A|S|D|UP|DOWN|SHIFT t0 t1`, `look yaw/s pitch/s t0 t1` (souris), `zoom fov0 fov1 t0 t1` (molette), `flashkey t0 t1` (touche F).
- `--record sortie.mp4 --size 1920x1080` rend à horloge fixe de 60 images/s et envoie les images à FFmpeg : le rendu est fluide et identique à chaque exécution.

## Structure du code

| Fichier | Rôle |
|---|---|
| `Scene_AllModels.cpp` | scène, génération de la ville, lumières, boucle de rendu, entrées |
| `Camera.*` | caméra FPS et orbitale |
| `Mesh.*` | chargement OBJ, buffers OpenGL, dessin |
| `GLB.*` | chargeur glTF binaire (meshes, textures, couleurs) |
| `ShaderProgram.*` | compilation et uniforms des shaders |
| `Texture2D.*` | chargement de textures (stb_image) |
| `shaders/` | `city.*` (éclairage de la scène), `sky.*`, `scan.*`, `glow.*`, `bulb.*`, et les shaders des exercices |
| `models/`, `textures/` | assets |
| `Lighting_*.cpp` | exercices de cours |

## Crédits

- Squelette : « Human Skeleton » par Armen Barsegyan (cgicoffee.com), CC0.
- Les autres modèles et textures (`models/`, `textures/`) proviennent de sources diverses, utilisés dans un cadre pédagogique.
- Chargement d'images : [stb_image](https://github.com/nothings/stb).
