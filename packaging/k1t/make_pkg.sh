#!/bin/bash
# Fabrique « Installer K1T.pkg » + K1T_vX.dmg à partir des plugins compilés.
# Usage : make_pkg.sh <version> <dossier artefacts Release> <dossier de sortie>
set -euo pipefail
VERSION="$1"; ART="$2"; OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; ROOT="$TMP/root"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$OUT"

cp -R "$ART/VST3/K1T.vst3"      "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/K1T.component"   "$ROOT/Library/Audio/Plug-Ins/Components/"
for b in "$ROOT/Library/Audio/Plug-Ins/VST3/K1T.vst3" "$ROOT/Library/Audio/Plug-Ins/Components/K1T.component"; do
  xattr -cr "$b" || true
  codesign --force --deep --sign - "$b"          # signature ad hoc (obligatoire sur Apple Silicon)
  lipo -info "$b/Contents/MacOS/K1T"
done

# les bundles s'installent toujours au bon endroit (pas de « relocation » vers une ancienne copie)
pkgbuild --analyze --root "$ROOT" "$TMP/comp.plist"
n=$(/usr/libexec/PlistBuddy -c "Print" "$TMP/comp.plist" | grep -c "RootRelativeBundlePath" || true)
for ((i=0; i<n; i++)); do /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$TMP/comp.plist"; done

chmod +x "$HERE/scripts/postinstall"
pkgbuild --root "$ROOT" --component-plist "$TMP/comp.plist" --identifier com.clideurbeats.k1t \
  --version "$VERSION" --install-location / --scripts "$HERE/scripts" "$TMP/k1t-core.pkg"

sed "s/__VERSION__/$VERSION/g" "$HERE/distribution.xml" > "$TMP/distribution.xml"
mkdir -p "$TMP/resources"; cp "$HERE/resources/"* "$TMP/resources/"
sed -i '' "s/__VERSION__/$VERSION/g" "$TMP/resources/"*.html
productbuild --distribution "$TMP/distribution.xml" --resources "$TMP/resources" --package-path "$TMP" \
  "$OUT/K1T_v$VERSION.pkg"

# signature Developer ID (facultative : seulement si un certificat est présent)
if [ -n "${K1T_INSTALLER_IDENTITY:-}" ]; then
  productsign --sign "$K1T_INSTALLER_IDENTITY" "$OUT/K1T_v$VERSION.pkg" "$TMP/signed.pkg" && mv "$TMP/signed.pkg" "$OUT/K1T_v$VERSION.pkg"
fi

# le .dmg à envoyer : l'installeur + un mode d'emploi
STAGE="$TMP/dmg"; mkdir -p "$STAGE"
cp "$OUT/K1T_v$VERSION.pkg" "$STAGE/Installer K1T.pkg"
sed "s/__VERSION__/$VERSION/g" "$HERE/Lis-moi.txt" > "$STAGE/Lis-moi.txt"
rm -f "$OUT/K1T_v$VERSION.dmg"
hdiutil create -volname "K1T $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$OUT/K1T_v$VERSION.dmg"
ls -la "$OUT"
