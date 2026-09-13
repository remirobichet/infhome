# Infhome : contexte, architecture cible et reprise du développement

Dernière mise à jour : 13 septembre 2026

## Décisions du 13 septembre 2026

- Matériel confirmé : **Raspberry Pi Zero 2 W Rev 1.0**, Raspbian GNU/Linux 13, `armv7l` (la mention historique Zero W/ARMv6 était incorrecte).
- Sur le Pi : Node 20.19.2, Python 3.13.5 ; NTP actif et synchronisé, fuseau `Europe/Paris`. Installer Node 22 LTS pour le service final.
- L'API Raspberry est implémentée en TypeScript avec HTTP natif Node, sans dépendance de production : snapshot validé, caches atomiques, météo Toulouse, heure/NTP, `/api/v1/dashboard` et `/api/status` historique.
- Contenu et météo : récupération immédiate au démarrage puis toutes les 15 minutes. Sans cache valide, sources à `null` ; dernières données conservées en cas d'erreur.
- Météo : maximum prévu et état représentatif de chaque demi-journée d'aujourd'hui, 00:00–11:59 et 12:00–23:59, en heure de Paris. Coordonnées Toulouse centre `43.6045, 1.4440`.
- Déploiement retenu : archive construite sur WSL, `scp` vers `infhome@192.168.0.104`, installation avec `sudo`, service `systemd`. Scripts fournis, caches et configuration conservés aux mises à jour, retour à la version précédente si le contrôle HTTP échoue.
- Le backoffice n'est pas encore déployé ; son URL est prévue. Tests locaux API réussis ; installation et fonctionnement sur le Pi restent à vérifier.
- Références actuelles : `apps/raspberry-api/README.md` et `docs/DASHBOARD_CONTRACT.md`. Le client C PSP reste à connecter au nouveau dashboard.

## Rôle de ce document

Ce document est le point d'entrée pour reprendre le développement d'Infhome après une interruption. Il résume :

- l'objectif du projet ;
- ce qui fonctionne déjà ;
- les contraintes matérielles et réseau ;
- les décisions d'architecture actuelles ;
- les points qui restent à décider ;
- une proposition de contrat JSON ;
- l'ordre recommandé des prochaines tâches.

Les décisions prises pendant le développement devront être reportées ici. Le code reste la référence pour les détails d'implémentation.

## Objectif

Infhome transforme une PSP-2004 en écran d'information mural. La PSP affiche notamment :

- l'heure ;
- la météo ;
- une liste de courses ;
- un agenda ;
- de futures informations domestiques.

Un Raspberry Pi Zero 2 W reste allumé sur le réseau local. Il récupère les données Internet, les met en cache et fournit à la PSP une API HTTP compatible avec son ancien environnement réseau.

Un futur capteur de présence relié au Raspberry devra permettre de réduire, couper ou réactiver l'affichage de la PSP.

## État validé

### PSP

- Modèle : PSP-2004 Slim & Lite.
- Firmware : Sony 6.60 avec ARK-5 LIVE.
- Fonctionnement sur secteur sans batterie validé.
- Connexion au Wi-Fi local validée avec le profil réseau PSP numéro 1.
- Obtention d'une adresse IP validée.
- Accès HTTP local au Raspberry validé.
- Exécution du homebrew natif Infhome validée.
- Compilation avec PSPSDK dans l'image Docker `pspdev/pspdev` validée.
- Interface native avec pages accueil, courses, agenda et système déjà présente.
- Mode de démonstration local déjà présent.

L'accès direct de la PSP à Internet ne fonctionne pas correctement. Une erreur DNS est observée lors du test Internet Sony, mais cela ne bloque pas Infhome car la PSP communique uniquement avec le Raspberry sur le réseau local.

### Raspberry

Une API TypeScript complète existe dans `apps/raspberry-api`, avec le serveur HTTP natif de Node et sans dépendance de production. Elle expose `/api/v1/dashboard` ; le contrat est documenté dans `docs/DASHBOARD_CONTRACT.md`.

