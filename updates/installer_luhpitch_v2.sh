#!/bin/bash
# ─────────────────────────────────────────────────────────────
#  luhpitch v2.0.0 — installeur @clideurbeats  (VST3 + AU · macOS)
#
#  bash installer_luhpitch_v2.sh          → compile + installe sur ce Mac
#  bash installer_luhpitch_v2.sh --dmg    → idem + .dmg rangé dans « plugins et apps @clideurbeats/luhpitch »
#  Mises à jour suivantes : directement dans le plugin (roue en haut à droite).
# ─────────────────────────────────────────────────────────────
set -e
VERSION="2.0.0"
JUCE_TAG="9.0.4"
STRETCH_REF="a670068d9aeb64913331d5cc29337b19a457a7df"
LINEAR_TAG="0.6.4"
WORK="$HOME/.luhpitch_build"
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

DMG=0; PLUGIN=0
for a in "$@"; do
  case "$a" in
    --dmg) DMG=1 ;;
    --plugin) PLUGIN=1 ;;   # lancé par luhpitch lui-même (mise à jour sans Terminal)
  esac
done

if [ "$PLUGIN" = "1" ]; then B=''; G=''; R=''; D=''; N=''; A=''
else B='\033[1m'; G='\033[32m'; R='\033[31m'; D='\033[2m'; N='\033[0m'; A='\033[38;5;117m'; fi
STEP=0; STEPS=6
step() { STEP=$((STEP+1)); echo -e "\n${A}▸${N} ${B}$1${N}"; [ "$PLUGIN" = "1" ] && echo "LUH_PROGRESS $STEP $STEPS $1"; return 0; }
ok()   { echo -e "  ${G}✓${N} $1"; }
ERRSENT=0
die()  { echo -e "  ${R}✗ $1${N}"; ERRSENT=1; [ "$PLUGIN" = "1" ] && echo "LUH_ERROR $1"; exit 1; }
trap 'code=$?; if [ $code -ne 0 ] && [ "$PLUGIN" = "1" ] && [ "$ERRSENT" = "0" ]; then echo "LUH_ERROR étape $STEP/$STEPS interrompue (code $code)"; fi' EXIT

[ "$PLUGIN" = "0" ] && echo -e "${A}${B}\n   luhpitch  v${VERSION}${N}  ${D}@clideurbeats${N}"
[ "$(uname)" = "Darwin" ] || die "Ce script est fait pour macOS."
mkdir -p "$WORK"

step "Outils Apple"
if ! xcode-select -p >/dev/null 2>&1; then
  [ "$PLUGIN" = "1" ] && die "Outils Apple absents : lance une fois l'installeur dans le Terminal."
  xcode-select --install || true
  die "Installe les « Command Line Tools » (fenêtre ouverte), puis relance ce script."
fi
# CMake récent obligatoire (les versions < 3.31 plantent avec le dernier Xcode)
CMAKE_V="4.1.2"
CMAKE="$(command -v cmake || true)"
if [ -n "$CMAKE" ]; then
  SYS_V="$("$CMAKE" --version | head -1 | awk '{print $3}')"
  if [ "$(printf '%s\n' "3.31.0" "$SYS_V" | sort -V | head -1)" != "3.31.0" ]; then CMAKE=""; fi
fi
PORTABLE="$WORK/cmake-$CMAKE_V/CMake.app/Contents/bin/cmake"
if [ -z "$CMAKE" ] && [ -x "$PORTABLE" ]; then CMAKE="$PORTABLE"; fi
if [ -z "$CMAKE" ]; then
  echo -e "  ${D}téléchargement de CMake $CMAKE_V…${N}"
  curl -fsSL -o "$WORK/cmake.tgz" "https://github.com/Kitware/CMake/releases/download/v$CMAKE_V/cmake-$CMAKE_V-macos-universal.tar.gz" || die "Téléchargement de CMake impossible (Internet ?)"
  rm -rf "$WORK/cmake-$CMAKE_V" && mkdir -p "$WORK/cmake-$CMAKE_V"
  tar xzf "$WORK/cmake.tgz" -C "$WORK/cmake-$CMAKE_V" --strip-components 1
  rm -f "$WORK/cmake.tgz"; rm -rf "$WORK/cmake"
  CMAKE="$PORTABLE"
fi
[ -x "$CMAKE" ] || die "CMake introuvable."
ok "$("$CMAKE" --version | head -1)"

step "Sources luhpitch v$VERSION"
rm -rf "$WORK/src" && mkdir -p "$WORK/src/deps"
sed -n '/^__SOURCES__$/,$p' "$0" | tail -n +2 | base64 --decode | tar xz -C "$WORK/src" || die "Fichier d'installation abîmé : retélécharge-le."
ok "extraites"

