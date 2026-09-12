# Backoffice Infhome

Application Nuxt en français, conçue d'abord pour le téléphone : courses en texte libre, agenda manuel, publication explicite et thèmes clair/sombre. Les composants shadcn-vue sont générés dans `app/components/ui`, puis adaptés aux cibles tactiles de 44 px et à la palette Infhome.

## Stack

- Nuxt 4.5.2, Vue 3, Nitro en mode `node-server`.
- Tailwind CSS 4.3.3 avec `@tailwindcss/vite`.
- shadcn-vue et shadcn-nuxt 2.8.2, style `reka-nova`.
- Zod pour le contrat partagé entre navigateur et serveur.
- Sessions chiffrées et authentifiées via H3, sans base de données.
- Polices Geist et Geist Mono hébergées par l'application.

Les versions résolues sont enregistrées dans le lockfile du monorepo. TypeScript est fixé à `~6.0.3` : TypeScript 7 n'est pas compatible avec le fonctionnement actuel de `vue-tsc`.

## Développement

Prérequis : Node `^22.19.0` ou `^24.11.0` ou `>=26.0.0`, pnpm 9.15.0. Node 24 LTS est utilisé dans Docker.

Depuis la racine du dépôt :

```bash
pnpm install
cp apps/web/.env.example apps/web/.env
```

Compléter `apps/web/.env` :

```dotenv
NUXT_ADMIN_USERNAME=remi
NUXT_ADMIN_PASSWORD=choisir-un-mot-de-passe-personnel
NUXT_SESSION_PASSWORD=remplacer-par-un-secret-aleatoire
NUXT_SITE_URL=http://localhost:3000
NUXT_DATA_DIR=./data
```

Générer le secret de session (au moins 32 caractères) :

```bash
node -e "console.log(require('node:crypto').randomBytes(48).toString('hex'))"
```

```bash
pnpm web:dev
```

Ouvrir `http://localhost:3000`. Pour tester sur téléphone, définir `NUXT_SITE_URL` avec l'origine exacte utilisée, par exemple `http://192.168.0.10:3000`, puis redémarrer le serveur. Le contrôle d'origine exige une correspondance exacte. Le cookie utilise HTTP uniquement en développement ; le build de production utilise un cookie `Secure`.

Les données locales sont écrites dans `apps/web/data/snapshot.json`, ignoré par Git. Nuxt charge `.env` en développement ; le serveur de production reçoit ses variables via l'environnement de déploiement.

## Fonctionnement

- **Courses** : un article par ligne, 20 articles au maximum, 64 octets UTF-8 par article. Les lignes vides sont ignorées. Le bouton de réinitialisation vide le formulaire après confirmation ; il faut publier pour modifier le contenu en ligne.
- **Agenda** : jusqu'à 100 événements enregistrés, avec titre, date de début, date de fin facultative et heure facultative. Le bouton de suppression retire un événement du formulaire. Les événements passés restent éditables jusqu'à leur suppression manuelle.
- **Publication** : les deux onglets sont publiés ensemble. Le contenu est validé côté client et côté serveur. Les modifications non publiées restent uniquement dans la page ; quitter ou recharger la page peut les perdre. Un avertissement de navigation est demandé au navigateur lorsqu'elles existent.
- **Conflits** : chaque publication possède une révision UUID. Une page utilisant une ancienne révision reçoit une erreur 409 et conserve ses modifications. Le rechargement de la version publiée demande confirmation.
- **Lecture publique** : `GET /snapshot.json` expose les courses et les trois prochains événements, triés et sélectionnés au moment de la requête. Aucun aperçu PSP n'est affiché dans le backoffice.

Le [contrat v1](../../docs/SNAPSHOT_CONTRACT.md) précise le format et la sélection de l'agenda.

## Routes

| Route | Accès | Rôle |
| --- | --- | --- |
| `POST /api/auth/login` | Origine autorisée | Connexion |
| `GET /api/auth/session` | Public | Session courante, sans secret |
| `POST /api/auth/logout` | Session + origine | Déconnexion |
| `GET /api/admin/snapshot` | Session | Document complet et révision |
| `POST /api/admin/snapshot` | Session + origine | Publication atomique |
| `GET /snapshot.json` | Public | Snapshot destiné au Raspberry |
| `GET /health` | Public | État du processus pour Coolify |

