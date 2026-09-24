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

## Lancer les tests du protocole

```bash
make -C native/uvc test
```

Ils vérifient le GUID `47524247-0000-1000-8000-00aa00389b71` (Bayer GRBG
8 bits), le parse des descripteurs, l’assemblage UVC et le score de mosaïque.

## Installer sur le Fold

Prérequis : Android SDK 35, NDK 27, un câble USB 3, et assez de VBUS
(la DFK 33UJ003, jumelle probable de la NexImage 10, tire environ 770 mA).

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
