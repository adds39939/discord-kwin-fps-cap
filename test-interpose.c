/* Smoke test for the dlsym interposition.
 *
 * Run twice and compare:
 *   ./test-interpose                              pw_stream_connect -> real
 *   LD_PRELOAD=./libfpscap.so ./test-interpose    pw_stream_connect -> shim
 *
 * pw_stream_new must resolve to the real library in both cases; only
 * pw_stream_connect should change. */

#include <dlfcn.h>
#include <stdio.h>

int main(void)
{
	void *h = dlopen("libpipewire-0.3.so.0", RTLD_LAZY);

	if (h == NULL) {
		printf("dlopen failed: %s\n", dlerror());
		return 1;
	}
	printf("pw_stream_connect = %p\n", dlsym(h, "pw_stream_connect"));
	printf("pw_stream_new     = %p\n", dlsym(h, "pw_stream_new"));
	return 0;
}
