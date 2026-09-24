# Astrophotography — NexImage 10 on Android

Projet d'application d'astrophotographie pour **Samsung Galaxy Z Fold 8** + **Celestron NexImage 10** via USB-C OTG.

## Contenu actuel

| Chemin | Description |
|--------|-------------|
| [`docs/TECHNOLOGY-DECISION.md`](docs/TECHNOLOGY-DECISION.md) | Analyse comparative des architectures + **recommandation finale** |
| [`prototype/neximage-probe/`](prototype/neximage-probe/) | Prototype Android natif de validation USB/UVC |

## Décision architecturale (résumé)

**Android natif Kotlin + Jetpack Compose + NDK (libusb/libuvc) + OpenGL ES 3.2**

Expo / React Native sont **écartés** : trop de copies mémoire, pas d'accès Bayer RAW UVC, latence incompatible avec l'astrophotographie planetary.

Voir le document complet pour la justification détaillée.

## Prototype de validation

```bash
cd prototype/neximage-probe
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Le prototype mesure : FPS, débit USB, latence, frames perdues, CPU, mémoire — et teste les formats **Y800** et **GRBG**.

## Matériel requis

- Samsung Galaxy Z Fold 8
- Celestron NexImage 10 (USB VID `199e`, PID `8619`)
- Adaptateur USB-C OTG

## Prochaines étapes

1. Valider le prototype sur Fold 8 réel
2. Confirmer FPS/drops à plein cadre 3872×2764
3. Implémenter l'application complète selon l'architecture recommandée
