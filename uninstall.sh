#!/usr/bin/env bash
#
# Remove the capture framerate cap and restore your Discord launchers.
#
#   bash <(curl -L https://raw.githubusercontent.com/adds39939/discord-kwin-fps-cap/main/uninstall.sh)
#
#   ./uninstall.sh            interactive
#   ./uninstall.sh --yes      no prompts
#
# LIB_DIR and APP_DIR override the install locations (used by the tests).

set -euo pipefail

LIB_DIR="${LIB_DIR:-$HOME/.local/lib}"
APP_DIR="${APP_DIR:-$HOME/.local/share/applications}"

LIB_NAME="libfpscap.so"
MARKER="X-FpsCap"
BACKUP_SUFFIX=".fpscap-backup"

ASSUME_YES=0

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	B=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; R=$'\033[0m'
else
	B=""; DIM=""; GRN=""; YEL=""; RED=""; R=""
fi

say()  { printf '%s\n' "$*"; }
ok()   { printf '  %s%s%s %s\n' "$GRN" "OK" "$R" "$*"; }
warn() { printf '  %s%s%s %s\n' "$YEL" "!!" "$R" "$*"; }

ask() {
	local prompt="$1" def="$2" reply
	if [ "$ASSUME_YES" = 1 ]; then return 0; fi
	if [ "$def" = y ]; then prompt="$prompt [Y/n] "; else prompt="$prompt [y/N] "; fi
	read -r -p "$prompt" reply </dev/tty || reply=""
	reply="${reply:-$def}"
	case "$reply" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

while [ $# -gt 0 ]; do
	case "$1" in
		--yes|-y) ASSUME_YES=1 ;;
		-h|--help) sed -n '3,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) printf '%sFailed:%s unknown option: %s\n' "$RED" "$R" "$1" >&2; exit 1 ;;
	esac
	shift
done

say ""
say "${B}Remove the Discord capture framerate cap${R}"
say ""

# ---------- find what we touched ----------

PATCHED=()
if [ -d "$APP_DIR" ]; then
	for f in "$APP_DIR"/*.desktop; do
		[ -e "$f" ] || continue
		grep -q "^$MARKER=" "$f" 2>/dev/null && PATCHED+=("$(basename "$f")")
	done
fi

FOUND=0
if [ "${#PATCHED[@]}" -gt 0 ]; then
	FOUND=1
	say "Patched launchers:"
	for e in "${PATCHED[@]}"; do
		if [ -f "$APP_DIR/$e$BACKUP_SUFFIX" ]; then
			say "  ${B}$e${R} ${DIM}(will restore your original)${R}"
		else
			say "  ${B}$e${R} ${DIM}(will remove; system entry takes over again)${R}"
		fi
	done
	say ""
fi

if [ -f "$LIB_DIR/$LIB_NAME" ]; then
	FOUND=1
	say "Installed library:"
	say "  ${DIM}$LIB_DIR/$LIB_NAME${R}"
	say ""
fi

if [ "$FOUND" = 0 ]; then
	say "Nothing to remove - no patched launchers and no installed library."
	say ""
	exit 0
fi

ask "Remove these?" y || { say "Nothing done."; exit 0; }
say ""

# ---------- remove ----------

for e in "${PATCHED[@]:-}"; do
	[ -n "$e" ] || continue
	if [ -f "$APP_DIR/$e$BACKUP_SUFFIX" ]; then
		mv "$APP_DIR/$e$BACKUP_SUFFIX" "$APP_DIR/$e"
		ok "$e restored"
	else
		rm -f "$APP_DIR/$e"
		ok "$e removed"
	fi
done

rm -f "$LIB_DIR/$LIB_NAME"
[ -f "$LIB_DIR/$LIB_NAME" ] && warn "could not remove $LIB_DIR/$LIB_NAME" || ok "library removed"
rm -f "$LIB_DIR/fpscap-uninstall.sh"

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "$APP_DIR" 2>/dev/null || true
fi

say ""
say "${GRN}${B}Done.${R} Fully quit Discord (including the tray icon) and start it again."
say ""
