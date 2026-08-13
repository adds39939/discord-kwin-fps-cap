#!/usr/bin/env bash
#
# Install the capture framerate cap and wire it into your Discord launchers.
#
#   bash <(curl -L https://raw.githubusercontent.com/adds39939/discord-kwin-fps-cap/main/install.sh)
#
#   ./install.sh              interactive
#   ./install.sh --yes        accept defaults, no prompts
#   ./install.sh --cap 30     set the framerate ceiling
#
# Run from a checkout or a release it uses what is beside it; run on its own it
# fetches the latest release. LIB_DIR and APP_DIR override install locations.

set -euo pipefail

REPO="adds39939/discord-kwin-fps-cap"
TARBALL_URL="https://github.com/$REPO/releases/latest/download/discord-kwin-fps-cap.tar.gz"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd)" || HERE="$PWD"
LIB_DIR="${LIB_DIR:-$HOME/.local/lib}"
APP_DIR="${APP_DIR:-$HOME/.local/share/applications}"
SYS_APP_DIRS="${SYS_APP_DIRS:-/usr/share/applications /usr/local/share/applications}"

LIB_NAME="libfpscap.so"
MARKER="X-FpsCap"
BACKUP_SUFFIX=".fpscap-backup"

CAP=60
ASSUME_YES=0

# ---------- output ----------

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	B=$'\033[1m'; DIM=$'\033[2m'; GRN=$'\033[32m'; YEL=$'\033[33m'; RED=$'\033[31m'; R=$'\033[0m'
else
	B=""; DIM=""; GRN=""; YEL=""; RED=""; R=""
fi

say()  { printf '%s\n' "$*"; }
ok()   { printf '  %s%s%s %s\n' "$GRN" "OK" "$R" "$*"; }
warn() { printf '  %s%s%s %s\n' "$YEL" "!!" "$R" "$*"; }
die()  { printf '%sFailed:%s %s\n' "$RED" "$R" "$*" >&2; exit 1; }

ask() {
	# ask <prompt> <default y|n>
	local prompt="$1" def="$2" reply
	if [ "$ASSUME_YES" = 1 ]; then return 0; fi
	if [ "$def" = y ]; then prompt="$prompt [Y/n] "; else prompt="$prompt [y/N] "; fi
	read -r -p "$prompt" reply </dev/tty || reply=""
	reply="${reply:-$def}"
	case "$reply" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

usage() {
	sed -n '3,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
	exit 0
}

while [ $# -gt 0 ]; do
	case "$1" in
		--yes|-y) ASSUME_YES=1 ;;
		--cap) shift; CAP="${1:-60}" ;;
		--cap=*) CAP="${1#*=}" ;;
		-h|--help) usage ;;
		*) die "unknown option: $1 (try --help)" ;;
	esac
	shift
done

case "$CAP" in
	''|*[!0-9]*) die "--cap must be a whole number, got '$CAP'" ;;
esac
[ "$CAP" -gt 0 ] 2>/dev/null || die "--cap must be greater than zero"

# ---------- locate the library ----------

fetch() {
	# fetch <url> <dest>
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL "$1" -o "$2"
	elif command -v wget >/dev/null 2>&1; then
		wget -qO "$2" "$1"
	else
		die "need curl or wget to download the release"
	fi
}

# Pull the latest release into a temp dir and point HERE at it.
bootstrap() {
	local tmp
	command -v tar >/dev/null 2>&1 || die "need tar to unpack the release"
	tmp="$(mktemp -d)"
	say "${DIM}Fetching the latest release...${R}" >&2
	fetch "$TARBALL_URL" "$tmp/release.tar.gz" \
		|| die "could not download $TARBALL_URL"
	tar -xzf "$tmp/release.tar.gz" -C "$tmp" --strip-components=1 \
		|| die "could not unpack the release"
	HERE="$tmp"
}

find_library() {
	[ -f "$HERE/$LIB_NAME" ] || [ -f "$HERE/fpscap.c" ] || bootstrap

	if [ -f "$HERE/$LIB_NAME" ]; then
		printf '%s' "$HERE/$LIB_NAME"
		return 0
	fi
	if [ -f "$HERE/build.sh" ] && [ -f "$HERE/fpscap.c" ]; then
		say "${DIM}No prebuilt library here; building from source...${R}" >&2
		( cd "$HERE" && ./build.sh >/dev/null 2>&1 ) \
			|| die "build failed - you need a C compiler and the PipeWire headers (libpipewire-0.3)"
		if [ -f "$HERE/$LIB_NAME" ]; then
			printf '%s' "$HERE/$LIB_NAME"
			return 0
		fi
	fi
	return 1
}

# ---------- desktop entries ----------