La session dure sept jours. Le cookie est `HttpOnly`, `SameSite=Strict`, `Secure` en production. Changer les identifiants invalide les anciennes sessions ; changer le secret de session invalide également tous les cookies. La déconnexion efface le cookie du navigateur, sans registre serveur de révocation.

Les tentatives de connexion sont limitées globalement à 10 par tranche glissante de 15 minutes ; les publications à 30 par minute. Ces compteurs sont en mémoire et redémarrent avec le processus. Le serveur ne fait pas confiance à un en-tête IP fourni par le client.

## Déploiement Coolify

### Application

Créer une ressource depuis ce dépôt avec :

- **Build Pack** : Dockerfile.
- **Base Directory / contexte de build** : `/` (racine du monorepo).
- **Dockerfile Location** : `/apps/web/Dockerfile`.
- **Port interne** : `3000`.
- **Domaine** : `https://infhome.remirobichet.fr` lorsque le DNS est prêt.
- **Healthcheck** : `GET /health`, port `3000` ; le Dockerfile contient également un healthcheck.
- **Une seule instance / un seul processus**. Désactiver le déploiement roulant avec chevauchement : deux processus ne doivent pas écrire sur le même fichier simultanément. La sérialisation des écritures et les limites de fréquence sont internes au processus.

Le reverse proxy Coolify termine HTTPS et transmet HTTP au conteneur. Le healthcheck vérifie le processus, pas les identifiants ni l'accès au volume : effectuer une première publication pour vérifier ces deux éléments.

### Variables d'environnement de production

Définir comme variables d'exécution, sans les incorporer à l'image :

```dotenv
NUXT_ADMIN_USERNAME=remi
NUXT_ADMIN_PASSWORD=choisir-un-mot-de-passe-personnel
NUXT_SESSION_PASSWORD=remplacer-par-un-secret-aleatoire-de-32-caracteres-minimum
NUXT_SITE_URL=https://infhome.remirobichet.fr
NUXT_DATA_DIR=/var/lib/infhome
```

Ne pas activer un cache proxy sur les routes privées ou `/snapshot.json`. L'application envoie `Cache-Control: no-store`.

### Volume persistant

Dans **Persistent Storage**, ajouter un volume nommé, par exemple `infhome-data`, avec le chemin de destination :

```text
/var/lib/infhome
```

Le volume contient `snapshot.json`, le document complet publié (et non uniquement les trois événements exposés publiquement). Monter le **dossier**, pas le fichier : la publication utilise un fichier temporaire voisin puis un renommage atomique.

L'image exécute Node avec UID/GID `1000:1000`. Un volume Docker nommé neuf reprend les permissions du dossier de l'image. Pour un bind mount déjà existant, donner à cet utilisateur les droits d'écriture sur le dossier.

### Vérification après déploiement

1. Se connecter et publier une liste ainsi qu'un événement.
2. Vérifier `https://infhome.remirobichet.fr/snapshot.json` sans session.
3. Redéployer, se reconnecter et vérifier que le document complet est conservé.
4. Sauvegarder régulièrement le volume ou son fichier `snapshot.json` hors du VPS.
5. Pour restaurer : arrêter l'application, remplacer le fichier dans le volume par une sauvegarde valide avec les bonnes permissions, puis redémarrer.

Un fichier corrompu provoque une erreur et n'est jamais remplacé silencieusement par une liste vide. Avant la toute première publication, la route publique répond 503 ; après publication d'un contenu vide, elle répond 200 avec des listes vides.

Build Docker manuel, depuis la racine :

```bash
docker build -f apps/web/Dockerfile -t infhome-web .
```

## Vérification locale

```bash
pnpm web:typecheck
pnpm web:test
pnpm web:build
pnpm --filter infhome-web exec playwright install --with-deps chromium
pnpm web:test:e2e
```

Les tests E2E utilisent le build de production, le port 3100, un dossier temporaire isolé et des identifiants de test. Ils couvrent connexion, cookies, origine, limitation des tentatives, formulaires, thèmes, publication, réinitialisation, échec réseau, limites de taille et conflits. Les captures sont générées dans `apps/web/test-results`.

La restauration après redémarrage est testée au niveau du stockage. La persistance réelle après redéploiement Coolify doit être vérifiée sur le VPS.
