# Décision technologique — capture NexImage 10 sur Galaxy Z Fold 8

Statut : **architecture retenue, confirmation matérielle encore ouverte**.
Le téléphone et la caméra ne sont pas dans cet environnement. Le prototype
`NexImage Probe` mesure FPS, débit, pertes, latence, CPU et mémoire sur le Fold.
Les chiffres de cet appareil ne sont donc pas inventés ici. Ce qui suit est
décidé à partir des spécifications, du protocole UVC, des limites réelles de
l’API USB Android, et du code ouvert qui pilote déjà cette caméra.

## Caméra

La Celestron NexImage 10 est une UVC USB 3.0, rebadge probable de la
The Imaging Source DFK 33UJ003.

| Propriété | Valeur | Source |
| --- | --- | --- |
| Capteur | ON Semi / Aptina MT9J003, couleur, rolling shutter | Celestron, manuel TIS |
| Pixels actifs | 3856 × 2764, 1,67 µm, 1/2,3" | Celestron |
| Taille UVC observée | 3872 × 2764, 10 702 208 octets | V4L2 (oaCapture, INDIGO) |
| CAN | 12 bits | Celestron |
| Formats documentés DFK 33UJ003 | Bayer 8 bits (GR), Bayer 16 bits (GR), Y800, RGB24, YUY2, Y411 | Rochester Imaging / TIS |
| Format réellement vu sous Linux | `GRBG` 8 bits, FourCC V4L2 `GRBG` | `v4l2-ctl`, INDIGO #421 |
| Débit annoncé | jusqu’à 14 fps pleine trame (TIS) ; Celestron dit 7 fps pleine trame, 94 fps en ROI | fiches |
| Contrôles | exposition absolue (observé 1…300000), gain, ROI matériel | V4L2, Celestron |
| VID:PID | `199e:8619` | rapport oaCapture |
| Alimentation | 770 mA à 5 V, 3,85 W | fiche DFK 33UJ003 |
| Logiciels cités par Celestron | iCap, IC Capture, DirectShow, oaCapture | Celestron |

### GUID `47524247-0000-1000-8000-00aa00389b71`

Ce n’est pas un GUID Windows canonique. C’est le vidage des 16 octets dans
l’ordre mémoire, forme utilisée par INDIGO :

```
47 52 42 47  00 00 10 00  80 00 00 aa  00 38 9b 71
'G' 'R' 'B' 'G'
```

Le noyau Linux nomme cette suite `UVC_GUID_FORMAT_GRBG` et la mappe vers
`V4L2_PIX_FMT_SGRBG8` : **Bayer 8 bits, motif GRBG** (ligne paire G R G R,
ligne impaire B G B G), un octet par pixel, non compressé.

La forme GUID Microsoft du même format est
`47425247-0000-0010-8000-00aa00389b71` (Data1 en little-endian). Les deux
écritures désignent les mêmes octets. Le prototype accepte les deux.

Y800 (`'Y','8','0','0'` + le même suffixe UVC) est le nom que le pilote TIS
donne à **ces mêmes octets relus comme du gris**. oaCapture 1.8 a ajouté
« 8-bit mono-as-raw » exactement pour la NexImage 10. Y800 n’est donc pas
un second capteur : c’est le Bayer brut sans le mot « couleur ».

Le CAN 12 bits n’est pas garanti sur le fil. Le format 16 bits TIS, s’il est
présent dans les descripteurs, place en général les bits utiles dans les
poids forts et met le bas à zéro. Le prototype doit lister tous les formats
non compressés, ouvrir le plus profond, puis mesurer l’entropie réelle
(octet bas toujours nul ou non). Tant que ce test n’a pas tourné sur la
caméra, la capture de référence est **GRBG 8 bits brut**, sans dématriçage
dans le fichier.

### Bande passante

