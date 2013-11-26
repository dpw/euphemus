#ifndef EUPHEMUS_EUPHEMUS_INT_H
#define EUPHEMUS_EUPHEMUS_INT_H

void insert_cont(struct eu_parse *ep, struct eu_parse_cont *c);
int set_member_name(struct eu_parse *ep, const char *start, const char *end);
int append_member_name(struct eu_parse *ep, const char *start, const char *end);

const char *skip_whitespace(const char *p, const char *end);

#endif