# Print the basenames of every Discord launcher we can find, once each.
find_entries() {
	{
		for d in $SYS_APP_DIRS "$APP_DIR"; do
			[ -d "$d" ] || continue
			for f in "$d"/*iscord*.desktop; do
				[ -e "$f" ] || continue
				basename "$f"
			done
		done
	} | sort -u
}

# Where should we read the pristine version of this entry from?
source_for() {
	local name="$1" backup="$APP_DIR/$1$BACKUP_SUFFIX"
	if [ -f "$backup" ]; then
		printf '%s' "$backup"; return 0
	fi
	if [ -f "$APP_DIR/$name" ] && ! grep -q "^$MARKER=" "$APP_DIR/$name" 2>/dev/null; then
		printf '%s' "$APP_DIR/$name"; return 0
	fi
	for d in $SYS_APP_DIRS; do
		[ -f "$d/$name" ] && { printf '%s' "$d/$name"; return 0; }
	done
	# Only an already-patched user entry exists and we have no pristine copy.
	[ -f "$APP_DIR/$name" ] && { printf '%s' "$APP_DIR/$name"; return 0; }
	return 1
}

patch_entry() {
	local name="$1" src dest tmp
	dest="$APP_DIR/$name"

	src="$(source_for "$name")" || { warn "$name: no source entry found, skipping"; return 1; }

	# Preserve a user's own entry the first time we touch it.
	if [ "$src" = "$dest" ] && [ ! -f "$dest$BACKUP_SUFFIX" ] \
	   && ! grep -q "^$MARKER=" "$dest" 2>/dev/null; then
		cp -p "$dest" "$dest$BACKUP_SUFFIX"
	fi

	tmp="$(mktemp)"
	awk -v lib="$LIB_DIR/$LIB_NAME" -v cap="$CAP" -v marker="$MARKER" '
		/^\[Desktop Entry\]/ { print; print marker "=true"; next }
		$0 ~ "^" marker "="  { next }
		/^Exec=/ {
			line = substr($0, 6)
			sub(/^env +LD_PRELOAD=[^ ]+ +/, "", line)
			sub(/^DISCORD_CAPTURE_FPS_CAP=[^ ]+ +/, "", line)
			printf "Exec=env LD_PRELOAD=%s DISCORD_CAPTURE_FPS_CAP=%s %s\n", lib, cap, line
			next
		}
		{ print }
	' "$src" > "$tmp"

	grep -q "^Exec=env LD_PRELOAD=" "$tmp" || {
		rm -f "$tmp"; warn "$name: no Exec line to patch, skipping"; return 1
	}

	mkdir -p "$APP_DIR"
	mv "$tmp" "$dest"
	chmod 644 "$dest"
	ok "$name"
	return 0
}

# ---------- run ----------

say ""
say "${B}Discord capture framerate cap${R}"
say "${DIM}Caps the frames Discord requests from the compositor, so it stops"
say "burning GPU on frames it throws away.${R}"
say ""

LIB_SRC="$(find_library)" || die "$LIB_NAME not found next to this script, and no sources to build it from"

mapfile -t ENTRIES < <(find_entries)
if [ "${#ENTRIES[@]}" -eq 0 ]; then
	warn "No Discord launchers found in: $SYS_APP_DIRS $APP_DIR"
	say ""
	say "The library can still be installed and used manually:"
	say "  ${DIM}LD_PRELOAD=$LIB_DIR/$LIB_NAME discord${R}"
	ask "Install the library anyway?" y || { say "Nothing done."; exit 0; }
	ENTRIES=()
else
	say "Launchers found:"
	for e in "${ENTRIES[@]}"; do say "  ${B}$e${R}"; done
	say ""
fi

if [ "$ASSUME_YES" = 0 ] && [ -t 0 ]; then
	read -r -p "Framerate ceiling [$CAP]: " reply </dev/tty || reply=""
	if [ -n "$reply" ]; then
		case "$reply" in
			''|*[!0-9]*) die "not a number: $reply" ;;
			*) CAP="$reply" ;;
		esac
	fi
	say ""
fi

say "Will install:"
say "  ${DIM}$LIB_DIR/$LIB_NAME${R}"
[ "${#ENTRIES[@]}" -gt 0 ] && say "  ${DIM}$APP_DIR/{$(printf '%s,' "${ENTRIES[@]}" | sed 's/,$//')}${R}"
say "  ${DIM}capping capture at $CAP fps${R}"
say ""
ask "Proceed?" y || { say "Nothing done."; exit 0; }
say ""

mkdir -p "$LIB_DIR"
install -m 755 "$LIB_SRC" "$LIB_DIR/$LIB_NAME"
ok "installed $LIB_DIR/$LIB_NAME"

for e in "${ENTRIES[@]:-}"; do
	[ -n "$e" ] && patch_entry "$e" || true
done

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "$APP_DIR" 2>/dev/null || true
fi

# Ship an uninstaller alongside the library if we were run from a release.
if [ -f "$HERE/uninstall.sh" ]; then
	install -m 755 "$HERE/uninstall.sh" "$LIB_DIR/fpscap-uninstall.sh"
	ok "uninstaller at $LIB_DIR/fpscap-uninstall.sh"
fi

say ""
say "${GRN}${B}Done.${R} Fully quit Discord (including the tray icon) and start it again."
say ""
say "To check it took, with a stream running:"
say "  ${DIM}pw-dump | grep -A2 maxFramerate | head${R}"
say "Both the compositor and Discord should report a ceiling of $CAP."
say ""
if [ -f "$LIB_DIR/fpscap-uninstall.sh" ]; then
	say "To remove: ${DIM}$LIB_DIR/fpscap-uninstall.sh${R}"
else
	say "To remove: ${DIM}bash <(curl -L https://raw.githubusercontent.com/$REPO/main/uninstall.sh)${R}"
fi
say ""