Route historique conservée pour la PSP actuelle :

```text
GET /api/status
```

Réponse actuelle :

```json
{
  "message": "Hello from Raspberry Pi"
}
```

Le serveur écoute sur `0.0.0.0:8080`.

### Communication PSP vers Raspberry

Le homebrew ouvre actuellement un socket TCP, envoie une requête HTTP/1.0 vers le Raspberry et extrait manuellement la propriété JSON `message`.

Configuration actuelle dans `apps/psp-homebrew/main.c` :

```c
#define INFHOME_WIFI_PROFILE 1
#define INFHOME_PI_IP "192.168.0.104"
#define INFHOME_PI_PORT 8080
```

L'adresse du Raspberry devra rester stable grâce à une réservation DHCP, ou être rendue configurable plus tard.

### Web

Le backoffice est implémenté dans `apps/web` avec Nuxt 4.5.2, Tailwind CSS 4.3.3 via Vite et shadcn-vue/shadcn-nuxt 2.8.2.

- Interface en français, conçue d'abord pour le téléphone, avec thèmes clair et sombre et une discrète inspiration PSP.
- Un compte personnel, identifiant et mot de passe configurés côté serveur, session par cookie sécurisé.
- Courses sous forme de texte, un article par ligne, avec réinitialisation du formulaire.
- Agenda manuel : titre, date de début, date de fin facultative et heure facultative.
- Publication explicite des deux formulaires, validation Zod partagée et écriture atomique.
- Snapshot public, avec sélection automatique des trois prochains événements à chaque lecture.
- Conflits de publication détectés par révision UUID ; modifications locales conservées en cas d'erreur.
- Dockerfile et documentation Coolify disponibles. Le domaine prévu est `infhome.remirobichet.fr`.

Les tests unitaires, la vérification des types, le build de production et les parcours Chromium mobiles passent localement. Docker n'est pas disponible dans l'environnement WSL de cette session : le build de l'image et la persistance réelle après redéploiement Coolify restent à valider sur l'environnement cible.

Documentation : `apps/web/README.md`. Contrat v1 : `docs/SNAPSHOT_CONTRACT.md`. TypeScript est fixé à la branche 6.0, car TypeScript 7 n'est pas compatible avec `vue-tsc` actuellement.

## Architecture retenue

```text
Utilisateur
    |
    v
Backoffice Nuxt sur VPS
    |
    | écriture validée et authentifiée
    v
snapshot.json persistant sur VPS
    |
    | HTTPS, téléchargement périodique
    v
Raspberry Pi Zero 2 W
    |
    | cache du snapshot + météo + heure système
    | API HTTP locale
    v
PSP-2004
```

### Principes

1. La PSP ne contacte jamais directement Internet.
2. La PSP contacte uniquement l'API HTTP locale du Raspberry.
3. Les requêtes de la PSP ne déclenchent jamais une requête Internet synchrone.
4. Le Raspberry télécharge les données en arrière-plan et conserve les dernières données valides.
5. Une panne Internet ou VPS ne doit pas empêcher l'affichage des dernières données connues.
6. Le format envoyé à la PSP doit rester petit, stable et simple à parser.
7. Le contenu créé dans le backoffice doit rester inchangé dans la section `content` de la réponse Raspberry.

## VPS et backoffice Nuxt

### Choix actuel

PocketBase n'est pas nécessaire pour la première version. Les données sont suffisamment simples pour utiliser un fichier JSON comme stockage principal.

Nuxt sert :

- le backoffice d'édition ;
- une route serveur authentifiée pour enregistrer les données ;
- une route publique permettant au Raspberry de télécharger le snapshot, en HTTPS derrière le proxy Coolify.

Routes implémentées :

```text
POST /api/admin/snapshot
GET  /api/admin/snapshot
GET  /snapshot.json
```

### Écriture du snapshot

La route d'administration réalise :

