# Infhome : contexte, architecture cible et reprise du développement

Dernière mise à jour : 12 septembre 2026

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

Un Raspberry Pi Zero W reste allumé sur le réseau local. Il récupère les données Internet, les met en cache et fournit à la PSP une API HTTP compatible avec son ancien environnement réseau.

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

Une API TypeScript minimale existe dans `apps/raspberry-api`.

Route actuelle :

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

Le dossier `apps/web` est encore un emplacement réservé. Le backoffice Nuxt n'est pas implémenté.

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
Raspberry Pi Zero W
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
- une route HTTPS permettant au Raspberry de télécharger le snapshot.

Routes envisagées :

```text
POST /api/admin/snapshot
GET  /snapshot.json
```

### Écriture du snapshot

La route d'administration devra :

1. vérifier l'authentification ;
2. valider complètement les données reçues ;
3. générer un document avec une version de schéma ;
4. écrire un fichier temporaire ;
5. renommer atomiquement le fichier temporaire vers `snapshot.json`.

Le fichier ne doit pas être écrit dans le dossier `public` du dépôt, car un déploiement pourrait le supprimer. Il doit être placé sur un volume persistant, par exemple :

```text
/var/lib/infhome/snapshot.json
```

Nuxt/Nitro peut lire ce fichier et le renvoyer depuis `GET /snapshot.json`. Caddy ou Nginx pourra aussi le servir directement si cela simplifie le déploiement.

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

- Télécharger `snapshot.json` toutes les 30 à 60 secondes.
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

Fréquence recommandée : toutes les 10 à 15 minutes.

Le Raspberry transforme la réponse externe vers un petit format stable destiné à Infhome. Les codes météo peuvent être convertis en libellés courts sur le Raspberry afin de garder la PSP simple.

Cache local envisagé :

```text
/var/lib/infhome/weather.json
```

### API locale

Route principale envisagée :

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

Le service existant utilise TypeScript et Express. Le conserver est le choix le plus simple si une version compatible de Node fonctionne sur le Raspberry.

Attention : le Raspberry Pi Zero W original utilise une architecture ARMv6. Le support ARMv6 par les versions modernes officielles de Node est limité. Avant de poursuivre, vérifier sur le Raspberry :

```bash
uname -m
node --version
```

Décision à prendre après ce test :

- conserver TypeScript et Express si Node est installé, maintenable et stable ;
- utiliser Python 3 si Node pose un problème de compatibilité ARMv6.

Éviter Docker sur le Raspberry Pi Zero W : la mémoire est limitée et les images ARMv6 ne sont pas toujours disponibles.

### Exécution en production

Le service devra être lancé par `systemd` avec :

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
INFHOME_TIMEZONE=Europe/Paris
INFHOME_DATA_DIR=/var/lib/infhome
```

## Contrats JSON proposés

Ces contrats sont des brouillons. Ils devront être validés avant implémentation afin d'éviter plusieurs formats incompatibles.

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
      "time": "18:30",
      "title": "Dentiste"
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
    "temperature": 18,
    "label": "Nuageux",
    "min": 12,
    "max": 20,
    "fetchedAt": 1789199700,
    "stale": false
  },
  "content": {
    "version": 1,
    "updatedAt": 1789200000,
    "shopping": [
      "Pain",
      "Lait"
    ],
    "agenda": [
      {
        "time": "18:30",
        "title": "Dentiste"
      }
    ]
  }
}
```

### Règles du contrat PSP

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

Le snapshot peut être public s'il ne contient aucune donnée sensible. Sinon, le Raspberry devra utiliser un secret de lecture conservé uniquement dans son fichier d'environnement.

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

- Technologie Raspberry définitive : Node/TypeScript ou Python 3 après test ARMv6.
- Méthode d'authentification du backoffice Nuxt.
- Snapshot public ou protégé par un secret de lecture.
- Schéma JSON final et taille maximale acceptée par la PSP.
- Coordonnées et champs Open-Meteo nécessaires.
- Fréquence de rafraîchissement automatique côté PSP.
- Gestion précise des caractères accentués dans l'interface native.
- Méthode future de gestion du rétroéclairage ou de la veille PSP.

## Feuille de route recommandée

### Étape 1 : valider le Raspberry

- Vérifier architecture du système et version Node.
- Vérifier synchronisation NTP et fuseau `Europe/Paris`.
- Configurer une réservation DHCP pour l'adresse du Raspberry.
- Choisir TypeScript ou Python pour le service final.

### Étape 2 : créer le snapshot VPS

- Initialiser application Nuxt dans `apps/web`.
- Créer authentification minimale du backoffice.
- Définir et valider schéma JSON version 1.
- Implémenter écriture atomique sur volume persistant.
- Exposer `GET /snapshot.json` en HTTPS.
- Tester restauration après redéploiement du VPS.

### Étape 3 : enrichir API Raspberry

- Télécharger et valider snapshot VPS en arrière-plan.
- Ajouter cache mémoire et cache disque.
- Ajouter synchronisation météo Open-Meteo.
- Ajouter heure système et état NTP.
- Exposer `GET /api/v1/dashboard`.
- Ajouter indicateurs `stale` et dates de dernière mise à jour.
- Installer service `systemd`.

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
