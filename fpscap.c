/*
 * fpscap - clamp the framerate Discord asks the Wayland portal for.
 *
 * Discord's Linux screen capture (capture_linux, inside discord_voice.node)
 * proposes a PipeWire format with no framerate bound, so kwin offers a ceiling
 * derived from the output's refresh rate - 240 on a 240Hz display - and hands
 * over that many 4K buffers per second to emit 60. The surplus is scaled and
 * converted on the GPU's shaders, then thrown away.
 *
 * Discord resolves PipeWire entirely through dlopen/dlsym, so we interpose
 * dlsym, hand back our own pw_stream_connect, and clamp
 * SPA_FORMAT_VIDEO_maxFramerate in the EnumFormat params before negotiation
 * ever sees them. Audio streams and non-video formats pass through untouched.
 *
 * Build:  ./build.sh
 * Use:    LD_PRELOAD=/path/libfpscap.so discord
 * Tune:   DISCORD_CAPTURE_FPS_CAP=30   (default 60)
 *         FPSCAP_DEBUG=1               (log every param seen)
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <spa/pod/pod.h>
#include <spa/pod/iter.h>
#include <spa/param/format.h>
#include <spa/utils/type.h>

#define DEFAULT_CAP 60
#define HEADROOM    128   /* slack for appending a prop */

static uint32_t g_cap;
static int g_debug;
static int g_inited;

static void log_init(void)
{
	const char *cap = getenv("DISCORD_CAPTURE_FPS_CAP");
	const char *dbg = getenv("FPSCAP_DEBUG");

	g_cap = DEFAULT_CAP;
	if (cap != NULL) {
		long v = strtol(cap, NULL, 10);
		if (v > 0 && v <= 1000)
			g_cap = (uint32_t)v;
	}
	g_debug = (dbg != NULL && dbg[0] == '1');
	g_inited = 1;
	fprintf(stderr, "[fpscap] active, capping capture maxFramerate at %u\n", g_cap);
}

/* ---------- POD surgery ---------- */

static int clamp_fraction(struct spa_fraction *f)
{
	if (f->denom == 0)
		return 0;
	if ((uint64_t)f->num <= (uint64_t)g_cap * f->denom)
		return 0;
	f->num = g_cap;
	f->denom = 1;
	return 1;
}

/* A maxFramerate value is either a bare Fraction or a Choice (Range/Enum) of
 * them. Range elements are [default, min, max]; clamping every element leaves
 * a lower min alone and pulls default/max down to the cap. */
static int clamp_value(struct spa_pod *val)
{
	struct spa_pod_choice_body *cb;
	uint8_t *elems;
	uint32_t n, i, stride;
	int changed = 0;

	if (val->type == SPA_TYPE_Fraction) {
		if (val->size < sizeof(struct spa_fraction))
			return 0;
		return clamp_fraction((struct spa_fraction *)SPA_POD_BODY(val));
	}

	if (val->type != SPA_TYPE_Choice)
		return 0;
	if (val->size < sizeof(struct spa_pod_choice_body))
		return 0;

	cb = (struct spa_pod_choice_body *)SPA_POD_BODY(val);
	if (cb->child.type != SPA_TYPE_Fraction)
		return 0;

	stride = cb->child.size;
	if (stride < sizeof(struct spa_fraction))
		return 0;

	elems = (uint8_t *)cb + sizeof(struct spa_pod_choice_body);
	n = (val->size - sizeof(struct spa_pod_choice_body)) / stride;
	for (i = 0; i < n; i++)
		changed |= clamp_fraction((struct spa_fraction *)(elems + (size_t)i * stride));

	return changed;
}

/* Append maxFramerate as Choice(Range){ cap, 0/1, cap } when absent.
 * Layout is 14 x uint32 = 56 bytes, already 8-byte aligned. */
static void append_max_framerate(struct spa_pod *pod)
{
	uint32_t w[14];
	uint8_t *end = (uint8_t *)pod + sizeof(struct spa_pod) + pod->size;

	w[0]  = SPA_FORMAT_VIDEO_maxFramerate;
	w[1]  = 0;                       /* prop flags   */
	w[2]  = 40;                      /* value.size   */
	w[3]  = SPA_TYPE_Choice;
	w[4]  = SPA_CHOICE_Range;
	w[5]  = 0;                       /* choice flags */
	w[6]  = 8;                       /* child.size   */
	w[7]  = SPA_TYPE_Fraction;
	w[8]  = g_cap; w[9]  = 1;        /* default */
	w[10] = 0;     w[11] = 1;        /* min     */
	w[12] = g_cap; w[13] = 1;        /* max     */

	memcpy(end, w, sizeof(w));
	pod->size += sizeof(w);
}

