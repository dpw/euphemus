#define TEST_PARSE(json_str, result_type, to_value, check, cleanup)   \
do {                                                                  \
	struct eu_parse *parse;                                       \
	const char *json = json_str;                                  \
	result_type result;                                           \
	size_t len = strlen(json);                                    \
	size_t i;                                                     \
	char *buf;                                                    \
                                                                      \
	/* Test parsing in one go */                                  \
	parse = eu_parse_create(to_value(&result));                   \
	require(eu_parse(parse, json, len));                           \
	require(eu_parse_finish(parse));                               \
	eu_parse_destroy(parse);                                      \
	check;                                                        \
	cleanup;                                                      \
                                                                      \
	/* Test parsing broken at each position within the json */    \
	for (i = 0; i < len; i++) {                                   \
		parse = eu_parse_create(to_value(&result));           \
                                                                      \
		buf = malloc(i);                                      \
		memcpy(buf, json, i);                                 \
		require(eu_parse(parse, buf, i));                      \
		free(buf);                                            \
                                                                      \
		buf = malloc(len - i);                                \
		memcpy(buf, json + i, len - i);                       \
		require(eu_parse(parse, buf, len - i));                \
		free(buf);                                            \
                                                                      \
		require(eu_parse_finish(parse));                       \
		eu_parse_destroy(parse);                              \
		check;                                                \
		cleanup;                                              \
	}                                                             \
                                                                      \
	/* Test parsing with the json broken into individual bytes */ \
	parse = eu_parse_create(to_value(&result));                   \
	for (i = 0; i < len; i++) {                                   \
		char c = json[i];                                     \
		require(eu_parse(parse, &c, 1));                       \
		require(eu_parse(parse, &c, 0));                       \
	}                                                             \
                                                                      \
	require(eu_parse_finish(parse));                               \
	eu_parse_destroy(parse);                                      \
	check;                                                        \
	cleanup;                                                      \
                                                                      \
	/* Test that resources are released after an unfinished parse. */ \
	parse = eu_parse_create(to_value(&result));                   \
	eu_parse_destroy(parse);                                      \
                                                                      \
	for (i = 0; i < len; i++) {                                   \
		parse = eu_parse_create(to_value(&result));           \
		require(eu_parse(parse, json, i));                     \
		eu_parse_destroy(parse);                              \
	}                                                             \
} while (0)
