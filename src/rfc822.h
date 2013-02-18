#ifndef _RFC822_H_
#define _RFC822_H_

struct rfc822_header {
    char *header;
    char *value;
    struct rfc822_header *next;
};

struct rfc822_header *rfc822_parse_stanza(FILE *file);

/* no need to free the returned string, rfc822_header_free_all takes
   care of this */
char *rfc822_header_lookup(struct rfc822_header *list, const char* key);

void rfc822_header_free_all(struct rfc822_header *list);

#endif