1. vérifier l'authentification ;
2. valider complètement les données reçues ;
3. générer un document avec une version de schéma ;
4. écrire un fichier temporaire ;
5. renommer atomiquement le fichier temporaire vers `snapshot.json`.

Le fichier ne doit pas être écrit dans le dossier `public` du dépôt, car un déploiement pourrait le supprimer. Il doit être placé sur un volume persistant, par exemple :

```text
/var/lib/infhome/snapshot.json
```

Le fichier persistant contient tous les événements publiés (jusqu'à 100) et une révision interne. Nuxt/Nitro le lit puis expose uniquement les trois prochains événements depuis `GET /snapshot.json`. Le proxy ne doit donc pas servir directement le fichier persistant : la sélection doit évoluer sans nouvelle publication. `updatedAt` reste la date de publication manuelle ; la réponse publique n'est pas mise en cache.

Coolify doit monter un volume nommé sur `/var/lib/infhome`, accessible à l'UID/GID `1000:1000`. Exécuter un seul processus et une seule instance, sans chevauchement de deux conteneurs écrivains lors d'un déploiement. Le Dockerfile utilise Node 24 LTS et expose le port interne 3000.

### Limites acceptées du stockage JSON

Ce choix convient tant que :

- peu d'utilisateurs modifient les données ;
- les données restent petites ;
- les modifications simultanées sont rares ;
- aucun historique complexe n'est nécessaire.

SQLite ou PocketBase pourra être ajouté plus tard si ces contraintes changent. Cette migration ne devra pas modifier le contrat `snapshot.json` consommé par le Raspberry.

## Service Raspberry

Le Raspberry agrège trois sources :

1. le contenu édité sur le VPS ;
2. la météo récupérée sur Internet ;
3. l'heure du système Raspberry synchronisée par NTP.

### Contenu du VPS

- Télécharger `snapshot.json` toutes les 15 minutes.
- Utiliser un timeout réseau de 5 à 10 secondes.
- Valider taille, version et structure avant remplacement du cache.
- Conserver le fichier précédent en cas d'erreur HTTP, JSON invalide ou contenu incomplet.
- Écrire le cache local de manière atomique.

Chemin local envisagé :

```text
/var/lib/infhome/content.json
```

Le Raspberry agit comme un passe-plat pour cette partie : il ne modifie pas le contenu métier reçu du VPS. Il le conserve puis le place dans la propriété `content` de la réponse PSP.

### Heure

L'heure ne doit pas être récupérée depuis une API web. Raspberry Pi OS doit synchroniser l'horloge système avec NTP, idéalement via `systemd-timesyncd`.

L'API ajoute l'heure système au moment de chaque réponse. Aucun fichier ne doit être réécrit chaque seconde.

Le Raspberry Pi Zero W ne possède pas d'horloge RTC matérielle. Après un démarrage sans réseau, l'heure peut donc être incorrecte. La réponse devra si possible indiquer si la synchronisation NTP a eu lieu.

Fuseau horaire prévu :

```text
Europe/Paris
```

### Météo

Open-Meteo est le fournisseur envisagé pour la première version :

- pas de clé API nécessaire ;
- données JSON ;
- latitude et longitude fixes ;
- récupération possible des conditions actuelles et prévisions journalières.

Fréquence retenue : toutes les 15 minutes. Deux créneaux d'aujourd'hui, 00:00–11:59 et 12:00–23:59 : maximum des températures horaires et état représentatif, avec priorité aux précipitations et orages. Les deux créneaux restent affichables toute la journée.

Le Raspberry transforme la réponse externe vers un petit format stable destiné à Infhome. Les codes météo peuvent être convertis en libellés courts sur le Raspberry afin de garder la PSP simple.

Cache local envisagé :

```text
/var/lib/infhome/weather.json
```

### API locale

Route principale implémentée :

```text
GET /api/v1/dashboard
```

Une seule réponse regroupe heure, météo et contenu du VPS. Cela réduit le nombre de connexions et simplifie le code PSP.

Routes complémentaires possibles :

```text
GET  /api/status
POST /api/refresh
```

`POST /api/refresh` est facultatif. S'il est ajouté, il devra rester limité au réseau local et ne pas bloquer pendant les téléchargements.

### Cycle de démarrage

1. Charger les caches présents sur disque.
2. Démarrer immédiatement l'API locale.
3. Lancer les synchronisations Internet en arrière-plan.
4. Remplacer chaque cache seulement après validation complète.
5. Signaler les données trop anciennes avec une propriété `stale`.

### Technologie du service

Le matériel confirmé est un Pi Zero 2 W avec système ARMv7. Le service conserve TypeScript et utilise HTTP natif Node à la place du serveur Express minimal : aucune dépendance de production à transférer ou installer. Node 22 LTS est recommandé et disponible officiellement pour `linux-armv7l`. Le Node 20.19.2 actuel doit être mis à jour.

Le déploiement utilise une archive `.tar.gz` et `systemd`, sans Docker ni Git sur le Pi. Procédure complète dans `apps/raspberry-api/README.md`.

### Exécution en production

Le service `systemd` fourni dans `apps/raspberry-api/deploy` prévoit :

- démarrage automatique ;
- redémarrage après erreur ;
- journalisation via `journalctl` ;
- configuration dans un fichier d'environnement ;
- attente raisonnable du réseau sans empêcher le démarrage depuis le cache.

Variables envisagées :

```text
PORT=8080
INFHOME_SNAPSHOT_URL=https://example.com/snapshot.json
INFHOME_LATITUDE=...
INFHOME_LONGITUDE=...
INFHOME_REFRESH_SECONDS=900
INFHOME_TIMEOUT_SECONDS=8
INFHOME_DATA_DIR=/var/lib/infhome
```

## Contrats JSON

Le snapshot VPS v1 est défini et implémenté dans `apps/web/shared/snapshot.ts`, avec sa documentation dans `docs/SNAPSHOT_CONTRACT.md`. La réponse agrégée Raspberry est implémentée ; son contrat complet est dans `docs/DASHBOARD_CONTRACT.md`.

### Snapshot produit par Nuxt

```json
{
  "version": 1,
  "updatedAt": 1789200000,
  "shopping": [
    "Pain",
    "Lait"
  ],
  "agenda": [
    {
      "title": "Dentiste",
      "startDate": "2026-09-23",
      "endDate": null,
      "time": "18:30"
    }
  ]
}
```

### Réponse finale du Raspberry

```json
{
  "version": 1,
  "generatedAt": 1789200000,
  "time": {
    "unix": 1789200000,
    "timezone": "Europe/Paris",
    "synced": true
  },
  "weather": {
    "date": "2026-09-12",
    "morning": { "max": 23.4, "code": 3, "label": "Couvert" },
    "evening": { "max": 28.1, "code": 61, "label": "Pluvieux" },
    "fetchedAt": 1789199700,
    "stale": false
  },
  "contentSync": { "fetchedAt": 1789199700, "stale": false },
  "content": {
    "version": 1,
    "updatedAt": 1789200000,
    "shopping": [
      "Pain",
      "Lait"
    ],
    "agenda": [
      {
        "title": "Dentiste",
        "startDate": "2026-09-23",
        "endDate": null,
        "time": "18:30"
      }
    ]
  }
}
```

### Règles du contrat PSP

Décisions v1 du snapshot VPS :

- Corps public limité à 4 096 octets UTF-8 ; 20 articles de 64 octets maximum chacun.
- Jusqu'à 100 événements conservés dans le document publié complet, titres de 96 octets maximum.
- Trois événements exposés, sans limite d'horizon, triés par date de début puis heure. Les périodes en cours restent présentes jusqu'à leur dernier jour inclus. Les événements d'un jour sans heure restent toute la journée ; ceux avec heure sont retirés lorsque leur minute de début est passée.
- Dates civiles au format `YYYY-MM-DD`, de 2000 à 2099 ; `endDate` et `time` sont explicitement `null` lorsqu'ils sont absents. Interprétation en `Europe/Paris`.
- Le document complet d'administration ajoute les UUID d'événements et une révision ; ces champs ne sont pas exposés publiquement. Requêtes d'administration et fichier persistant limités à 65 536 octets.
- Le Raspberry doit remplacer son cache après chaque téléchargement valide, même si `updatedAt` est inchangé : la sélection des trois événements peut avoir changé. Dater séparément les téléchargements réussis.
- Pour le futur dashboard agrégé, une limite de 8 Kio de corps JSON est recommandée, à confirmer côté C avec un espace borné distinct pour les en-têtes.

- Garder une propriété `version` entière.
- Préférer les timestamps Unix aux formats de date complexes.
- Garder les chaînes et listes courtes.
- Définir une taille maximale de réponse.
- Ne pas envoyer de données inutilisées par l'écran.
- Renvoyer un `Content-Length`.
- Fermer la connexion après la réponse.
- Ne pas utiliser gzip.
- Ne pas utiliser de transfert HTTP chunked.
- Vérifier le rendu des accents et de l'UTF-8 sur PSP avant de les utiliser dans le contenu.

## Contraintes actuelles du homebrew PSP

Le code actuel est une preuve de communication, pas encore un client JSON complet.

- Buffer HTTP actuel limité à 512 octets.
- Extraction manuelle de la seule propriété `message`.
- Absence de validation du statut HTTP.
- Absence de vrai parseur JSON.
- Adresse IP du Raspberry codée en dur.
- Rafraîchissement manuel avec `CROIX`.

Pour le contrat final, il faudra probablement :

- augmenter le buffer avec une limite stricte ;
- vérifier le statut HTTP et `Content-Length` ;
- intégrer un petit parseur JSON compatible C, par exemple `jsmn` ;
- mapper les données vers les pages accueil, courses et agenda ;
- décider d'une fréquence de rafraîchissement automatique ;
- conserver le mode démonstration pour le développement dans PPSSPP.

## Sécurité

### VPS

- Utiliser HTTPS.
- Protéger toutes les routes d'écriture du backoffice.
- Valider les données côté serveur même si le formulaire Nuxt les valide déjà.
- Sauvegarder le volume contenant `snapshot.json`.
- Limiter taille et fréquence des écritures.

Le snapshot est public, conformément au choix du propriétaire. Aucun secret de lecture n'est requis sur le Raspberry. Les routes d'administration restent protégées par session, contrôle strict d'origine, limites de taille et de fréquence. Le cookie est chiffré, `HttpOnly`, `SameSite=Strict`, `Secure` en production et expire après sept jours.

### Réseau local

- Utiliser HTTP entre PSP et Raspberry pour rester compatible avec la PSP.
- Garder l'API en lecture seule autant que possible.
- Ne jamais exposer le port `8080` sur Internet.
- Limiter le port au réseau local avec le pare-feu du Raspberry ou du routeur.
- Ne pas publier d'informations sensibles, car le réseau compatible PSP utilise une sécurité Wi-Fi ancienne.

## Capteur de présence, phase future

Le Raspberry devra plus tard lire un capteur GPIO et transmettre l'état de présence à la PSP.

Objectif :

```text
Présence détectée
    -> affichage actif

Absence pendant un délai configurable
    -> luminosité réduite, rétroéclairage coupé ou veille
```

La méthode exacte de contrôle de l'écran PSP reste à étudier dans PSPSDK. Cette fonctionnalité ne doit pas bloquer les premières versions du dashboard.

## Décisions encore ouvertes

- Installer Node 22 LTS sur le Pi et valider le déploiement `systemd` fourni.
- Taille maximale de la réponse agrégée acceptée par la PSP et implémentation de son parseur ; le snapshot VPS v1 est fixé à 4 Kio.
- Configuration effective du domaine et du volume Coolify, build Docker et validation de la restauration après redéploiement.
- Valider sur le Pi les prévisions Toulouse par demi-journée et la reprise hors ligne.
- Fréquence de rafraîchissement automatique côté PSP.
- Gestion précise des caractères accentués dans l'interface native.
- Méthode future de gestion du rétroéclairage ou de la veille PSP.

## Feuille de route recommandée

### Étape 1 : valider le Raspberry

- [x] Vérifier architecture du système et version Node : ARMv7, Node 20.19.2.
- [x] Vérifier synchronisation NTP et fuseau `Europe/Paris`.
- [ ] Confirmer la réservation DHCP de `192.168.0.104`.
- [x] Choisir TypeScript avec HTTP natif Node pour le service final.
- [ ] Installer Node 22 LTS sur le Pi.

### Étape 2 : créer le snapshot VPS

- [x] Initialiser l'application Nuxt dans `apps/web` avec Tailwind CSS et shadcn-vue.
- [x] Créer l'authentification personnelle du backoffice.
- [x] Définir et valider le schéma JSON version 1.
- [x] Implémenter l'écriture atomique dans un dossier configurable et tester la restauration au niveau du stockage.
- [x] Implémenter `GET /snapshot.json` avec la sélection automatique des trois prochains événements.
- [x] Ajouter les formulaires mobiles, la publication, les thèmes et les tests navigateur.
- [ ] Configurer Coolify, le domaine HTTPS et le volume persistant, puis valider le build Docker.
- [ ] Tester la restauration après redéploiement du VPS et configurer la sauvegarde du volume.

### Étape 3 : enrichir API Raspberry

- [x] Télécharger et valider snapshot VPS en arrière-plan.
- [x] Ajouter cache mémoire et cache disque.
- [x] Ajouter synchronisation météo Open-Meteo par demi-journée.
- [x] Ajouter heure système et état NTP.
- [x] Exposer `GET /api/v1/dashboard`.
- [x] Ajouter indicateurs `stale` et dates de dernière mise à jour.
- [x] Fournir archive et installation du service `systemd`.
- [ ] Installer et vérifier le service sur le Pi physique.

### Étape 4 : connecter dashboard PSP

- Définir taille maximale de réponse.
- Ajouter gestion HTTP robuste.
- Intégrer parseur JSON léger.
- Afficher heure, météo, courses et agenda.
- Ajouter rafraîchissement périodique sans bloquer interface.
- Tester sur PSP physique, pas seulement PPSSPP.

### Étape 5 : fiabiliser

- Tester démarrage sans Internet avec caches existants.
- Tester VPS indisponible.
- Tester réponse météo invalide ou lente.
- Tester coupure puis retour du Wi-Fi.
- Vérifier consommation mémoire du Raspberry et de la PSP.
- Mettre en place sauvegarde du snapshot VPS.

### Étape 6 : présence et écran

- Brancher capteur GPIO.
- Définir délai d'absence.
- Explorer API PSPSDK de luminosité, rétroéclairage, veille et réveil.
- Ajouter état de présence au contrat local si nécessaire.

## Critères de réussite de la première version

- Les données sont modifiables depuis le backoffice Nuxt.
- Un redéploiement du VPS ne supprime pas le snapshot.
- Le Raspberry récupère contenu et météo sans intervention manuelle.
- L'heure affichée vient d'une horloge Raspberry synchronisée.
- La PSP n'accède qu'à `http://IP_DU_RASPBERRY:8080`.
- La PSP affiche heure, météo, courses et agenda.
- Une panne Internet conserve les dernières données valides à l'écran.
- Aucun secret VPS n'est stocké sur la PSP.
- Le service Raspberry redémarre automatiquement après une panne ou un reboot.

## Reprise rapide

Lors d'une prochaine session de développement :

1. lire ce fichier ;
2. lire `README.md` pour les commandes actuelles ;
3. vérifier `git status` avant toute modification ;
4. vérifier les éléments de la section « Décisions encore ouvertes » ;
5. commencer par la première étape incomplète de la feuille de route ;
6. mettre ce document à jour après chaque décision d'architecture importante.