step "JUCE + moteur audio"
fetch() { # dossier url ref
  local dir="$WORK/deps/$1"
  if [ ! -d "$dir/.git" ]; then rm -rf "$dir"; git clone --quiet "$2" "$dir" || die "Téléchargement de $1 impossible (Internet ?)"; fi
  git -C "$dir" checkout --quiet "$3" 2>/dev/null || { git -C "$dir" fetch --quiet --tags origin && git -C "$dir" checkout --quiet "$3"; } || die "Version de $1 introuvable"
  ln -sfn "$dir" "$WORK/src/deps/$1"
}
mkdir -p "$WORK/deps"
fetch JUCE https://github.com/juce-framework/JUCE.git "$JUCE_TAG"
fetch signalsmith-stretch https://github.com/Signalsmith-Audio/signalsmith-stretch.git "$STRETCH_REF"
fetch signalsmith-linear https://github.com/Signalsmith-Audio/linear.git "$LINEAR_TAG"
ok "JUCE $JUCE_TAG"

step "Compilation"
if [ "$PLUGIN" = "1" ]; then ARCH="$(uname -m)"; else ARCH="arm64;x86_64"; fi
SDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)"
configure() {
  rm -rf "$WORK/build"
  "$CMAKE" -S "$WORK/src" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$1" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    ${SDK:+-DCMAKE_OSX_SYSROOT="$SDK"} > "$WORK/configure.log" 2>&1
}
echo -e "  ${D}($ARCH — quelques minutes la première fois)${N}"
if ! configure "$ARCH"; then
  if [ "$ARCH" != "$(uname -m)" ]; then
    ARCH="$(uname -m)"
    echo -e "  ${D}nouvel essai pour ce Mac uniquement ($ARCH)…${N}"
    configure "$ARCH" || { tail -20 "$WORK/configure.log"; die "configuration échouée (voir $WORK/configure.log)"; }
  else
    tail -20 "$WORK/configure.log"; die "configuration échouée (voir $WORK/configure.log)"
  fi
fi
"$CMAKE" --build "$WORK/build" --config Release -j "$(sysctl -n hw.ncpu)" > "$WORK/build.log" 2>&1 \
  || { grep -E "error" "$WORK/build.log" | head -20; die "compilation échouée (voir $WORK/build.log)"; }
ART="$WORK/build/luhpitch_artefacts/Release"
[ -d "$ART/VST3/luhpitch.vst3" ] && [ -d "$ART/AU/luhpitch.component" ] || die "plugin introuvable après compilation"
ok "compilé"

step "Installation"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"
rm -rf "$VST3_DIR/luhpitch.vst3" "$AU_DIR/luhpitch.component"
cp -R "$ART/VST3/luhpitch.vst3" "$VST3_DIR/"
cp -R "$ART/AU/luhpitch.component" "$AU_DIR/"
for b in "$VST3_DIR/luhpitch.vst3" "$AU_DIR/luhpitch.component"; do
  xattr -cr "$b" 2>/dev/null || true
  codesign --force --deep -s - "$b" >/dev/null 2>&1 || true
done
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
ok "VST3 → $VST3_DIR"
ok "AU   → $AU_DIR"

step "Rangement"
if [ "$DMG" = "1" ] || [ "$PLUGIN" = "1" ]; then
  STAGE="$WORK/dmg"; rm -rf "$STAGE"; mkdir -p "$STAGE"
  cp -R "$VST3_DIR/luhpitch.vst3" "$AU_DIR/luhpitch.component" "$STAGE/"
  cat > "$STAGE/Installer luhpitch.command" <<'INST'
#!/bin/bash
cd "$(dirname "$0")"
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/luhpitch.vst3 ~/Library/Audio/Plug-Ins/Components/luhpitch.component
cp -R luhpitch.vst3 ~/Library/Audio/Plug-Ins/VST3/ && cp -R luhpitch.component ~/Library/Audio/Plug-Ins/Components/
xattr -cr ~/Library/Audio/Plug-Ins/VST3/luhpitch.vst3 ~/Library/Audio/Plug-Ins/Components/luhpitch.component 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo "luhpitch installé. Relance FL Studio et scanne les plugins."
INST
  chmod +x "$STAGE/Installer luhpitch.command"
  DMGFILE="$WORK/luhpitch_v$VERSION.dmg"; rm -f "$DMGFILE"
  if hdiutil create -volname "luhpitch v$VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMGFILE" >/dev/null 2>&1; then
    DEST="$HOME/Desktop/plugins et apps @clideurbeats/luhpitch"
    mkdir -p "$DEST/ancienne version"
    for old in "$DEST"/*.dmg; do if [ -e "$old" ] && [ "$(basename "$old")" != "luhpitch_v$VERSION.dmg" ]; then mv -f "$old" "$DEST/ancienne version/"; fi; done
    cp -f "$DMGFILE" "$DEST/"
    ok ".dmg → $DEST"
  else
    ok "(.dmg non créé, le plugin est bien installé)"
  fi
else
  ok "rien à ranger (ajoute --dmg pour créer le .dmg)"
fi

[ "$PLUGIN" = "1" ] && echo "LUH_DONE"
echo -e "\n${G}${B}  luhpitch v$VERSION est installé.${N}"
echo -e "  Relance FL Studio (ou Options → Manage plugins → Find more plugins).\n"
exit 0
__SOURCES__
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
/wNp+pKWAJABAA==
