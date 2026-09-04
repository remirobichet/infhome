# Contexte PSP — Projet Infhome

## Objectif du projet

Transformer une vieille **PSP-2004** en écran d'information mural piloté par un **Raspberry Pi Zero W**.

Le Raspberry Pi doit :

- rester allumé en permanence ;
- récupérer les données à afficher ;
- gérer un futur capteur de présence ;
- servir les données à la PSP via le réseau local.

La PSP doit :

- fonctionner sans batterie, uniquement sur secteur ;
- afficher les informations ;
- à terme pouvoir éteindre/rallumer son écran ou son rétroéclairage selon la présence détectée par le Raspberry Pi.

---

# Matériel PSP

Modèle identifié :

```text
PSP-2004
```

Il s'agit d'une **PSP Slim & Lite série 2000**, version européenne.

La batterie a été retirée.

La PSP fonctionne correctement **sur secteur uniquement**, sans batterie.

C'est intéressant pour un usage fixe H24 car on évite de laisser une vieille batterie Li-ion continuellement en charge.

---

# Wi-Fi de la PSP

La PSP est trop ancienne pour les configurations Wi-Fi modernes classiques.

Dans les paramètres WLAN de la PSP, les choix disponibles sont :

```text
Aucun
WEP
WPA PSK TKIP
WPA PSK AES
```

Routeur utilisé :

```text
TP-Link Archer C80
```

Les choix de sécurité disponibles sur le routeur sont :

```text
None
WPA2-PSK[AES]
WPA2-PSK[AES]+WPA-PSK[TKIP]
WPA3-Personal
WPA3-Personal+WPA2-PSK[AES]
Portal
```

La configuration utilisée pour rendre le réseau compatible avec la PSP est :

```text
Routeur :
WPA2-PSK[AES]+WPA-PSK[TKIP]

PSP :
WPA PSK TKIP
```

La PSP parvient alors correctement à :

- voir le réseau ;
- s'authentifier ;
- recevoir une adresse IP.

---

# Erreur Internet / DNS

Le test réseau PSP indique :

```text
Adresse IP : OK
Connexion Internet : Échec
```

Erreur obtenue :

```text
80410410
```

avec le message :

```text
Erreur de la communication avec le serveur.
Une erreur DNS est survenue.
```

Un test avec DNS manuels a été effectué :

```text
DNS primaire : 1.1.1.1
DNS secondaire : 8.8.8.8
```

Cela n'a pas corrigé le test Internet de la PSP.

Mais cette erreur n'est **pas bloquante pour Infhome**.

---

# Validation réseau local PSP ↔ Raspberry Pi

Le Raspberry Pi a servi une page HTTP locale avec :

```bash
mkdir -p ~/psp-test
cd ~/psp-test

echo '<html><body><h1>Hello PSP</h1></body></html>' > index.html

python3 -m http.server 8080
```

Depuis le navigateur PSP, accès via :

```text
http://IP_DU_RASPBERRY:8080
```

Résultat :

```text
Hello PSP
```

affiché correctement sur la PSP.

Conclusion :

```text
PSP → Wi-Fi → Raspberry Pi en HTTP local : OK
```

Donc la PSP n'a pas besoin d'un accès Internet direct.

Architecture possible :

```text
Internet
   │
   ▼
Raspberry Pi Zero W
   │
   ├── récupère météo / agenda / autres APIs
   ├── futur capteur de présence
   └── serveur HTTP local
            │
            │ Wi-Fi
            ▼
         PSP-2004
```

---

# Test HTML / CSS / JavaScript

Une page de test 480×272 a été créée avec :

- HTML ;
- CSS ;
- JavaScript ;
- horloge JavaScript avec `setInterval()`.

La PSP a correctement affiché :

```text
INFHOME

HTML OK
CSS OK
JavaScript OK

HH:MM:SS
```

avec l'heure mise à jour chaque seconde.

Conclusion :

Le navigateur PSP est suffisamment fonctionnel pour une interface locale très simple en :

- HTML ancien ;
- CSS basique ;
- JavaScript ES3/ES5 simple.

Mais pour aller plus loin, une application native PSP a été privilégiée.

---

# Firmware de la PSP

Version système initiale :

```text
6.60
```

Une tentative de mise à jour vers 6.61 a été faite.

L'updater officiel refuse l'installation sans batterie avec le message :

```text
Le niveau de batterie est faible.
Connectez l'adaptateur AC pour recharger la batterie.
```

Comme aucune batterie n'est disponible et qu'il n'est pas souhaité d'en acheter une juste pour ça, la mise à jour 6.61 a été abandonnée.

Ce n'est pas un problème car ARK fonctionne aussi sur 6.60.

---

# Installation du Custom Firmware ARK

ARK a été installé en mode LIVE.

Version maintenant affichée :

```text
6.60 ARK-5 LIVE
```

Donc :

- la PSP est toujours en firmware 6.60 ;
- ARK est actif ;
- les homebrews peuvent être lancés ;
- l'installation n'est pas encore permanente.

En cas d'arrêt complet de la PSP, il faut simplement relancer ARK Loader.

Il a volontairement été décidé de **ne pas installer ARK de façon permanente pour le moment**.

---

# Premier homebrew PSP natif

Objectif :

Créer une application native PSP nommée :

```text
Infhome
```

au lieu d'utiliser le navigateur Sony.

Développement effectué depuis :

