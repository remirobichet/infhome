# Contrat dashboard Raspberry v1

Implémenté le 13 septembre 2026. Route locale : `GET http://192.168.0.104:8080/api/v1/dashboard`.

## Transport

JSON UTF-8, maximum 8 192 octets de corps, `Content-Length`, `Connection: close`, `Cache-Control: no-store`. Pas de gzip ni de transfert chunked. Requêtes HTTP/1.0 acceptées. Les en-têtes nécessitent un buffer distinct côté PSP. `HEAD` est accepté sur les routes de lecture ; les méthodes d'écriture y reçoivent 405.

### Actualisation manuelle

`POST /api/v1/dashboard/refresh`, sans corps (`Content-Length: 0`), récupère et persiste le snapshot courses/agenda avant de renvoyer le même dashboard avec HTTP 200. La météo reste en cache. Les autres méthodes sur cette route reçoivent 405 (`Allow: POST`).

Les synchronisations de contenu simultanées, automatiques ou manuelles, partagent le même téléchargement. Un échec de téléchargement, de validation ou de persistance renvoie HTTP 502 avec `{"error":"Content refresh failed"}` ; le cache précédent reste disponible via GET. Aucun ancien contenu n'est présenté comme une actualisation manuelle réussie.

Le téléchargement du contenu utilise `INFHOME_TIMEOUT_SECONDS` (8 s par défaut), plafonné à 45 s. La connexion serveur du POST autorise 55 s d'inactivité et la PSP attend au maximum 60 s pour cette requête HTTP. La connexion Wi-Fi éventuelle est une étape distincte. Les GET conservent leur délai de 10 s côté PSP.

Croix déclenche le POST en mode réel ; les clics pendant une opération réseau sont ignorés. L'ancien affichage reste visible pendant la requête et en cas d'erreur. La PSP continue ses GET automatiques toutes les 60 s ; les synchronisations Raspberry restent toutes les 15 min par défaut.

## Exemple

```json
{
  "version": 1,
  "generatedAt": 1789300800,
  "time": {
    "unix": 1789300800,
    "timezone": "Europe/Paris",
    "synced": true
  },
  "weather": {
    "date": "2026-09-13",
    "morning": { "max": 23.4, "code": 3, "label": "Couvert" },
    "evening": { "max": 28.1, "code": 61, "label": "Pluvieux" },
    "fetchedAt": 1789300700,
    "stale": false
  },
  "content": {
    "version": 1,
    "updatedAt": 1789200000,
    "shopping": ["Pain", "Lait"],
    "agenda": []
  },
  "contentSync": {
    "fetchedAt": 1789300700,
    "stale": false
  }
}
```

## Sémantique

- `generatedAt`, `time.unix`, `fetchedAt` : timestamps Unix entiers en secondes. L'heure système n'est pas une donnée téléchargée.
- `time.synced` : `true` si NTP synchronisé, `false` si non synchronisé, `null` si la vérification est indisponible. Contrôle toutes les 60 secondes.
- `content` : snapshot public v1 inchangé, selon [`SNAPSHOT_CONTRACT.md`](SNAPSHOT_CONTRACT.md), ou `null` sans cache valide. Le Raspberry ne filtre pas lui-même les événements, même hors ligne.
- `contentSync.fetchedAt` : dernier téléchargement validé et persisté, ou `null`. Distinct de la date de publication `content.updatedAt`.
- `contentSync.stale` : vrai sans cache, après plus de deux intervalles sans succès, ou si le timestamp du cache est dans le futur.
- `weather` : météo valide mise en cache, ou `null`. `date` est la date civile de la prévision, à Paris.
- `morning` : 00:00–11:59. `evening` : 12:00–23:59. Les deux créneaux sont conservés toute la journée.
- `max` : maximum des températures horaires prévues, en °C, potentiellement décimal.
- `code` : code WMO Open-Meteo représentatif du créneau. Priorité aux orages, puis précipitations verglaçantes, neige et pluie. Sinon code dominant. À fréquence égale, code numérique le plus élevé.
- `label` : libellé français UTF-8 du code ; le rendu des accents doit être vérifié sur PSP.
- `weather.stale` : mêmes règles de fraîcheur que le contenu, plus date différente du jour courant. Une météo de la veille ne doit pas être présentée comme celle d'aujourd'hui.

Le dashboard renvoie HTTP 200 même si une source manque ; le client examine les valeurs `null` et les indicateurs de fraîcheur. Une source indisponible ne bloque pas les autres. Au changement de date, les nouvelles prévisions apparaissent au prochain cycle de 15 minutes.

`GET /api/status` conserve exactement `{"message":"Hello from Raspberry Pi"}` pour le client historique. Il ne garantit ni Internet ni la disponibilité des données.
