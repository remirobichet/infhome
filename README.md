# Infhome Monorepo

Ce depot regroupe les briques du projet Infhome.

Le contexte complet, l'architecture cible, les decisions ouvertes et la feuille de route sont regroupes dans [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md).

## Structure

- `apps/raspberry-api` : API locale du Raspberry Pi en TypeScript
- `apps/psp-homebrew` : homebrew natif PSP
- `apps/web` : backoffice Nuxt de mise a jour des courses et de l'agenda

## Backoffice web

Nuxt 4, Tailwind CSS 4 et shadcn-vue. Interface en francais, adaptee au telephone, avec connexion personnelle, themes clair/sombre et publication d'un snapshot JSON public.

```bash
pnpm install
cp apps/web/.env.example apps/web/.env
# Renseigner les identifiants et le secret de session dans apps/web/.env.
pnpm web:dev
```

Commandes de verification :

```bash
pnpm web:typecheck
pnpm web:test
pnpm web:build
pnpm --filter infhome-web exec playwright install --with-deps chromium
pnpm web:test:e2e
```

Voir [`apps/web/README.md`](apps/web/README.md) pour la configuration et le deploiement Coolify avec volume persistant, et [`docs/SNAPSHOT_CONTRACT.md`](docs/SNAPSHOT_CONTRACT.md) pour le contrat v1.

## API Raspberry Pi

Service TypeScript avec HTTP natif Node, sans dépendance de production. Cible : Raspberry Pi Zero 2 W, Raspbian 13 ARMv7, Node 22 LTS.

```bash
pnpm install
pnpm api:dev
pnpm api:test
pnpm api:package
```

- `GET /api/status` : message historique compatible avec le homebrew actuel.
- `GET /api/v1/dashboard` : heure/NTP, météo Toulouse matin et après-midi/soir, courses et agenda.
- Synchronisation des deux sources toutes les 15 minutes, caches persistants et fonctionnement hors ligne.
- Déploiement depuis WSL par archive `.tar.gz`, `scp`, puis service `systemd` ; aucun Git ou Docker sur le Pi.

Installation, mise à jour de Node et exploitation : [`apps/raspberry-api/README.md`](apps/raspberry-api/README.md). Format PSP : [`docs/DASHBOARD_CONTRACT.md`](docs/DASHBOARD_CONTRACT.md).

## Homebrew PSP

Le code natif PSP se trouve maintenant dans `apps/psp-homebrew`.

### Configuration PSP

La PSP-2004 utilise le firmware Sony `6.60` avec FasterARK en mode Live.

- FasterARK active ARK pour executer le homebrew natif.
- Apres un arret complet, il faut relancer FasterARK avant de lancer Infhome.
- L'installation reste non permanente : ne pas lancer `CustomIPL` pour ce projet.
- Le Wi-Fi doit etre teste dans les reglages Sony apres le lancement de FasterARK avant de lancer Infhome.

Le profil Wi-Fi utilise par Infhome est configure dans les reglages Sony de la PSP :

```text
Routeur : WPA2-PSK [AES]
PSP : WPA PSK AES
Profil PSP : 1
```

Le homebrew reutilise ce profil en appelant `sceNetApctlConnect(1)`. Le SSID et le mot de passe restent enregistres dans la configuration systeme PSP et ne sont pas stockes dans le code source.

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

### Dashboard et controles

Le homebrew demarre toujours en mode reel : il se connecte au Wi-Fi, puis recupere le message de l'API Raspberry Pi. La barre superieure affiche `INFHOME` et l'etat actuel : `DEMO`, `ONLINE` ou `OFFLINE`.

- `CARRE` affiche l'accueil.
- `TRIANGLE` affiche la liste de courses.
- `ROND` affiche l'agenda.
- `HAUT` alterne entre la page systeme et l'accueil. La page systeme est volontairement absente du menu et affiche l'IP PSP, l'etat Wi-Fi, l'accessibilite de l'API et l'adresse du Raspberry Pi.
- `SELECT` bascule entre le mode reel et le mode demo.
- En mode demo, aucune requete reseau n'est envoyee et le dashboard affiche des donnees locales simulees.
- `CROIX` rafraichit la source active : API en mode reel ou donnees locales en mode demo.
- `HOME` quitte l'application.

### Emulation avec PPSSPP

PPSSPP permet d'iterer rapidement sur l'UI, les controles et la mise en page sans recopier le PBP sur la Memory Stick.

1. Installer PPSSPP pour Windows depuis <https://www.ppsspp.org/download/>.
2. Compiler le homebrew avec Docker.
3. Dans PPSSPP, choisir `Load` puis ouvrir `apps/psp-homebrew/build/EBOOT.PBP`.
4. Mapper les boutons PSP dans `Settings > Controls`, notamment `CARRE`, `TRIANGLE`, `ROND`, `HAUT`, `CROIX`, `SELECT` et `HOME`.
5. Utiliser `SELECT` dans le dashboard pour passer au mode demo et iterer sur l'UI hors ligne.

PPSSPP ne valide pas fidelement les profils Wi-Fi PSP, ARK/FasterARK, le WPA2 ou le reseau materiel. Les tests Wi-Fi et API reels doivent toujours etre effectues sur la PSP physique.
