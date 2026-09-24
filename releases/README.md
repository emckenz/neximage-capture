# Releases — installation directe sur Android (sans PC)

## NexImage Probe v0.1.0-debug

**Fichier :** [`neximage-probe-v0.1.0-debug.apk`](neximage-probe-v0.1.0-debug.apk) (~10 Mo)

Prototype de validation USB/UVC pour Celestron NexImage 10 sur Samsung Galaxy Z Fold 8.

### Installation sur le téléphone (sans ordinateur)

1. Ouvrir ce dépôt sur le Fold 8 : https://cursor.com/codebase/eric-mckenzie/neximage-capture
2. Naviguer vers `releases/neximage-probe-v0.1.0-debug.apk`
3. Télécharger le fichier APK
4. Android demandera d'autoriser l'installation depuis le navigateur ou « Fichiers » → **Autoriser**
5. Ouvrir le fichier téléchargé et appuyer sur **Installer**

> Si le téléchargement direct depuis le navigateur ne fonctionne pas, ouvrez la même page sur un ordinateur, envoyez-vous l'APK (email, Drive, etc.), puis ouvrez-le sur le téléphone.

### Utilisation

1. Brancher la NexImage 10 via **USB-C OTG**
2. Lancer **NexImage Probe**
3. Accorder la permission USB
4. Tester Y800 puis GRBG, observer les métriques

### Notes

- APK debug signé automatiquement — pas besoin de Play Store
- `minSdk 26` (Android 8+) — compatible Fold 8
- Aucune connexion PC/adb requise après installation
