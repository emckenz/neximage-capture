# NexImage Probe

Prototype Android pour la Celestron NexImage 10 branchée en USB-C sur un
Samsung Galaxy Z Fold 8. Il énumère l’UVC, négocie un format brut (Y800,
GRBG, ou tout GUID inconnu), assemble les trames dans un tampon natif et
affiche un aperçu dématricé sur le GPU.

L’architecture est argumentée dans [docs/TECHNOLOGY-DECISION.md](docs/TECHNOLOGY-DECISION.md).
Expo n’est pas utilisé : le flux passe par usbfs (`USBDEVFS_SUBMITURB`),
inaccessible depuis Expo Go et coûteux à traverser depuis un pont React Native.

Les mesures affichées (fps, débit, trames perdues, bit ERR, vitesse USB,
latence d’aperçu, CPU, mémoire) ne valent que sur le téléphone. Cet
environnement n’a pas la caméra.

## Installer sur le Samsung Galaxy Z Fold 8 (sans PC)

Les APK ne sont **pas** stockés dans le dépôt Git (Cursor ne peut pas les
prévisualiser). Ils sont publiés comme **assets téléchargeables** sur
GitHub Releases.

### 1. Ouvrir GitHub Releases sur le téléphone

Dans Chrome sur le Fold 8, ouvrir :

**https://github.com/emckenz/neximage-capture/releases/latest**

Pour une version précise (ex. v0.1.0) :

**https://github.com/emckenz/neximage-capture/releases/tag/v0.1.0**

> Si la page renvoie 404, le miroir GitHub n’est pas encore activé — voir
> [Connecter GitHub](#connecter-github-origin--github) ci-dessous.

### 2. Télécharger l’APK

Sur la page **Releases**, section **Assets** :

- Télécharger **`neximage-probe-v0.1.0-debug.apk`**
- **Ne pas** ouvrir le fichier `.sha256` (c’est seulement la checksum)

### 3. Autoriser l’installation

Android peut demander :

- **Chrome** ou **GitHub** → *Autoriser l’installation d’applications*
- Accepter pour la source utilisée

### 4. Installer

- Ouvrir le fichier téléchargé (notification ou app **Téléchargements**)
- Appuyer sur **Installer**
- Lancer **NexImage Probe**, brancher la NexImage 10 en USB-C OTG, accorder
  la permission USB

### 5. Utiliser

Choisir le format **GRBG** ou **Y800**, démarrer le flux, lire les métriques
à l’écran.

---

## Connecter GitHub (Origin → GitHub)

Le dépôt Cursor/Origin doit être lié à GitHub pour que les Releases et
Actions fonctionnent :

1. Ouvrir https://cursor.com/codebase/eric-mckenzie/neximage-capture
2. Paramètres du dépôt → **Connect GitHub** / **Publish to GitHub**
3. Créer ou lier `emckenz/neximage-capture` sur GitHub
4. Pousser le tag de release (fait automatiquement par CI, ou manuellement) :

```bash
git push origin v0.1.0
git push github v0.1.0   # si remote github configuré
```

Le workflow [`.github/workflows/android-release.yml`](.github/workflows/android-release.yml)
compile l’APK, le valide (`aapt` + `unzip -t`), calcule le SHA-256, et
l’attache à la Release.

---

## Lancer les tests du protocole (sans téléphone)

```bash
make -C native/uvc test
```

Ils vérifient le GUID `47524247-0000-1000-8000-00aa00389b71` (Bayer GRBG
8 bits), le parse des descripteurs, l’assemblage UVC et le score de mosaïque.

## Build local (optionnel)

Prérequis : Android SDK 35, NDK 27.

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

L’APK est `arm64-v8a` seulement. Branchez la caméra, accordez la permission
USB, choisissez le format GRBG ou Y800, puis lisez la ligne de mesures.

`Sauver une trame` écrit `frame-<temps>.raw` et un JSON à côté, dans le
dossier privé de l’appli. Le fichier est le brut, sans dématriçage.
Le bouton de motif cycle GRBG, GBRG, RGGB, BGGR pour confirmer à l’œil.

Si la vitesse affichée est High (USB 2), la pleine trame à 7 fps ne tient
pas : il faudra un ROI. Si la caméra disparaît dès le flux, le VBUS du
téléphone est en dessous de 770 mA et il faut un hub alimenté.

## Cloner le dépôt

```bash
curl -fsSL https://downloads.cursor.com/origin/install.sh | sh
origin auth login
origin repo clone eric-mckenzie/neximage-capture
```

Si `origin` n’est pas trouvé :

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Documentation Origin CLI : https://cursor.com/docs/origin/cli
