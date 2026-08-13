#!/bin/sh
# Launch Discord with the capture framerate cap applied.
#   ./run-discord.sh          cap at 60
#   DISCORD_CAPTURE_FPS_CAP=30 ./run-discord.sh
#   FPSCAP_DEBUG=1 ./run-discord.sh    log every param rewritten
exec env LD_PRELOAD="$(dirname "$(readlink -f "$0")")/libfpscap.so" /usr/bin/discord "$@"
