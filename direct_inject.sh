#!/bin/bash

GAME_DIR="/home/yannek/Dokumente/GTAVEnhanced"
TARGET_EXE="GTA5_Enhanced.exe"
MOD_DLL="YimMenuV2.dll"

cd "$GAME_DIR" || exit 1

echo "[*] Suche nach dem echten Wine-Prozess von GTA V..."

# 1. Exakte Wine-PID ermitteln (prüft, ob der Prozess wirklich WINESERVERSOCKET in environ hat,
#    damit niemals aus Versehen ein Bash- oder Python-Wrapper ausgewählt wird!)
GTA_PID=""
for pid in $(pgrep -i -f "GTA5_Enhanced"); do
    if [ -r "/proc/$pid/environ" ] && grep -qa "WINESERVERSOCKET=" "/proc/$pid/environ" 2>/dev/null; then
        GTA_PID="$pid"
        break
    fi
done

# Falls WINESERVERSOCKET nicht gesetzt ist, nimm den Prozess mit dem 15-Zeichen-Kernelnamen
if [ -z "$GTA_PID" ]; then
    GTA_PID=$(pgrep -i "GTA5_Enhanced.e" | head -n 1)
fi

if [ -z "$GTA_PID" ]; then
    echo "[!] FEHLER: GTA V läuft nicht! Bitte erst in den Story-Modus laden."
    notify-send "GTA V Injector" "Fehler: Spielprozess nicht gefunden!"
    exit 1
fi

echo "[+] Echter Spielprozess verifiziert! Linux-PID: $GTA_PID"

# 2. Exakten Proton-Loader ermitteln
WINE_BIN=$(readlink -f "/proc/$GTA_PID/exe" | sed 's/-preloader$//')
WINE_DIR=$(dirname "$WINE_BIN")

# 3. Proton-Umgebung sauber klonen (OHNE ungültige Dateideskriptoren!)
while IFS= read -r -d '' var; do
    case "$var" in
        # WICHTIG: Diese 3 internen Deskriptoren gehören exklusiv zu PID $GTA_PID
        # und verursachen sonst "Bad server socket 562 : Ungültiger Dateideskriptor"!
        WINESERVERSOCKET=*|WINELOADERNOEXEC=*|WINEPRELOADRESERVE=*)
            continue
            ;;
        WINE*|PROTON*|STEAM*|LD_LIBRARY_PATH=*|PATH=*|TMPDIR=*|XDG_RUNTIME_DIR=*|USER=*)
            export "$var"
            ;;
    esac
done < "/proc/$GTA_PID/environ"

unset WINESERVERSOCKET WINELOADERNOEXEC WINEPRELOADRESERVE
export WINESERVER="$WINE_DIR/wineserver"
export WINEDEBUG="-all" # Unterdrückt unnötige fixme-Warnungen

# 4. Socket-Brücke: Falls Heroic/Proton in einem privaten /tmp-Namespace läuft,
#    spiegeln wir das aktive .wine-UID Socket-Verzeichnis aus dem Prozess-Root auf den Host
UID_NUM=$(id -u)
GAME_WINE_TMP="/proc/$GTA_PID/root/tmp/.wine-$UID_NUM"
HOST_WINE_TMP="/tmp/.wine-$UID_NUM"

if [ -d "$GAME_WINE_TMP" ]; then
    mkdir -p "$HOST_WINE_TMP"
    cp -rsf "$GAME_WINE_TMP/"* "$HOST_WINE_TMP/" 2>/dev/null || true
fi

# ==============================================================================
# MAIN METHODE: Direkte Ausführung mit bereinigter Proton-Umgebung & Socket-Sync
# ==============================================================================
echo "[*] Starte MAIN METHODE (Direkte Proton-Socket-Verbindung)..."
if "$WINE_BIN" ./injector.exe "$TARGET_EXE" "$MOD_DLL"; then
    echo "[+] ERFOLG über Main-Methode!"
    notify-send "GTA V Injector" "YimMenuV2 erfolgreich injiziert (Main-Methode)!"
    exit 0
fi

# ==============================================================================
# FALLBACK 1: Ausführung direkt im Kernel-Mount-Namespace des Spiels (nsenter)
# (Greift, falls Heroic das Spiel in einer Bubblewrap/Pressure-Vessel-Sandbox isoliert)
# ==============================================================================
echo "[!] Main-Methode konnte Prozess nicht sehen. Starte FALLBACK 1 (nsenter Container-Einstieg)..."
if command -v nsenter >/dev/null 2>&1; then
    if nsenter --target "$GTA_PID" --user --preserve-credentials --mount --wd="$GAME_DIR" \
        "$WINE_BIN" ./injector.exe "$TARGET_EXE" "$MOD_DLL"; then
        echo "[+] ERFOLG über Fallback 1 (nsenter)!"
        notify-send "GTA V Injector" "YimMenuV2 erfolgreich injiziert (Fallback 1)!"
        exit 0
    fi
fi

# ==============================================================================
# FALLBACK 2: Direkter Aufruf über wine64 im selben Verzeichnis wie die Spieldatei
# ==============================================================================
echo "[!] Starte FALLBACK 2 (Alternativer Loader-Aufruf)..."
ALT_WINE="${WINE_DIR}/wine64"
if [ -x "$ALT_WINE" ]; then
    if "$ALT_WINE" ./injector.exe "GTA5" "$MOD_DLL"; then
        echo "[+] ERFOLG über Fallback 2!"
        notify-send "GTA V Injector" "YimMenuV2 erfolgreich injiziert (Fallback 2)!"
        exit 0
    fi
fi

echo "[!] Alle Methoden fehlgeschlagen. Bitte Terminal-Ausgabe oben prüfen!"
notify-send "GTA V Injector" "Injektion fehlgeschlagen!"
exit 1
