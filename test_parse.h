#define TEST_PARSE(json_str, result_type, parse_init, check, cleanup) \
do {                                                                  \
	struct eu_parse ep;                                           \
	const char *json = json_str;                                  \
	result_type result;                                           \
	size_t len = strlen(json);                                    \
	size_t i;                                                     \
								      \
	/* Test parsing in one go */ 				      \
	parse_init(&ep, &result);                                     \
	assert(eu_parse(&ep, json, len));                             \
	assert(eu_parse_finish(&ep));                                 \
	eu_parse_fini(&ep);                                           \
	check;                                                        \
	cleanup;                                                      \
                                                                      \
	/* Test parsing broken at each position within the json */    \
	for (i = 0; i < len; i++) {                                   \
		parse_init(&ep, &result);                             \
		assert(eu_parse(&ep, json, i));                       \
		assert(eu_parse(&ep, json + i, len - i));             \
		assert(eu_parse_finish(&ep));                         \
		eu_parse_fini(&ep);                                   \
		check;                                                \
		cleanup;                                              \
	}                                                             \
                                                                      \
	/* Test parsing with the json broken into individual bytes */ \
	parse_init(&ep, &result);                                     \
	for (i = 0; i < len; i++)                                     \
		assert(eu_parse(&ep, json + i, 1));                   \
                                                                      \
	assert(eu_parse_finish(&ep));                                 \
	eu_parse_fini(&ep);                                           \
	check;                                                        \
	cleanup;                                                      \
                                                                      \
	/* Test that resources are released after an unfinished parse. */ \
	parse_init(&ep, &result);                                     \
	eu_parse_fini(&ep);                                           \
                                                                      \
	for (i = 0; i < len; i++) {                                   \
		parse_init(&ep, &result);                             \
		assert(eu_parse(&ep, json, i));                       \
		eu_parse_fini(&ep);                                   \
	}                                                             \
} while (0)