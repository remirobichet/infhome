# Contrat snapshot Infhome v1

Décision du 12 septembre 2026. Référence exécutable : `apps/web/shared/snapshot.ts`.

## Transport public

`GET https://infhome.remirobichet.fr/snapshot.json` est public et ne nécessite aucun secret de lecture.

- JSON UTF-8, sans mise en forme, corps limité à **4 096 octets**.
- `Content-Type: application/json; charset=utf-8`.
- `Content-Length` envoyé ; pas de compression ni de transfert chunked par le serveur Nuxt.
- `Cache-Control: no-store` : la sélection des événements évolue avec la date courante.
- 503 tant qu'aucune publication n'existe ; 200 pour un contenu vide explicitement publié.
- Une erreur de lecture ou de validation du stockage renvoie une erreur HTTP, jamais un faux document vide.

Le Raspberry doit télécharger le document entier à chaque synchronisation réussie, même lorsque `updatedAt` n'a pas changé. Il conserve le précédent document en cas d'erreur et insère le snapshot reçu sans modification dans `content`.

## Document public

```json
{
  "version": 1,
  "updatedAt": 1789200000,
  "shopping": ["Pain", "Lait"],
  "agenda": [
    {
      "title": "Week-end en famille",
      "startDate": "2026-09-19",
      "endDate": "2026-09-20",
      "time": "10:00"
    },
    {
      "title": "Dentiste",
      "startDate": "2026-09-23",
      "endDate": null,
      "time": "18:30"
    }
  ]
}
```

### Champs

| Champ | Contrat |
| --- | --- |
| `version` | Entier égal à 1. |
| `updatedAt` | Timestamp Unix en secondes de la dernière publication manuelle, pas de la sélection automatique. |
| `shopping` | 0 à 20 chaînes non vides, chacune limitée à 64 octets UTF-8. Ordre saisi conservé. |
| `agenda` | 0 à 3 événements sélectionnés. |
| `title` | Chaîne non vide, 96 octets UTF-8 maximum. |
| `startDate` | Date civile valide `YYYY-MM-DD`, de 2000 à 2099 inclus. |
| `endDate` | Même format, supérieure ou égale au début, ou `null` pour un événement d'un jour. Dernier jour inclus. |
| `time` | Heure locale `HH:mm`, 00:00 à 23:59, ou `null`. Pour une période, heure du premier jour. |

Les dates et heures sont interprétées dans **Europe/Paris**. Les dates civiles utilisent volontairement un format ISO court : elles représentent des jours du calendrier, sans conversion arbitraire vers un timestamp UTC. Les métadonnées de publication restent en secondes Unix.

Les chaînes sont nettoyées des espaces en début et fin, les caractères de contrôle sont refusés. Les accents sont conservés en UTF-8 ; leur rendu dans le client C reste à valider avant de connecter celui-ci. Le document ne contient aucun identifiant d'administration ni aucune révision interne.

## Sélection des trois événements

À chaque lecture publique :

1. Écarter les événements dont le dernier jour est passé à Paris.
2. Pour un événement d'un seul jour avec heure, l'écarter lorsque sa minute de début est passée. Il n'a pas de durée implicite ; utiliser une période pour un événement qui doit rester présent plusieurs jours.
3. Pour un événement sans heure, conserver toute la journée. Pour une période de plusieurs jours, conserver jusqu'à la fin du dernier jour, même si l'heure du début est passée.
4. Trier par date de début, puis par heure (sans heure avant les événements horaires). Les périodes déjà commencées précèdent donc les événements futurs.
5. En cas d'égalité, utiliser l'identifiant interne pour un ordre stable ; conserver les trois premiers.

Il n'y a aucune limite d'horizon : un rendez-vous dans plusieurs années peut être sélectionné. S'il reste moins de trois événements, la liste est simplement plus courte.

## Document persistant et administration

Le fichier `${NUXT_DATA_DIR}/snapshot.json` contient le document publié complet :

- `version`, `updatedAt`, `shopping` ;
- `agenda` avec jusqu'à **100 événements**, chacun possédant en plus un `id` UUID unique ;
- `revision`, UUID renouvelé à chaque publication, utilisé pour détecter les conflits.

Le fichier persistant n'est **pas servi directement par le proxy** : la route Nitro réalise la projection publique. Aucun fichier n'est réécrit simplement parce qu'un événement devient passé.

`GET /api/admin/snapshot` renvoie ce document à l'administrateur connecté. Lors du premier démarrage sans fichier, il renvoie des listes vides, `updatedAt: 0` et `revision: null`.

`POST /api/admin/snapshot` attend exactement :

```json
{
  "revision": null,
  "shopping": ["Pain"],
  "agenda": []
}
```

Après la première publication, remplacer `null` par la dernière révision reçue. Les événements ajoutés possèdent un UUID généré dans le navigateur. Le serveur génère la version, la révision et la date de publication. Les champs inconnus sont refusés.

Le corps d'administration et le fichier persistant sont limités à **65 536 octets**, indépendamment de la limite publique de 4 Kio. La publication est sérialisée dans un seul processus : lecture de la révision, écriture temporaire, synchronisation du fichier, fermeture, renommage atomique. Une ancienne révision reçoit 409.

## Suite Raspberry / PSP

Le contrat ci-dessus est implémenté côté Nuxt et validé par le Raspberry. Le Raspberry expose désormais `/api/v1/dashboard` ; le client PSP utilise encore `/api/status` et devra être adapté. Voir [`DASHBOARD_CONTRACT.md`](DASHBOARD_CONTRACT.md).

La limite de 4 Kio concerne uniquement le snapshot VPS. Prévoir une limite distincte pour la réponse agrégée Raspberry ; **8 Kio de corps JSON** est une recommandation initiale à confirmer lors de l'implémentation C, avec un espace séparé et borné pour les en-têtes HTTP.

Le Raspberry devra dater ses téléchargements réussis séparément de `content.updatedAt` pour détecter un cache périmé. Le fait qu'aucune édition n'ait été publiée récemment ne signifie pas que la synchronisation est en panne.
