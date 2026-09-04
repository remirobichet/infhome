# Infhome Monorepo

Ce depot regroupe les briques du projet Infhome.

## Structure

- `apps/raspberry-api` : API locale du Raspberry Pi en TypeScript
- `apps/psp-homebrew` : homebrew natif PSP
- `apps/web` : future application web de mise a jour des donnees

## API Raspberry Pi

Installation :

```bash
pnpm install
```

Developpement :

```bash
pnpm api:dev
```

Build :

```bash
pnpm api:build
```

L'API expose actuellement :

- `GET /api/status`

Reponse :

```json
{
  "message": "Hello from Raspberry Pi"
}
```

## Homebrew PSP

Le code natif PSP se trouve maintenant dans `apps/psp-homebrew`.

Compilation avec Docker :

```bash
cd apps/psp-homebrew

docker run --rm -v "$PWD:/source" pspdev/pspdev:latest sh -lc \
  "cd /source && /usr/local/pspdev/bin/psp-cmake -B build -S . && make -C build"
```

Le homebrew compile est genere dans :

```text
apps/psp-homebrew/build/EBOOT.PBP
```

Copier ensuite ce fichier dans `PSP/GAME/INFHOME/EBOOT.PBP` sur la Memory Stick.

### Execution sur PSP

L'application utilise le profil Wi-Fi PSP configure dans les reglages Sony et appelle l'API locale :

```text
http://192.168.0.104:8080/api/status
```

L'adresse du Raspberry Pi est definie dans `apps/psp-homebrew/main.c` :

```c
#define INFHOME_PI_IP "192.168.0.104"
```

### Modes dashboard

Le homebrew demarre toujours en mode reel : il se connecte au Wi-Fi, puis recupere le message de l'API Raspberry Pi.

- `SELECT` bascule entre le mode reel et le mode demo.
- En mode demo, aucune requete reseau n'est envoyee et le dashboard affiche des donnees locales simulees.
- `X` rafraichit la source active : API en mode reel ou donnees locales en mode demo.
- `HOME` quitte l'application.

Le mode demo est indique par `DEMO MODE` dans l'en-tete du dashboard.

### Emulation avec PPSSPP

PPSSPP permet d'iterer rapidement sur l'UI, les controles et la mise en page sans recopier le PBP sur la Memory Stick.

1. Installer PPSSPP pour Windows depuis <https://www.ppsspp.org/download/>.
2. Compiler le homebrew avec Docker.
3. Dans PPSSPP, choisir `Load` puis ouvrir `apps/psp-homebrew/build/EBOOT.PBP`.
4. Mapper les boutons PSP dans `Settings > Controls`, notamment `X`, `SELECT` et `HOME`.
5. Utiliser `SELECT` dans le dashboard pour passer au mode demo et iterer sur l'UI hors ligne.

PPSSPP ne valide pas fidelement les profils Wi-Fi PSP, ARK/FasterARK, le WPA2 ou le reseau materiel. Les tests Wi-Fi et API reels doivent toujours etre effectues sur la PSP physique.
