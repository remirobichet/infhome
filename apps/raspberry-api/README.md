# API Raspberry Infhome

Service TypeScript pour un **Raspberry Pi Zero 2 W Rev 1.0**, Raspbian 13, architecture `armv7l`. Il utilise le serveur HTTP natif de Node : aucune dépendance npm en production, aucune compilation sur le Pi, aucun Git ou Docker nécessaire sur celui-ci.

## Fonctionnement

- `GET /api/status` conserve la réponse historique `{"message":"Hello from Raspberry Pi"}`.
- `GET /api/v1/dashboard` regroupe l'heure, la météo et le snapshot du backoffice.
- Les deux téléchargements sont indépendants et commencent dès le démarrage, puis 900 secondes après la fin de chaque tentative. Les GET PSP lisent uniquement les caches.
- `POST /api/v1/dashboard/refresh`, sans corps, synchronise les courses et l'agenda puis renvoie le dashboard. La météo n'est pas téléchargée par cette route. Les actualisations simultanées partagent le téléchargement du contenu en cours. En cas d'échec, HTTP 502 est renvoyé et le cache précédent est conservé.
- Le snapshot HTTPS est limité à 4 096 octets UTF-8 et validé strictement. Un téléchargement valide remplace le cache même si `updatedAt` est identique. Le contenu métier reste inchangé.
- Les caches sont chargés avant l'ouverture HTTP et écrits par fichier temporaire, synchronisation puis renommage. En cas d'erreur distante ou d'écriture, la dernière valeur valide reste disponible. Un fichier corrompu est signalé et ignoré, sans être effacé.
- Sans cache, `content` et/ou `weather` valent `null`. Le dashboard reste accessible avec HTTP 200 ; `/api/status` vérifie seulement le processus.
- `stale` devient vrai après plus de deux intervalles sans téléchargement réussi (30 minutes par défaut), ou si l'horloge est antérieure au téléchargement. La météo devient aussi périmée lorsque sa date n'est plus celle du jour à Paris.
- L'état NTP est vérifié toutes les 60 secondes avec `timedatectl` : `true`, `false`, ou `null` si inconnu. L'heure de réponse vient toujours du système.
- Les réponses sont bornées à 8 192 octets, avec `Content-Length`, `Connection: close`, sans compression ni transfert chunked. HTTP/1.0 est accepté.
- Le délai de téléchargement du contenu est plafonné à 45 s (8 s par défaut). Le POST dispose de 55 s d'inactivité côté serveur et de 60 s d'attente HTTP côté PSP.

### Météo Toulouse

