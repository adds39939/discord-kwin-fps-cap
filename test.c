/* Unit tests for the POD surgery in fpscap.c.
 *
 * Builds formats with the real spa_pod_builder - the same way a capturer does -
 * runs them through rewrite(), then re-parses the result to check both the
 * values and that the POD is still structurally intact. */

#define FPSCAP_TEST
#include "fpscap.c"

#include <spa/pod/builder.h>
#include <spa/pod/parser.h>

static int failures;

static void check(int cond, const char *what)
{
	printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		failures++;
}

/* Read back maxFramerate: returns element count, fills out[] with num/denom. */
static int read_max(const struct spa_pod *pod, uint32_t *num, uint32_t *denom)
{
	const struct spa_pod_prop *p =
		spa_pod_find_prop(pod, NULL, SPA_FORMAT_VIDEO_maxFramerate);
	const struct spa_pod_choice_body *cb;
	const struct spa_fraction *f;

	if (p == NULL)
		return -1;

	if (p->value.type == SPA_TYPE_Fraction) {
		f = (const struct spa_fraction *)SPA_POD_BODY(&p->value);
		*num = f->num;
		*denom = f->denom;
		return 1;
	}
	if (p->value.type == SPA_TYPE_Choice) {
		cb = (const struct spa_pod_choice_body *)SPA_POD_BODY(&p->value);
		f = (const struct spa_fraction *)((const uint8_t *)cb +
			sizeof(struct spa_pod_choice_body));
		*num = f->num;          /* [0] is the default/preferred value */
		*denom = f->denom;
		return (p->value.size - sizeof(struct spa_pod_choice_body)) / cb->child.size;
	}
	return -2;
}

/* Walk every prop to prove sizes still add up after we mutate/append. */
static int pod_walks_cleanly(const struct spa_pod *pod)
{
	const struct spa_pod_object *obj = (const struct spa_pod_object *)pod;
	const struct spa_pod_prop *p;
	int n = 0;

	if (pod->type != SPA_TYPE_Object)
		return -1;
	SPA_POD_OBJECT_FOREACH(obj, p) {
		if (p->value.size > pod->size)
			return -1;
		n++;
	}
	return n;
}

static struct spa_pod *video_choice(uint8_t *buf, size_t n, uint32_t max)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, n);
	return spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType,         SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype,      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
		SPA_FORMAT_VIDEO_maxFramerate,
			SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(max, 1),
						      &SPA_FRACTION(0, 1),
						      &SPA_FRACTION(max, 1)));
}

static struct spa_pod *video_plain(uint8_t *buf, size_t n, uint32_t max)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, n);
	return spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType,         SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype,      SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
		SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_Fraction(&SPA_FRACTION(max, 1)));
}

static struct spa_pod *video_nomax(uint8_t *buf, size_t n)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, n);
	return spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw));
}

static struct spa_pod *audio(uint8_t *buf, size_t n)
{
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, n);
	return spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_audio),
		SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw));
}

int main(void)
{
	uint8_t buf[1024];
	const struct spa_pod *in, *out;
	uint32_t num = 0, denom = 0;
	int props_before, n;

	g_cap = 60;
	g_debug = 0;
	g_inited = 1;

	printf("Choice(Range) 240 -> cap 60\n");
	in = video_choice(buf, sizeof(buf), 240);
	props_before = pod_walks_cleanly(in);
	out = rewrite(in);
	check(out != in, "returns a rewritten copy");
	n = read_max(out, &num, &denom);
	check(n == 3, "still a 3-element range");
	check(num == 60 && denom == 1, "default clamped to 60/1");
	check(pod_walks_cleanly(out) == props_before, "prop count unchanged");

	printf("\nBare Fraction 240/1 -> cap 60\n");
	in = video_plain(buf, sizeof(buf), 240);
	out = rewrite(in);
	check(out != in, "returns a rewritten copy");
	check(read_max(out, &num, &denom) == 1, "still a bare fraction");
	check(num == 60 && denom == 1, "clamped to 60/1");

	printf("\nAlready 30 -> untouched\n");
	in = video_plain(buf, sizeof(buf), 30);
	out = rewrite(in);
	check(out == in, "original returned, nothing copied");

	printf("\nNo maxFramerate -> appended\n");
	in = video_nomax(buf, sizeof(buf));
	props_before = pod_walks_cleanly(in);
	check(read_max(in, &num, &denom) == -1, "absent to begin with");
	out = rewrite(in);
	check(out != in, "returns a rewritten copy");
	n = read_max(out, &num, &denom);
	check(n == 3, "appended as a 3-element range");
	check(num == 60 && denom == 1, "default is 60/1");
	check(pod_walks_cleanly(out) == props_before + 1, "exactly one prop added");

	printf("\nAudio format -> untouched\n");
	in = audio(buf, sizeof(buf));
	out = rewrite(in);
	check(out == in, "audio passes through unmodified");

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
	       failures, failures == 1 ? "" : "s");
	return failures != 0;
}
