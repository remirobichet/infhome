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
