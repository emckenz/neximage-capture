# Décision technologique — Application d'astrophotographie NexImage 10

**Date :** 24 septembre 2026  
**Cible matérielle :** Samsung Galaxy Z Fold 8 + Celestron NexImage 10 (USB-C OTG)  
**Objectif :** Choisir l'architecture maximisant la qualité des données, le débit USB et le contrôle bas niveau — **sans contrainte Expo**.

---

## 1. Contexte matériel et protocole

### Celestron NexImage 10

| Paramètre | Valeur |
|-----------|--------|
| USB VID:PID | `199e:8619` (rebadge Imaging Source / TIS) |
| USB | SuperSpeed USB 3.0 (5 Gbps), alimentation bus |
| Capteur | ON Semi MT9J003, 3856×2764 px, 1.67 µm |
| A/D | 12 bits (livré en 8 bits via UVC) |
| Protocole | **UVC 1.x** (pas de driver propriétaire Windows requis sur Linux) |
| FPS max | ~94 fps (ROI), ~7 fps plein cadre |
| Contrôles UVC | Exposition absolue, gain, ROI matériel, AE mode |

### Formats pixel confirmés (Linux / INDIGO / V4L2)

| FOURCC UVC | Type | Résolution typique | Notes |
|------------|------|-------------------|-------|
| `GRBG` | 8-bit Bayer GRGR/BGBG | 3872×2764 | Format couleur natif |
| `Y800` / `GREY` | 8-bit mono | variable | Monochrome, pas de debayer |
| `Y16 ` | 16-bit mono | rare | Si exposé par le firmware |

### GUID `47524247-0000-1000-8000-00aa00389b71`

Analyse détaillée (voir §8) : ce GUID correspond au format **GRBG 8-bit Bayer**. Le suffixe `1000` (au lieu du `0010` standard DirectShow FOURCCMap) indique un sous-type media **propriétaire Celestron/TIS**, mais le payload pixel est identique au FOURCC UVC `GRBG`.

### Samsung Galaxy Z Fold 8

| Paramètre | Valeur |
|-----------|--------|
| SoC | Snapdragon 8 Elite Gen 5 for Galaxy |
| GPU | Adreno 840 @ ~1.3 GHz |
| API graphiques | OpenGL ES 3.2, Vulkan 1.3, OpenCL 3.0 |
| USB | USB 3.1 Gen 2 Type-C, OTG host |
| RAM | 12–16 Go LPDDR5X |
| Android | 17 (API 36+) |

Le Fold 8 dispose d'un GPU largement suffisant pour debayer 3872×2764 en temps réel (>30 fps preview) si le pipeline évite les copies mémoire inutiles.

---

## 2. Priorités utilisateur (ordre décroissant)

1. Qualité maximale des données NexImage 10  
2. Zéro frame perdu (objectif)  
3. Accès USB/UVC complet + RAW/Bayer  
4. FPS maximal  
5. Latence preview minimale  
6. Contrôle Exposure/Gain/ROI/FPS  
7. Capture RAW/lossless  
8. Traitement GPU efficace  
9. Stabilité Fold 8  
10. UI Fold + astronomie nocturne  
11. Facilité de développement *(dernier critère)*

---

## 3. Solutions open source évaluées

### 3.1 Accès USB/UVC Android