| Mode | Taille trame | fps | Débit utile |
| --- | ---: | ---: | ---: |
| GRBG8 3872×2764 | 10,2 Mio | 7 | ~75 Mo/s |
| GRBG8 pleine trame | 10,2 Mio | 14 | ~150 Mo/s |
| GR16 / Y16 même géométrie | 20,4 Mio | 7 | ~150 Mo/s |
| USB 2.0 utile | | | ~30–40 Mo/s |
| USB 3.0 utile | | | ~300–400 Mo/s |

Pleine trame à 7 fps **ne tient pas en USB 2**. Le Fold 8 a un USB-C 3.1.
Le prototype lit `USBDEVFS_GET_SPEED` : si la négociation retombe en High
Speed, le plan est un ROI matériel, pas un dématriçage plus malin.

Le VBUS est l’autre risque. 770 mA dépasse le budget 500 mA de beaucoup
d’hôtes USB 2. Un port USB 3 a un budget de 900 mA, donc la marge est
faible. Si l’énumération échoue ou si la caméra se déconnecte sous charge,
il faut un hub alimenté. Ce n’est pas un problème de framework.

## Ce que l’API Android permet vraiment

`UsbRequest` et `bulkTransfer` ne font que du **bulk** et de l’**interrupt**.
Le transfert isochrone n’existe pas dans le SDK Java. Les caméras UVC
historiques streament en isochrone ; plusieurs USB 3 TIS streament en bulk.
Il faut les deux.

Le motif qui marche sans root :

1. `UsbManager` ouvre l’appareil et donne un descripteur de fichier.
2. Les transferts de contrôle UVC (PROBE, COMMIT, exposition, gain, ROI)
   passent par `UsbDeviceConnection.controlTransfer`.
3. Le flux vidéo passe par `ioctl` usbfs sur ce fd :
   `USBDEVFS_SUBMITURB` / `USBDEVFS_REAPURBNDELAY`, type ISO ou BULK.
4. `USBDEVFS_SETINTERFACE` choisit l’alternate setting dont la bande
   passante couvre `dwMaxPayloadTransferSize`.

