void test_gen(struct eu_value value, struct eu_string_ref expected);
void require_fail(const char *requirement, const char *file,
		  int line, const char *func);

#define require(expr) ((expr) ? (void)0 : require_fail(#expr, __FILE__, __LINE__, __func__))