| Projet | Licence | Dernière activité | Rôle |
|--------|---------|-------------------|------|
| [libuvc/libuvc](https://github.com/libuvc/libuvc) | BSD-3 | 2025 (issues actives) | Stack UVC cross-platform, parsing descriptors, isoc/bulk |
| [libusb/libusb](https://github.com/libusb/libusb) | LGPL-2.1 | 2025 (v1.0.27+) | Accès USB userspace via fd Android |
| [saki4510t/UVCCamera](https://github.com/saki4510t/UVCCamera) | Apache-2.0 | Maintenance lente | Référence Android : `uvc_get_device_with_fd`, isoc Android |
| [ernestp/AndroidUSBCamera](https://github.com/ernestp/AndroidUSBCamera) | Apache-2.0 | **2026** (fork actif) | Fork maintenu, Android 16, 16K pages |
| [jiangdongguo/AndroidUSBCamera](https://github.com/jiangdongguo/AndroidUSBCamera) | Apache-2.0 | Sep 2024 (upstream mort) | Base historique, 2700+ stars |
| [anova-culinary/AndroidUSBCamera](https://github.com/anova-culinary/AndroidUSBCamera) | Apache-2.0 | Jun 2025 | Fork intermédiaire |

**Limitations Android documentées (libuvc #299, libusb #1504, #1726) :**
- `packets_per_transfer` par défaut (32) provoque `ENOMEM` sur MediaTek/Samsung → réduire à ≤12  
- Transferts isochrones instables sur certains kernels Android  
- Pas de `/dev/video*` sur Samsung sans module kernel → **libusb+libuvc via USB Host API obligatoire**  
- Camera2 NDK ne voit pas le Bayer RAW des UVC externes

### 3.2 Traitement Bayer / astrophotographie

| Projet | Licence | Utilité |
|--------|---------|---------|
| [indigo-astronomy/indigo](https://github.com/indigo-astronomy/indigo) `ccd_uvc` | INDIGO | **Référence directe NexImage 10** : mapping FOURCC, contrôles UVC, capture FITS |
| [indilib/indi](https://github.com/indilib/indi) | LGPL-2.1 | Driver V4L2 GRBG NexImage 5 |
| [openastroproject/openastro](https://github.com/openastroproject/openastro) | GPL-3.0 | oaCapture — support NexImage 10 via libuvc |
| [guvcview/guvcview](https://github.com/guvcview/guvcview) | GPL-2.0 | Debayer GRBG en live (colorspaces.c) |
| [FFmpeg](https://github.com/FFmpeg/FFmpeg) `vulkan/debayer.comp.glsl` | LGPL-2.1 | Shaders debayer Vulkan compute (RGGB16, extensible GRBG8) |
| OpenCV `Imgproc.demosaicing` | Apache-2.0 | Debayer CPU, portable mais lent plein cadre |

### 3.3 GPU debayer

| Approche | Avantages | Inconvénients |
|----------|-----------|---------------|
| **OpenGL ES 3.2 fragment shader** | Mature, intégration Surface/SurfaceView simple, Adreno optimisé | Overhead driver vs Vulkan |
| **Vulkan 1.3 compute** | Moins de overhead CPU, compute parallèle | Complexité initiale élevée, gain GPU marginal pour 1 pipeline |
| **OpenCL 3.0** | Compute généraliste | Moins intégré au pipeline rendu Android |
| **CPU (libindigo/OpenCV)** | Simple | ~1–3 fps à 3872×2764, inacceptable pour preview |

**Conclusion GPU :** OpenGL ES 3.2 pour le preview/debayer v1 ; migration Vulkan compute si profiling montre un goulot CPU/driver.

---

## 4. Comparaison des architectures

Légende : ✅ excellent · ⚠️ partiel · ❌ inadéquat · — non applicable

| Critère | A) Expo + RN + Dev Build | B) RN sans Expo | C) Kotlin + Compose natif | D) Kotlin + NDK C/C++ | E) Natif + OpenGL ES | F) Natif + Vulkan | G) libusb/libuvc NDK | H) USB Host API seul |
|---------|--------------------------|-----------------|---------------------------|----------------------|---------------------|-------------------|---------------------|---------------------|
| **Accès USB** | ⚠️ module natif requis | ⚠️ idem | ✅ USB Host API | ✅ fd → libusb | ✅ | ✅ | ✅ | ⚠️ pas de parsing UVC |
| **Compatibilité NexImage 10** | ⚠️ si module OK | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **RAW/Bayer GRBG** | ⚠️ via JNI | ⚠️ | ✅ natif | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Contrôle UVC complet** | ⚠️ | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Formats propriétaires** | ⚠️ | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **FPS max** | ❌ copies JNI | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **Latence preview** | ❌ bridge JS | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **Copies mémoire** | ❌ 3–5× | ❌ 3–5× | ✅ 1–2× | ✅ 1× | ✅ 1× | ✅ 1× | ✅ 1× | — |
| **CPU preview** | ❌ élevé | ❌ | ✅ faible | ✅ minimal | ✅ minimal | ✅ minimal | ✅ minimal | — |
| **GPU debayer** | ⚠️ indirect | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **Capture lossless** | ⚠️ | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| **Stabilité Fold 8** | ⚠️ Hermes+JNI | ⚠️ | ✅ | ✅ | ✅ | ✅ | ⚠️ tuning isoc | ❌ |
| **Android moderne (API 36)** | ⚠️ Expo lag | ⚠️ | ✅ | ✅ | ✅ | ✅ | ⚠️ patches | ✅ |
| **Support Fold (dual screen)** | ⚠️ | ⚠️ | ✅ WindowManager | ✅ | ✅ | ✅ | ✅ | — |
| **Complexité** | Moyenne | Moyenne | Moyenne | Élevée | Élevée | Très élevée | Élevée | Faible |
| **Maintenance** | Expo SDK | RN modules | Android standard | libuvc upstream | Standard | Vulkan boilerplate | libuvc+libusb | — |
| **Dépendances** | Expo, RN, Hermes | RN, Hermes | Compose, AGP | +libuvc, libusb | +GLSL | +SPIR-V | idem G | Aucune lib UVC |
| **Licences** | MIT + deps | MIT + deps | Apache-2.0 | BSD+LGPL | Apache-2.0 | Apache-2.0 | BSD+LGPL | Apache-2.0 |
| **Déploiement Fold 8** | Dev build + sideload | idem | APK direct | APK direct | APK direct | APK direct | APK direct | Insuffisant |

### Détail par architecture

#### A) Expo + React Native + Development Build + modules natifs

Expo Go est **exclu** : pas d'accès USB Host, pas de modules natifs arbitraires, pas de NDK direct.

Avec Development Build, un module natif libuvc est théoriquement possible, mais :
- Chaque frame traverse : USB → native → JNI → JSI/Hermes → React → Skia/View
- Impossible de garantir zéro copy sans contourner complètement le bridge JS
- Expo SDK ajoute une couche de mise à jour qui retarde le support Android 16/16K pages
- Aucun projet astrophoto mature n'utilise cette stack

**Verdict : ❌ Écarté** — la contrainte « facilité de dev » ne compense pas la perte de performance.

#### B) React Native sans Expo

Identique à A pour le pipeline frame. Des modules comme `react-native-vision-camera` ciblent les caméras **internes** Camera2, pas les UVC USB externes en Bayer RAW.

**Verdict : ❌ Écarté**

#### C) Android natif Kotlin + Jetpack Compose

UI et logique métier en Kotlin. Accès USB via `UsbManager` + `UsbDeviceConnection.fileDescriptor`.

Sans NDK, impossible de parser les descriptors UVC ni de faire des isoch/bulk transfers performants. Compose seul ne suffit pas.

**Verdict : ⚠️ Nécessite couche native (→ D)**

#### D) Kotlin/Compose + C/C++ NDK (USB + vidéo) ⭐

Architecture optimale :
```
UsbManager (Kotlin) → fd → libusb → libuvc → ring buffer (native)
                                              ↓
                                    OpenGL ES debayer + histogram
                                              ↓
                                    SurfaceView / Compose AndroidView
```

- **1 copy** : USB DMA → buffer natif mlocké → texture GPU
- Thread dédié capture (SCHED_FIFO si possible), thread render séparé
- Kotlin gère permissions USB, UI Fold, contrôles expos/gain
- Réutilise la logique INDIGO `ccd_uvc` pour le mapping formats/contrôles

**Verdict : ✅ Recommandé**

#### E) Android natif + OpenGL ES

Sous-ensemble de D. OpenGL ES 3.2 fragment shader debayer (GRBG → RGBA8) :
- ~280 Mpix/s sur GPU mobile (McGuire 2009, confirmé Adreno)
- 3872×2764 = 10.7 Mpix → **<40 ms** par frame debayer HQ
- Intégration native `Surface`/`AHardwareBuffer` pour zero-copy texture upload

**Verdict : ✅ Composant preview/capture de D**

#### F) Android natif + Vulkan

Avantages réels : -30% CPU driver vs GL ES (Google I/O case study), latence légèrement inférieure.

Inconvénients pour ce projet :
- Debayer = 1 compute dispatch, GPU-bound identique
- Boilerplate Vulkan (~2000 lignes) pour un gain preview marginal
- Adreno 840 supporte les deux ; OpenGL ES suffit pour v1

**Verdict : ⚠️ Phase 2** — migrer le debayer/histogramme si profiling le justifie

#### G) libusb/libuvc via NDK

**Obligatoire** sur Samsung sans `/dev/video*`. C'est le seul chemin userspace non-root validé par INDIGO et AndroidUSBCamera.

Patches requis pour Android :
```c
// libuvc stream.c — Android safe mode
#define PACKETS_PER_TRANSFER 8  // ou probe dynamique, max 12 sur Samsung
```

Utiliser `uvc_get_device_with_fd()` (patch saki4510t) pour Android 7+.

**Verdict : ✅ Couche USB de D**

#### H) Android USB Host API sans libuvc

L'API `UsbDeviceConnection.bulkTransfer()` / `UsbRequest` permet un accès raw, mais :
- Pas de parsing automatique des descriptors UVC
- Re-implémentation manuelle de `VS_PROBE/COMMIT`, format négociation, isoc framing
- INDIGO, libuvc, guvcview ont tous choisi libuvc

**Verdict : ❌ Réinventer libuvc sans bénéfice**

#### I) Autres options considérées

| Option | Verdict |
|--------|---------|
| **Camera2 / NDK Camera external** | ❌ Expose YUV/JPEG, pas Bayer RAW GRBG pour UVC |
| **V4L2 via /dev/video*** | ❌ Absent sur Samsung stock |
| **Flutter + texture registry** | ❌ Même problème JNI/copies que RN |
| **Capacitor / Ionic** | ❌ WebView, inadapté |
| **INDIGO server embarqué + UI web** | ⚠️ Possible pour prototypage rapide, latence réseau locale, pas optimal mobile |
| **oaCapture port Android** | ⚠️ GPL-3.0, pas de port Android existant |

---

## 5. Pipeline recommandé (architecture cible)

```
┌─────────────────────────────────────────────────────────────────┐
│                     Samsung Galaxy Z Fold 8                     │
├─────────────────────────────────────────────────────────────────┤
│  UI Layer (Kotlin + Jetpack Compose)                            │
│  • Fold-aware layouts (WindowSizeClass, dual-pane)              │
│  • Thème astronomie nocturne (rouge, OLED-friendly)             │
│  • Contrôles : exposure, gain, ROI, format, record              │
├─────────────────────────────────────────────────────────────────┤
│  Service Layer (Kotlin)                                         │
│  • UsbManager / permission / hotplug BroadcastReceiver          │
│  • CameraSession lifecycle                                      │
│  • File I/O FITS/RAW séquentiel (Storage Access Framework)      │
├─────────────────────────────────────────────────────────────────┤
│  Native Layer (C++17 NDK)                                       │
│  • libusb + libuvc (patches Android)                            │
│  • Ring buffer triple-buffered (lock-free SPSC)                 │
│  • Frame metadata : timestamp, frame#, format, dropped count    │
│  • UVC controls : AE, exposure abs, gain, ROI                   │
├─────────────────────────────────────────────────────────────────┤
│  GPU Layer (OpenGL ES 3.2)                                      │
│  • Upload Bayer R8 → GL_R8 texture (PBO double-buffer)          │
│  • Fragment shader debayer GRBG (Malvar-He ou bilinear HQ)      │
│  • Histogramme compute (256 bins R/G/B)                         │
│  • Output → SurfaceView / TextureView                           │
├─────────────────────────────────────────────────────────────────┤
│  Capture Layer                                                  │
│  • RAW lossless : écriture directe buffer Bayer (pas de debayer)│
│  • FITS header (INDIGO-compatible) avec BAYERPAT='GRBG'         │
│  • Séquence video : container MKV ou SER                      │
└─────────────────────────────────────────────────────────────────┘
         ↑ USB-C OTG
┌─────────────────┐
│ NexImage 10     │
│ 199e:8619 UVC   │
│ GRBG/Y800       │
└─────────────────┘
```

### Copies mémoire cibles

| Étape | Copies |
|-------|--------|
| USB → ring buffer | 1 (DMA) |
| ring buffer → GL PBO | 1 (ou 0 avec AHardwareBuffer) |
| debayer GPU → Surface | 0 (on-GPU) |
| capture RAW → fichier | 1 (async write, buffer dédié) |
| **Total preview** | **1–2** |
| **RN/Expo typique** | **4–6** |

---

## 6. Métriques de validation (prototype)

Le prototype `prototype/neximage-probe/` mesure :

| Métrique | Méthode | Seuil attendu |
|----------|---------|---------------|
| FPS réel | Compteur frames / wall clock | ≥5 fps @ 3872×2764 GRBG |
| Débit USB | bytes/frame × fps | ~53 MB/s @ 7 fps plein cadre |
| Latence | timestamp UVC header → Surface flip | <100 ms preview |
| Frames perdues | `(received - displayed)` / received | <1% |
| CPU | `/proc/self/stat` + `top` | <30% single core capture |
| Mémoire | `Runtime.getRuntime()` + native heap | <200 MB steady state |

---

## 7. Risques et mitigations

| Risque | Probabilité | Mitigation |
|--------|-------------|------------|
| Isoc ENOMEM Samsung | Élevée | `packets_per_transfer=8`, probe dynamique |
| USB 3 bandwidth saturation | Moyenne | ROI matériel, format Y800 pour focus |
| Chauffe Fold en capture longue | Moyenne | Limit FPS preview, pause capture |
| GRBG non reconnu par libuvc | Faible | Fallback FOURCC custom mapping (INDIGO) |
| Android 16K page size | Faible | Fork ernestp/AndroidUSBCamera, NDK r27+ |

---

## 8. Analyse du GUID `47524247-0000-1000-8000-00aa00389b71`

### Décodage

```
GUID : 47524247 - 0000 - 1000 - 8000 - 00aa00389b71
         │                │
         │                └── suffixe NON-STANDARD (DirectShow utilise 0010)
         └── Data1 = 0x47524247
```

**Interprétation ASCII des octets Data1 (ordre d'affichage GUID) :**

| Octet hex | ASCII |
|-----------|-------|
| 0x47 | G |
| 0x52 | R |
| 0x42 | B |
| 0x47 | G |

→ **FOURCC = `GRBG`**

> Note : un FOURCCMap DirectShow standard encoderait `GRBG` comme `0x47425247` (little-endian DWORD), pas `0x47524247`. Le GUID NexImage utilise une variante propriétaire TIS/Celestron avec suffixe `1000`, mais le format pixel est confirmé **GRBG 8-bit Bayer** par V4L2, INDIGO et oaCapture.

### Format pixel

| Propriété | Valeur |
|-----------|--------|
| Nom | GRBG (8-bit Bayer GRGR/BGBG) |
| Bits/pixel | 8 |
| Pattern | Ligne paire : G R G R… / Ligne impaire : B G B G… |
| Taille frame | width × height bytes (ex. 3872×2764 = 10 702 208 bytes) |
| Debayer requis | Oui, pour preview couleur |
| Équivalent libuvc | `UVC_FRAME_FORMAT_SGRBG8` / FOURCC `"GRBG"` |
| Équivalent V4L2 | `V4L2_PIX_FMT_GRBG` |
| Équivalent INDIGO | `{ UVC_FRAME_FORMAT_SGRBG8, "GRBG", "RAW8 %dx%d" }` |

### Y800 (format de test prototype)

| Propriété | Valeur |
|-----------|--------|
| FOURCC | `Y800` (0x30303859) |
| Type | 8-bit grayscale (MONO8) |
| libuvc | `UVC_FRAME_FORMAT_GRAY8` |
| Usage | Test débit USB sans debayer, focus, drift |

---

## 9. RECOMMENDED ARCHITECTURE

### Choix : **D + E + G — Android natif Kotlin/Compose + NDK (libusb/libuvc) + OpenGL ES 3.2**

### Justification technique

1. **Qualité des données (priorité #1)** : libuvc accède directement au flux Bayer GRBG 8-bit sans recompression. La capture lossless écrit le buffer natif tel quel (FITS/SER/RAW). Aucune couche JavaScript ou bridge ne dégrade les données.

2. **Zéro frame perdu (#2)** : Un ring buffer triple natif avec thread capture SCHED_FIFO et transferts isoc tunés (`packets_per_transfer ≤ 8`) minimise les drops. React Native/Expo ne permet pas ce niveau de contrôle thread/memory.

3. **Accès USB/UVC complet (#3)** : Seul libusb+libuvc via NDK offre VS_PROBE/COMMIT, énumération formats, contrôles exposure/gain/ROI sur Samsung sans root. Camera2 et Expo ne le supportent pas.

4. **FPS (#4)** : INDIGO atteint 7 fps plein cadre sur Linux avec la même stack. Le pipeline 1-copy natif reproduit ces performances. Expo ajouterait 2–3 copies et du GC pressure.

5. **Latence preview (#5)** : OpenGL ES debayer directement depuis texture R8 → SurfaceView. Latence cible <100 ms vs >300 ms via bridge RN.

6. **Contrôles (#6)** : INDIGO `ccd_uvc` prouve le mapping complet des contrôles UVC NexImage 10 — réutilisable en C++.

7. **Capture lossless (#7)** : Écriture async du buffer Bayer sans debayer ni conversion.

8. **GPU (#8)** : Adreno 840 + GLSL debayer Malvar-He : >30 fps preview à 10.7 Mpix. Vulkan reporté en phase 2.

9. **Stabilité Fold 8 (#9)** : APK natif, pas de runtime Expo/Hermes. Fork AndroidUSBCamera compatible Android 16/16K pages.

10. **UI Fold (#10)** : Jetpack Compose + WindowSizeClass natif, meilleur support dual-pane que RN.

11. **Facilité de dev (#11)** : Plus complexe qu'Expo initialement, mais c'est le **seul** chemin viable pour les priorités 1–10.

### Stack technique finale

| Couche | Technologie |
|--------|-------------|
| Langage UI | Kotlin 2.x |
| UI Framework | Jetpack Compose + Material 3 |
| USB Permission | Android USB Host API |
| UVC Stack | libusb 1.0.27 + libuvc (patches Android) |
| Native | C++17, CMake, NDK r27 |
| Preview GPU | OpenGL ES 3.2 (GLSL debayer) |
| Capture | FITS (CFITSIO) + SER |
| Référence | INDIGO ccd_uvc, ernestp/AndroidUSBCamera |
| Build | Gradle 8.x, minSdk 26, targetSdk 36 |
| Déploiement | APK/AAB sideload (pas de Play Store requis) |

### Ce qui est explicitement rejeté

- **Expo Go** : pas d'USB natif
- **Expo Dev Build + RN** : copies mémoire, latence, pas de gain vs natif
- **Camera2 seul** : pas de Bayer RAW UVC
- **Vulkan v1** : complexité sans gain mesurable pour debayer single-pass
- **USB Host API sans libuvc** : réimplémentation inutile

### Feuille de route

1. ✅ **Prototype `neximage-probe`** — validation USB → Y800/GRBG → preview + métriques  
2. Confirmer FPS/drops sur Fold 8 réel  
3. Implémenter debayer GLSL + histogramme  
4. UI Compose Fold + thème nuit  
5. Capture FITS/SER + contrôles complets  
6. (Optionnel) Migration debayer → Vulkan compute  

---

## 10. Références

- [INDIGO ccd_uvc](https://github.com/indigo-astronomy/indigo/tree/master/indigo_drivers/ccd_uvc) — driver UVC, mapping GRBG
- [INDIGO issue #421](https://github.com/indigo-astronomy/indigo/issues/421) — NexImage 10 debayer GRBG
- [oaCapture issue #252](https://github.com/openastroproject/openastro/issues/252) — NexImage 10 V4L2 GRBG
- [libuvc Android ENOMEM #299](https://github.com/libuvc/libuvc/issues/299)
- [libusb Android isoc #1504](https://github.com/libusb/libusb/issues/1504)
- [ernestp/AndroidUSBCamera](https://github.com/ernestp/AndroidUSBCamera) — fork maintenu 2026
- [Void Computing — Raw UVC on Android](https://voidcomputing.hu/blog/android-uvc/)
- [DirectShow FOURCC GUID mapping](https://learn.microsoft.com/en-us/windows/win32/directshow/fourcc-codes)
- [Snapdragon 8 Elite Gen 5 product brief](https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Elite-Gen-5-product-brief.pdf)