`libusb` sur Android utilise ces mêmes ioctl. Ses mainteneurs déconseillent
libusb/libuvc pour l’isochrone Android (pertes de paquets, issue libusb
#1504). `libuvc` soumet par défaut 32 paquets par URB ; sur plusieurs SoC
Android cela renvoie `ENOMEM` (issue libuvc #299). La valeur stable observée
est souvent 8 à 12. Un empilement libusb n’apporte donc pas l’accès bas
niveau : il ajoute une couche qui perd des paquets.

Camera2 / External USB Camera d’Android n’expose pas les GUID Bayer
inconnus, ni le COMMIT UVC, ni un fichier brut sans conversion du HAL.
Inutilisable pour la priorité 1.

## Bibliothèques examinées

| Projet | Licence | État | Accès brut NexImage 10 |
| --- | --- | --- | --- |
| libusb 1.0.27+ | LGPL-2.1 | actif, mais « ne pas utiliser libusb pour l’UVC Android » | isochrone fragile, LGPL |
| libuvc | BSD | release 2017, commits ensuite ; pas d’API Bayer (constat INDIGO) | GUID souvent ignoré, `ENOMEM` Android |
| saki4510t/UVCCamera | Apache 2.0 + libusb LGPL | figé vers 2017, libuvc de 2016 | preview YUYV/MJPEG, Bayer absent |
| shiyinghan/UVCAndroid 1.0.13 | Apache 2.0 | utilisé, ~100 issues ouvertes | callbacks NV21/RGBA, pas le brut propriétaire |
| jiangdongguo/AndroidUSBCamera | Apache 2.0 | dernier essor 2023–2024 | OpenGL preview, pas un enregistreur astronomique |
| ernestp/AndroidUSBCamera | Apache 2.0 | fork de mars 2026, Android 16, pages 16 Ko | même modèle preview, jeune |
| pilote UVC du noyau + V4L2 | GPL | excellent sur Linux | `/dev/video` n’est pas accessible à une appli Android non privilégiée |
| CameraX / Camera2 | SDK Android | actif | pas de Bayer UVC arbitraire |
| OpenCV | Apache 2.0 | actif | dématriçage CPU, trop tard et trop cher si le fil USB n’est pas tenu |

Code astronomique réutilisable comme **référence**, pas comme dépendance :

| Projet | Apport | Licence à respecter |
| --- | --- | --- |
| oaCapture | NexImage 10, Y800 relu en GRBG, ROI, dématriçage au preview seulement | GPL : on ne copie pas le code |
| INDIGO `ccd_uvc` | le GUID est détecté ; le dématriçage n’est pas le travail du pilote | ne pas vendor |
| guvcview | table des GUID, dont GBRG/GRBG | GPL, même règle |
| Siril | confirmation terrain du motif GRBG | hors chemin temps réel |

Aucune de ces bibliothèques Android ne donne à la fois : GUID inconnu,
zéro conversion, URB calibrés pour Android, compteur de `ERR`/`EOF`, et
fichier octet-pour-octet. Les reprendre pour « aller plus vite » remet
une copie et un dématriçage au milieu du chemin critique.

## GPU du Fold 8

Galaxy Z Fold 8 : Snapdragon 8 Elite Gen 5 for Galaxy, **Adreno 840**,
Android 17, USB-C 3.1, écran interne 7,6" 2448×1848.

Le dématriçage bilinéaire d’une trame de 10,7 Mpx et un histogramme 256
niveaux sont petits pour cet Adreno. Le goulet est l’USB, pas le shader.
Une copie `glTexSubImage2D` de 10 Mio à 14 fps représente ~150 Mo/s, à
comparer à la bande passante LPDDR5X du téléphone (dizaines de Go/s).

| API | Intérêt ici | Coût |
| --- | --- | --- |
| OpenGL ES 3.0, texture `R8` + fragment shader GRBG | preview couleur, histogramme en compute ES 3.1, disponible sur Adreno | une copie vers la texture |
| Vulkan + `AHardwareBuffer` | supprime cette copie, synchronisation explicite | beaucoup plus de code, **aucun fps USB en plus** |
| CPU (OpenCV, RenderScript) | RenderScript est retiré ; le CPU doit rester disponible pour récolter les URB | déconseillé sur le chemin preview |

Vulkan sera justifié seulement si une trace montre que la copie GL fait
tomber une trame. Il n’est pas la première brique. Le prototype preview
en GLES 3.0, avec un bouton pour cycler GRBG / GBRG / RGGB / BGGR : le
motif se vérifie à l’œil (le mauvais motif verdit le disque planétaire)
et par un score de corrélation.

## Comparaison des architectures

Légende : **oui** = la propriété est naturelle dans cette architecture,
**non** = elle est structurellement mauvaise, **via NDK** = il faut écrire
le même code natif que l’option D, plus un pont.

| Critère | A Expo + RN + dev build | B RN sans Expo | C Kotlin/Compose seul | D Kotlin + NDK C/C++ | E D + OpenGL ES | F D + Vulkan | G libusb/libuvc NDK | H USB Host Java seul |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Accès USB | via NDK | via NDK | contrôle seulement | usbfs direct | comme D | comme D | indirect, fragile | bulk/contrôle, **pas d’iso** |
| NexImage 10 | si le module natif existe | idem | non pour le flux iso | oui | oui | oui | partiel | seulement si endpoint bulk |
| RAW / Bayer | le pont convertit souvent | idem | non | octets bruts | preview GPU, fichier brut | idem | GUID mal exposés | si bulk et sans conversion |
| Contrôle UVC | à écrire en natif | idem | `controlTransfer` oui | oui | oui | oui | partiel | oui pour le contrôle |
| Format propriétaire | à parser soi-même | idem | descripteurs oui, flux non | oui | oui | oui | si le GUID est dans l’enum | descripteurs seulement |
| FPS max | copie JNI/JS en plus | idem | bon si bulk | le meilleur | identique à D | identique à D | pertes documentées | bon si bulk |
| Latence preview | haute (bridge) | haute | moyenne | basse | basse | la plus basse si la copie GL compte | moyenne | moyenne |
| Copies mémoire | ≥ 3 | ≥ 3 | 1 à 2 | 1 (URB → trame) | +1 vers GPU | +0 si import mémoire | 2+ | 1 à 2 |
| CPU | JS + conversion | idem | assemblage | assemblage seul | shader, CPU libre | CPU libre | libuvc + copies | assemblage |
| GPU | possible, tard | possible | oui | non sans E/F | oui, suffisant | oui, gain marginal | à ajouter | oui |
| Capture sans perte | le bridge jette des trames | idem | possible en bulk | oui, fichier = tampon | preview ≠ fichier | idem | non garanti | possible en bulk |
| Stabilité | runtime RN + NDK | idem | bonne | bonne | bonne | plus de surface de bugs | `ENOMEM`, paquets perdus | bonne, incomplète |
| Android 17 | dev build à regénérer | idem | oui | oui | oui | oui | correctifs locaux permanents | oui |
| Fold 8 | Compose n’est pas là ; RN fold est un surcoût | idem | WindowSizeClass, charnière | UI en Compose | idem | idem | pas d’UI | UI en Compose |
| Complexité | la plus haute (deux mondes) | haute | insuffisante | moyenne | moyenne | haute | moyenne, mauvaise cible | faible, plafond USB |
| Maintenance | Expo + RN + fork natif | RN + fork | Google | Google + notre code UVC | Google | Google | libusb LGPL, libuvc lent | Google |
| Dépendances | Node, Expo, Hermes, NDK | Node, RN, NDK | AndroidX | NDK | GLES du système | Vulkan du système | LGPL + BSD | aucune |
| Licence | mélange | mélange | Apache | Apache, code à nous | idem | idem | LGPL-2.1 contaminante si lien statique | Apache |
| Déploiement sur le Fold | Expo Go **impossible** ; APK dev build | APK | APK | APK | APK | APK | APK | APK |

Expo Go est écarté : il ne charge pas ce module natif. Un development build
Expo serait une coquille autour du même NDK, avec un pont qui copie des
trames de 10 Mio. La facilité de développement perd les priorités 1 à 5.

React Native sans Expo a le même pont. Il n’achète rien sur l’USB.

Kotlin seul (colonne C et H) est la bonne UI et le bon chemin de contrôle,
et il est **incomplet** le jour où l’endpoint vidéo est isochrone. La
NexImage 10 peut être bulk (USB 3 TIS) ou iso. L’application doit démarrer
dans les deux cas.

libusb/libuvc (colonne G) est le réflexe desktop. Sur Android il est le
chemin dont les bugs ouverts parlent de paquets perdus. On s’en sert comme
spec, on ne l’embarque pas.

Vulkan (colonne F) n’augmente pas le fps USB. OpenGL ES 3.0 fait le
dématriçage et le preview. Vulkan reste une optimisation mesurée, pas le
socle.

## Architecture recommandée

**Kotlin, Jetpack Compose, C/C++ NDK, usbfs, OpenGL ES 3.0.**
Pas Expo. Pas React Native. Pas libusb. Pas Camera2. Vulkan en réserve.

```
Galaxy Z Fold 8
  UsbManager                permission, ouverture, fd
  controlTransfer           PROBE, COMMIT, exposition, gain, ROI
  descripteurs bruts        GUID, y compris inconnus, alternate settings
        │
  NDK  uvc_pump             USBDEVFS_SUBMITURB  ISO ou BULK
        │                   8–16 URB, ≤ 12 paquets iso
  uvc_assemble              en-tête UVC, FID, EOF, bit ERR
        │                   une copie URB → tampon de trame
        ├─ fichier .raw     octets capteur, aucun dématriçage
        └─ texture GL_R8    shader GRBG, preview seulement
  Compose                   nuit (fond noir, rouge faible), charnière Fold
```

Pourquoi ce découpage, dans l’ordre des priorités :

1. **Qualité.** Le fichier est le tampon UVC après retrait de l’en-tête de
   payload, rien d’autre. Le dématriçage n’existe que dans le shader.
2. **Trames perdues.** Le fil qui récolte les URB ne dématrice pas, n’écrit
   pas le disque de façon synchrone, et ne passe pas par un pont JS. Les
   compteurs `ERR`, trame incomplète et file pleine sont affichés.
3. **USB/UVC et Bayer.** Le parseur lit le GUID 16 octets. Un format absent
   de libuvc reste sélectionnable. Y800 et GRBG sont tous les deux gardés.
4. **FPS.** L’alternate setting est le plus petit qui couvre
   `dwMaxPayloadTransferSize`. La vitesse USB est lue, pas supposée.
5. **Latence.** Dernière trame complète uniquement. Le preview peut sauter ;
   le compteur de sauts est séparé du compteur de pertes USB.
6. **Contrôles.** `SET_CUR` / `GET_MIN` / `GET_MAX` sur l’unité caméra et
   l’unité processing, plus le ROI des descripteurs de frame. Le prototype
   expose d’abord l’exposition et le gain quand les descripteurs les
   annoncent.
7. **Sans perte.** Bouton « une trame » : `.raw` + JSON (GUID, largeur,
   hauteur, bpp, pts). Le conteneur SER/FITS viendra après le test de format.
8. **GPU.** Fragment shader. Le CPU reste sur usbfs.
9. **Stabilité Fold.** Une activité, `FLAG_KEEP_SCREEN_ON`, pas de service
   caché. APK `arm64-v8a` seulement : le Fold 8 est arm64, et le layout
   `usbdevfs_urb` doit être celui du noyau 64 bits.
10. **UI.** Compose, thème nuit, colonne de mesures à côté du preview quand
    l’écran interne est ouvert.
11. **Déploiement.** `./gradlew :app:assembleDebug`, copie de l’APK, installation
    locale. Pas de compte Expo, pas de store.

Le prototype qui suit est cette architecture réduite au chemin
énumération → négociation UVC → tampon natif → preview, plus les mesures.
Il ne contient pas encore l’enregistrement continu, le stacking ni la
cartographie complète des contrôles. Ces pièces n’apportent rien tant que
la vitesse USB et le GUID ne sont pas lus sur le Fold.

## Ce que le prototype doit trancher

| Question | Mesure |
| --- | --- |
| USB 3 ou repli USB 2 ? | `USBDEVFS_GET_SPEED` |
| Bulk ou isochrone ? | `bmAttributes` de l’endpoint VS |
| Y800, GRBG, ou un 16 bits ? | liste des descripteurs VS |
| Le 16 bits porte-t-il 12 bits utiles ? | entropie de l’octet bas sur une trame sauvée |
| Le motif est-il bien GRBG ? | score de mosaïque + cycle visuel des 4 motifs |
| Quel fps, quel débit, quelles pertes ? | compteurs de la pompe, fenêtre d’une seconde |
| Le téléphone tient-il 3,85 W ? | `bMaxPower`, et une déconnexion sous flux |
| GLES absorbe-t-il le preview ? | âge de la trame affichée, CPU, mémoire |

Si le flux tient le fps annoncé sans bit `ERR` et sans trame courte, cette
architecture est confirmée. Si le repli est USB 2, on garde la même pile et
on impose un ROI. Si l’isochrone Android perd des paquets malgré 8 URB de
8 paquets, on bascule l’alternate setting bulk quand il existe, avant
d’envisager un autre système. On ne revient pas à Expo pour ça.