static int is_video_format(const struct spa_pod *pod)
{
	const struct spa_pod_object *obj;
	const struct spa_pod_prop *mt;

	if (pod->type != SPA_TYPE_Object)
		return 0;
	if (pod->size < sizeof(struct spa_pod_object_body))
		return 0;

	obj = (const struct spa_pod_object *)pod;
	if (obj->body.type != SPA_TYPE_OBJECT_Format)
		return 0;

	mt = spa_pod_find_prop(pod, NULL, SPA_FORMAT_mediaType);
	if (mt == NULL || mt->value.type != SPA_TYPE_Id)
		return 0;

	return *(const uint32_t *)SPA_POD_BODY(&mt->value) == SPA_MEDIA_TYPE_video;
}

/* Returns a rewritten copy, or the original if nothing needed doing. The copy
 * is intentionally never freed: pw_stream_connect is called a handful of times
 * per session, and leaking a few hundred bytes beats risking a use-after-free
 * if the callee retains the pointer. */
static const struct spa_pod *rewrite(const struct spa_pod *pod)
{
	struct spa_pod *copy;
	struct spa_pod_prop *mf;
	size_t total;

	if (pod == NULL || !is_video_format(pod))
		return pod;

	total = sizeof(struct spa_pod) + pod->size;
	copy = malloc(total + HEADROOM);
	if (copy == NULL)
		return pod;
	memcpy(copy, pod, total);

	mf = (struct spa_pod_prop *)spa_pod_find_prop(copy, NULL,
						      SPA_FORMAT_VIDEO_maxFramerate);
	if (mf != NULL) {
		if (!clamp_value(&mf->value)) {
			free(copy);
			return pod;          /* already at or below the cap */
		}
		if (g_debug)
			fprintf(stderr, "[fpscap] clamped maxFramerate -> %u\n", g_cap);
	} else {
		append_max_framerate(copy);
		if (g_debug)
			fprintf(stderr, "[fpscap] added maxFramerate -> %u\n", g_cap);
	}

	return copy;
}

/* ---------- interposition ---------- */
#ifndef FPSCAP_TEST

typedef int (*connect_fn)(void *stream, int direction, uint32_t target_id,
			  int flags, const struct spa_pod **params, uint32_t n_params);

static connect_fn real_connect;
static void *(*real_dlsym)(void *, const char *);

static void ensure_real_dlsym(void)
{
	static const char *vers[] = { "GLIBC_2.34", "GLIBC_2.2.5", "GLIBC_2.0", NULL };
	int i;

	if (real_dlsym != NULL)
		return;
	for (i = 0; vers[i] != NULL; i++) {
		real_dlsym = dlvsym(RTLD_NEXT, "dlsym", vers[i]);
		if (real_dlsym != NULL)
			return;
	}
}

static int shim_pw_stream_connect(void *stream, int direction, uint32_t target_id,
				  int flags, const struct spa_pod **params,
				  uint32_t n_params)
{
	const struct spa_pod **out = NULL;
	uint32_t i;
	int rc;

	if (!g_inited)
		log_init();

	if (params != NULL && n_params > 0) {
		out = malloc(sizeof(*out) * n_params);
		if (out != NULL) {
			for (i = 0; i < n_params; i++)
				out[i] = rewrite(params[i]);
		}
	}

	rc = real_connect(stream, direction, target_id, flags,
			  out != NULL ? out : params, n_params);
	free(out);
	return rc;
}

/* glibc declares symbol as nonnull, but a caller can still pass NULL and we
 * would rather return cleanly than fault inside strcmp. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
void *dlsym(void *handle, const char *symbol)
{
	ensure_real_dlsym();
	if (real_dlsym == NULL)
		return NULL;

	if (symbol != NULL && strcmp(symbol, "pw_stream_connect") == 0) {
		if (real_connect == NULL)
			real_connect = (connect_fn)real_dlsym(handle, symbol);
		if (real_connect != NULL) {
			if (!g_inited)
				log_init();
			return (void *)shim_pw_stream_connect;
		}
	}

	return real_dlsym(handle, symbol);
}
#pragma GCC diagnostic pop

#endif /* !FPSCAP_TEST */
