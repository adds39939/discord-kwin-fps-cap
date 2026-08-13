#!/bin/sh
set -e
cd "$(dirname "$0")"

CFLAGS="$(pkg-config --cflags libpipewire-0.3)"

gcc -shared -fPIC -O2 -Wall -Wextra -o libfpscap.so fpscap.c $CFLAGS -ldl
gcc -O2 -Wall -o test test.c $CFLAGS
gcc -O2 -Wall -o test-interpose test-interpose.c -ldl

./test

echo
echo "interposition:"
echo "  without shim:"
./test-interpose | sed 's/^/    /'
echo "  with shim:"
LD_PRELOAD="$PWD/libfpscap.so" ./test-interpose | sed 's/^/    /'

echo
echo "built: $PWD/libfpscap.so"
