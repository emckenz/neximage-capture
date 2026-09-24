# NexImage Probe — Prototype de validation USB/UVC

Prototype minimal pour valider la chaîne :

```
Samsung Galaxy Z Fold 8 → USB-C OTG → Celestron NexImage 10
    → énumération USB → frame Y800/GRBG → buffer natif → preview OpenGL ES
```

## Prérequis

- Android Studio Ladybug+ ou JDK 17 + Android SDK 35 + NDK 27
- Samsung Galaxy Z Fold 8 avec USB-C OTG
- Celestron NexImage 10 (VID `199e`, PID `8619`)
- Câble USB-C OTG ou adaptateur

## Build

```bash
export ANDROID_HOME=$HOME/Android/Sdk   # ou votre chemin SDK
cd prototype/neximage-probe
./gradlew assembleDebug
```

APK produit : `app/build/outputs/apk/debug/app-debug.apk`

## Installation sur Fold 8

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Autoriser l'installation depuis sources inconnues si nécessaire.

## Utilisation

1. Brancher la NexImage 10 via USB-C OTG
2. Lancer **NexImage Probe**
3. Accorder la permission USB quand demandé
4. Vérifier l'énumération des formats UVC affichée
5. Tester **Y800** en 640×480 (débit maximal, pas de debayer)
6. Tester **GRBG** en 640×480 puis **Full frame** 3872×2764
7. Observer les métriques : FPS, débit USB, latence, frames perdues, CPU, mémoire

## Métriques attendues (référence Linux/INDIGO)

| Mode | FPS attendu | Débit USB |
|------|-------------|-----------|
| Y800 640×480 | 30–94 | ~15–30 Mbps |
| GRBG 640×480 | 30–94 | ~15–30 Mbps |
| GRBG 3872×2764 | ~7 | ~530 Mbps théorique, ~400 Mbps effectif |

> Si `nativeStartStream` échoue avec ENOMEM, le patch `packets_per_transfer=8` dans libuvc est requis (déjà prévu dans l'architecture cible).

## Analyse GUID

L'application affiche automatiquement l'analyse du GUID `47524247-0000-1000-8000-00aa00389b71` → **GRBG 8-bit Bayer**.

## Architecture

Voir [`docs/TECHNOLOGY-DECISION.md`](../../docs/TECHNOLOGY-DECISION.md) pour la décision technologique complète.
