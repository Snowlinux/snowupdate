#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <ctype.h>


#include "rfc822.h"

#include <glib.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>


/*
 * Function: rfc822db_parse_stanza
 * Input: a FILE pointer to an open readable file containing a stanza in rfc822 
 *    format.
 * Output: a pointer to a dynamically allocated rfc822_header structure
 * Description: parse a stanza from file into the returned header struct
 * Assumptions: no lines are over 8192 bytes long.
 */

struct rfc822_header* rfc822_parse_stanza(FILE *file)
{
    struct rfc822_header *head, **tail, *cur;
    char buf[8192];

    head = NULL;
    tail = &head;
    cur = NULL;

    /*    fprintf(stderr,"rfc822db_parse_stanza(file)\n");*/
    while (fgets(buf, sizeof(buf), file))
    {
        char *tmp = buf;

        if (*tmp == '\n')
            break;

	if (buf[strlen(buf)-1] == '\n') 
	   buf[strlen(buf)-1] = '\0';

	// " ." -> \n 
	if (isspace(*tmp) && 
	    *(tmp+1) != 0 && *(tmp+1) == '.' &&
	    *(tmp+2) == 0)
	{
	   gchar *now = cur->value;
	   cur->value = g_strconcat(now, "\n", NULL);
	   g_free(now);
	}
	else if (isspace(*tmp))
        {
	   gchar *now = cur->value;
	   if(strlen(now) == 0 || g_str_has_suffix(now, " ") || g_str_has_suffix(now, "\n"))
	      cur->value = g_strconcat(now, g_strstrip(tmp), NULL);
	   else	      
	      cur->value = g_strconcat(now, " ", g_strstrip(tmp), NULL);
	   g_free(now);
        } 
        else 
        {
            while (*tmp != 0 && *tmp != ':')
                tmp++;
            *tmp++ = '\0';

            cur = g_new0(struct rfc822_header,1);
            if (cur == NULL)
                return NULL;
            cur->header = strdup(buf);

            while (isspace(*tmp))
                tmp++;
            cur->value = strdup(tmp);

            *tail = cur;
            tail = &cur->next;
        }
    }

    return head;
}


char *rfc822_header_lookup(struct rfc822_header *list, const char* key)
{
/*    fprintf(stderr,"rfc822db_header_lookup(list,key=%s)\n",key);*/
    while (list && (strcasecmp(key, list->header) != 0))
        list = list->next;
    if (!list)
        return NULL;
/*    fprintf(stderr,"rfc822db_header_lookup returning: '%s'\n", list->value);*/
    return list->value;
}


void rfc822_header_free_all(struct rfc822_header *list)
{
   while (list) { 
      struct rfc822_header *now = list;
      g_free(list->header);
      g_free(list->value);

      list = list->next;
      g_free(now);
   }
}
