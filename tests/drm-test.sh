#!/bin/sh
# DRM login-freeze test. The log is fsync'd every 0.3s so it survives a
# hard freeze, and the whole process group is killed after 45s so the
# machine comes back without the power button.
#
# Run from a spare TTY (Ctrl+Alt+F2, login), then:
#   ~/gluemac/tests/drm-test.sh            # real config (full repro)
#   ~/gluemac/tests/drm-test.sh noeffects  # blur/animations/opacity off
#   ~/gluemac/tests/drm-test.sh noauto     # autostart stripped
#   ~/gluemac/tests/drm-test.sh min        # no autostart, no effects
# Ctrl+Alt+F1 goes back to the running session; log: ~/gluemac-drm-test-<mode>.log

MODE="${1:-full}"
LOG="$HOME/gluemac-drm-test-$MODE.log"
BIN="${GLUEMAC:-$HOME/gluemac/gluemac}"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}/gluemac/config.conf"
TIMEOUT=45

TMP=$(mktemp -d) || exit 1
mkdir -p "$TMP/cfg/gluemac"
cp "$CFG" "$TMP/cfg/gluemac/config.conf" || exit 1
strip_effects() {
	sed -i -e 's/^blur .*/blur = false/' -e 's/^animations .*/animations = false/' \
		-e 's/^corner_radius.*/corner_radius = 0/' -e 's/^opacity .*/opacity = 1.0/' \
		"$TMP/cfg/gluemac/config.conf"
}
case "$MODE" in
full) ;;
noeffects) strip_effects ;;
noauto) sed -i '/^autostart/d' "$TMP/cfg/gluemac/config.conf" ;;
min) sed -i '/^autostart/d' "$TMP/cfg/gluemac/config.conf"; strip_effects ;;
*) echo "unknown mode: $MODE (full|noeffects|noauto|min)"; exit 1 ;;
esac

QS_BEFORE=$(pgrep -u "$USER" -x quickshell | sort)

: > "$LOG"
( while :; do sync -d "$LOG" 2>/dev/null || sync; sleep 0.3; done ) &
SYNCER=$!

echo "== mode=$MODE $(date) ==" >> "$LOG"
env -u WAYLAND_DISPLAY -u DISPLAY XDG_CONFIG_HOME="$TMP/cfg" \
	setsid "$BIN" -d 2>> "$LOG" &
GPID=$!
( sleep "$TIMEOUT"; echo "== watchdog fired, killing group $GPID ==" >> "$LOG"; \
	kill -9 -"$GPID" 2>/dev/null ) &
WD=$!
wait "$GPID"
RC=$?
echo "== gluemac exited rc=$RC ==" >> "$LOG"
kill "$WD" 2>/dev/null

# quickshell --daemonize escapes the process group; kill only instances
# that appeared during the test
for p in $(pgrep -u "$USER" -x quickshell | sort); do
	echo "$QS_BEFORE" | grep -qx "$p" || kill -9 "$p" 2>/dev/null
done

kill "$SYNCER" 2>/dev/null
sync
echo "log: $LOG"
tail -5 "$LOG"
