#include <assert.h>

#include <euphemus.h>

void test_gen(struct eu_value value, struct eu_string_ref expected)
{
	struct eu_generate *eg;
	char *buf = malloc(expected.len + 100);
	char *buf2 = malloc(expected.len + 100);
	size_t i, len, len2;

	/* Test generation in one go. */
	eg = eu_generate_create(value);
	len = eu_generate(eg, buf, expected.len + 100);
	assert(len <= expected.len);
	assert(!eu_generate(eg, buf + len, 1));
	assert(eu_generate_ok(eg));
	eu_generate_destroy(eg);
	assert(eu_string_ref_equal(eu_string_ref(buf, len), expected));

	/* Test generation broken into two chunks */
	for (i = 0;; i++) {
		eg = eu_generate_create(value);
		len = eu_generate(eg, buf, i);
		assert(len <= i);
		len2 = eu_generate(eg, buf2, expected.len + 1);
		assert(len + len2 <= expected.len);
		assert(eu_generate_ok(eg));
		eu_generate_destroy(eg);

		memcpy(buf + i, buf2, len2);
		assert(eu_string_ref_equal(eu_string_ref(buf, len + len2),
					   expected));

		if (len2 == 0)
			break;
	}

	/* Test byte-at-a-time generation */
	eg = eu_generate_create(value);
	len = 0;

	while (eu_generate(eg, buf2, 1)) {
		assert(len < expected.len);
		buf[len++] = *buf2;
	}

	assert(eu_generate_ok(eg));
	eu_generate_destroy(eg);
	assert(eu_string_ref_equal(eu_string_ref(buf, len), expected));

	/* Test that resources are released after an unfinished generation. */
	eg = eu_generate_create(value);
	eu_generate_destroy(eg);

	for (i = 0;; i++) {
		eg = eu_generate_create(value);
		len = eu_generate(eg, buf, i);
		eu_generate_destroy(eg);

		if (len < i)
			break;
	}

	free(buf);
	free(buf2);
}