```text
Windows 11 + WSL2
```

Toolchain utilisée :

```text
Docker
+
image pspdev/pspdev
+
PSPSDK
```

Projet créé dans :

```bash
~/projects/infhome-psp
```

---

# Fichier main.c

Le premier homebrew affiche simplement du texte à l'écran.

Code de base utilisé :

```c
#include <pspuser.h>
#include <pspdebug.h>
#include <pspdisplay.h>

PSP_MODULE_INFO("Infhome", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

int exit_callback(int arg1, int arg2, void *common)
{
    sceKernelExitGame();
    return 0;
}

int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback(
        "Exit Callback",
        exit_callback,
        NULL
    );

    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();

    return 0;
}

int setup_callbacks(void)
{
    int thid = sceKernelCreateThread(
        "callback_thread",
        callback_thread,
        0x11,
        0xFA0,
        0,
        0
    );

    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);

    return thid;
}

int main(void)
{
    setup_callbacks();

    pspDebugScreenInit();
    pspDebugScreenClear();

    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("INFHOME\n\n");
    pspDebugScreenPrintf("Hello from native PSP homebrew!\n");
    pspDebugScreenPrintf("Raspberry Pi -> PSP incoming...\n");

    while (1)
    {
        sceDisplayWaitVblankStart();
    }

    return 0;
}
```

---

# CMakeLists.txt

Configuration utilisée :

```cmake
cmake_minimum_required(VERSION 3.11)

project(infhome)

add_executable(${PROJECT_NAME}
    main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pspdebug
    pspdisplay
    pspge
)

create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Infhome"
    VERSION 01.00
)
```

---

# Compilation du homebrew

Image Docker utilisée :

```bash
docker pull pspdev/pspdev:latest
```

Compilation :

```bash
cd ~/projects/infhome-psp

docker run --rm -it \
  -v "$PWD:/source" \
  pspdev/pspdev:latest
```

Dans le container :

```bash
cd /source

mkdir -p build
cd build

psp-cmake ..
make
```

Le build génère notamment :

```text
EBOOT.PBP
```

---

# Installation du homebrew sur la PSP

Le fichier compilé a été copié dans :

```text
PSP/
└── GAME/
    └── INFHOME/
        └── EBOOT.PBP
```

Depuis le XMB de la PSP :

```text
Jeu
→ Memory Stick
→ Infhome
```

Le homebrew se lance correctement.

Résultat affiché :

```text
INFHOME

Hello from native PSP homebrew!
Raspberry Pi -> PSP incoming...
```

Le lancement natif fonctionne donc.

---

# État actuel du projet

Tout ceci est validé :

```text
PSP-2004
    │
    ├── fonctionne sans batterie sur secteur
    │
    ├── Wi-Fi WPA-PSK/TKIP : OK
    │
    ├── obtention IP : OK
    │
    ├── HTTP local vers Raspberry Pi : OK
    │
    ├── navigateur HTML/CSS/JS : OK
    │
    ├── firmware 6.60 : OK
    │
    ├── ARK-5 LIVE : OK
    │
    └── homebrew natif Infhome : OK
```

---

# Architecture cible

La cible souhaitée est maintenant :

```text
                    Internet
                       │
                       ▼
              ┌────────────────┐
              │ Raspberry Pi   │
              │ Zero W         │
              │                │
              │ météo          │
              │ agenda         │
              │ rappels        │
              │ autres données │
              └───────┬────────┘
                      │
             ┌────────┴─────────┐
             │                  │
             │ GPIO             │ Wi-Fi local
             ▼                  ▼
       Presence sensor     ┌─────────────┐
                           │ PSP-2004    │
                           │ ARK-5       │
                           │ Infhome app │
                           └─────────────┘
```

---

# Prochaine étape souhaitée

Faire communiquer le homebrew natif avec le Raspberry Pi.

Premier objectif :

Le Raspberry Pi expose une route :

```text
http://IP_DU_PI:8080/api/status
```

qui renvoie par exemple :

```json
{
  "message": "Hello from Raspberry Pi"
}
```

Le homebrew PSP devra :

1. initialiser le réseau PSP ;
2. utiliser la configuration Wi-Fi existante ;
3. se connecter au Raspberry Pi ;
4. effectuer une requête HTTP locale ;
5. récupérer la réponse ;
6. afficher :

```text
Hello from Raspberry Pi
```

directement dans le homebrew.

Une fois cela validé :

```text
PSP homebrew
    ↓
HTTP local
    ↓
Raspberry Pi API
```

les étapes suivantes seront :

1. créer une vraie API Infhome sur le Raspberry Pi ;
2. récupérer météo / agenda / autres données ;
3. créer une vraie UI native PSP en 480×272 ;
4. brancher le capteur de présence au Raspberry Pi ;
5. transmettre l'état de présence à la PSP ;
6. explorer les API PSPSDK permettant de gérer :
   - luminosité ;
   - rétroéclairage ;
   - veille ;
   - réveil éventuel ;

7. obtenir le comportement final :

```text
Présence détectée
        ↓
Raspberry Pi
        ↓
PSP affiche le dashboard

Absence pendant X minutes
        ↓
Raspberry Pi
        ↓
PSP coupe ou réduit son affichage
```

L'objectif final est donc d'utiliser la PSP comme **terminal d'affichage natif dédié à Infhome**, sans dépendre du navigateur Web Sony.