Coordonnées par défaut : `43.6045, 1.4440`. Source : [Open-Meteo](https://open-meteo.com/), prévisions horaires en °C, fuseau `Europe/Paris`.

- `morning` : 00:00 à 11:59.
- `evening` : 12:00 à 23:59, après-midi compris.
- Les deux créneaux concernent **aujourd'hui**, même quand le matin est passé.
- `max` est le maximum des températures horaires prévues dans le créneau, pas un maximum mesuré.
- L'état correspond au code le plus fréquent, mais donne priorité aux orages, puis précipitations verglaçantes, neige, puis pluie/bruine/averses. À priorité et fréquence égales, le code WMO le plus élevé est retenu.
- À minuit, la météo de la veille reste visible avec sa date et `stale: true` jusqu'au prochain téléchargement, normalement sous 15 minutes. Le client doit tenir compte de ces champs.

Contrat détaillé : [`../../docs/DASHBOARD_CONTRACT.md`](../../docs/DASHBOARD_CONTRACT.md).

## Développement depuis WSL

Node 22 LTS recommandé pour le système ARMv7 du Pi. Node 24/26 sont également acceptés lorsque disponibles pour l'architecture utilisée. Node 20, actuellement présent sur le Pi, doit être remplacé pour ce déploiement.

Depuis la racine du dépôt :

```bash
pnpm install
pnpm api:dev
pnpm api:test
pnpm api:package
```

Le développement utilise `apps/raspberry-api/data` par défaut. Les variables sont prises dans l'environnement ; aucun `.env` n'est chargé automatiquement par `api:dev`. Le fuseau métier est fixé à `Europe/Paris`, conformément au contrat du backoffice.

L'archive est générée dans `apps/raspberry-api/releases/infhome-api-<date>.tar.gz`. Elle contient `dist`, les scripts de déploiement, l'exemple de configuration et ce README, sans secrets, caches ni `node_modules`.

## Installation initiale du Pi

### 1. Installer Node 22 pour ARMv7

Les binaires officiels Node 22 existent pour `linux-armv7l`. La procédure ci-dessous installe uniquement le runtime sous `/opt/infhome-node`, sans remplacer les fichiers gérés par apt. Elle place un lien `/usr/local/bin/node` prioritaire sur l'ancien Node 20. Si ce chemin contient déjà une installation personnelle, vérifier celle-ci avant de le remplacer.

Sur le Pi, avec l'utilisateur `infhome` :

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates curl xz-utils

# Exécuter ce bloc dans un shell bash.
(
  set -euo pipefail
  test "$(uname -m)" = armv7l
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  base=https://nodejs.org/dist/latest-v22.x
  curl --fail --show-error --silent "$base/SHASUMS256.txt" -o "$work/SHASUMS256.txt"
  file=$(awk '$2 ~ /^node-v22\.[0-9]+\.[0-9]+-linux-armv7l\.tar\.xz$/ {print $2}' "$work/SHASUMS256.txt")
  test -n "$file"
  # URL versionnée : une nouvelle publication pendant l'installation ne change pas l'archive.
  version=${file#node-}
  version=${version%-linux-armv7l.tar.xz}
  curl --fail --show-error --silent "https://nodejs.org/dist/$version/$file" -o "$work/$file"
  (cd "$work" && awk -v file="$file" '$2 == file' SHASUMS256.txt | sha256sum --check -)
  tar -xJf "$work/$file" -C "$work"
  "$work/${file%.tar.xz}/bin/node" --version
  sudo install -d "/opt/infhome-node/$version" /usr/local/bin
  sudo install -m 0755 "$work/${file%.tar.xz}/bin/node" "/opt/infhome-node/$version/node"
  sudo ln -sfn "/opt/infhome-node/$version/node" /usr/local/bin/node
)
hash -r
node --version
sudo node --version
```

Relancer cette procédure pour les mises à jour de sécurité Node 22, puis `sudo systemctl restart infhome-api`. npm et pnpm ne sont pas requis sur le Pi.

### 2. Transférer l'archive

Depuis WSL, à la racine du dépôt :

```bash
pnpm api:package
# Remplacer <date> par la date affichée par la commande précédente.
scp apps/raspberry-api/releases/infhome-api-<date>.tar.gz infhome@192.168.0.104:/tmp/infhome-api.tar.gz
ssh infhome@192.168.0.104
```

### 3. Installer et lancer

Sur le Pi :

```bash
release=$(mktemp -d)
tar -xzf /tmp/infhome-api.tar.gz -C "$release"
sudo bash "$release/deploy/install.sh"
curl --fail http://127.0.0.1:8080/api/v1/dashboard
sudo systemctl status infhome-api --no-pager
```

Le script crée :

```text
/opt/infhome/releases/release-XXXXXXXX/  Code des versions
/opt/infhome/current                    Lien vers la version active
/etc/infhome/infhome.env                 Configuration conservée aux mises à jour
/var/lib/infhome/content.json           Snapshot et date de téléchargement
/var/lib/infhome/weather.json           Météo et date de téléchargement
/etc/systemd/system/infhome-api.service Service exécuté par infhome-api
```

Il vérifie Node et la configuration, empêche deux installations simultanées, bascule la version, redémarre le service et contrôle le dashboard local. Si le contrôle échoue, il restaure le code et l'unité précédents. Les caches et la configuration ne sont pas remplacés lors d'une mise à jour. Les anciennes versions restent disponibles sur disque.

Le service démarre automatiquement après reboot, sans attendre Internet. Le domaine du backoffice étant encore prévu, les erreurs de téléchargement sont normales jusqu'à son déploiement et sa première publication. La météo fonctionne indépendamment.

### 4. Configuration et exploitation

```bash
sudo nano /etc/infhome/infhome.env
sudo systemctl restart infhome-api
journalctl -u infhome-api -f
```

Variables disponibles :

```dotenv
HOST=0.0.0.0
PORT=8080
INFHOME_SNAPSHOT_URL=https://infhome.remirobichet.fr/snapshot.json
INFHOME_LATITUDE=43.6045
INFHOME_LONGITUDE=1.4440
INFHOME_REFRESH_SECONDS=900
INFHOME_TIMEOUT_SECONDS=8
INFHOME_DATA_DIR=/var/lib/infhome
```

Conserver `/var/lib/infhome` avec l'unité fournie : `ProtectSystem=strict` n'autorise l'écriture persistante que dans ce dossier. Le fichier d'environnement contient des affectations simples, sans expansion shell. Garder le port 8080 accessible uniquement sur le réseau local, sans redirection Internet. Réserver `192.168.0.104` dans le DHCP du routeur.

Pour une mise à jour, répéter les étapes 2 et 3. Pour revenir manuellement à une version conservée :

```bash
sudo ln -s /opt/infhome/releases/release-ANCIENNE /opt/infhome/current.rollback
sudo mv -Tf /opt/infhome/current.rollback /opt/infhome/current
sudo systemctl restart infhome-api
```

## Vérification sur le matériel

Les tests locaux couvrent validation, météo, caches, réponses invalides/lentes, reprise distante et transport HTTP/1.0. L'installation `systemd`, les permissions et les binaires ARM doivent être validés sur le Pi :

1. Vérifier le dashboard et les deux créneaux météo.
2. Publier une modification des courses et de l'agenda, puis appuyer sur Croix sur la PSP : vérifier le nouveau contenu et la conservation de la date de récupération météo. Le POST peut aussi être testé avec `curl --fail -X POST http://127.0.0.1:8080/api/v1/dashboard/refresh`.
3. Couper l'accès Internet en conservant le LAN, puis redémarrer le service : les caches doivent rester disponibles.
4. Rétablir Internet et vérifier la reprise, au prochain cycle.
5. Redémarrer le Pi et vérifier le démarrage automatique et `time.synced`.

Le client C PSP lit le dashboard toutes les 60 s et utilise le POST lors d'un appui sur Croix. Tester aussi les clics répétés et l'échec sans Internet : l'ancien affichage doit rester visible et l'état réseau doit signaler l'erreur.
