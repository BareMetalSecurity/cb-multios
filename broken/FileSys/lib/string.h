#ifndef STRING_H_
#define STRING_H_

#include <stddef.h>

extern void *cgc_memcpy(void *dest, const void *src, size_t n);
extern void *cgc_memmove(void *dest, const void *src, size_t n);
extern void *cgc_memset(void *dest, int c, size_t n);
extern int cgc_memcmp(void *s1, const void *s2, size_t n);
extern void *cgc_memchr(const void *s, int c, size_t n);

extern size_t cgc_strlen(const char *s);
extern char *cgc_strcpy(char *dest, const char *src);
extern char *cgc_strncpy(char *dest, const char *src, size_t n);
extern char *cgc_strchr(const char *s, int c);
extern char *cgc_strsep(char **stringp, const char *delim);
extern int cgc_strcmp(const char *s1, const char *s2);
extern int cgc_strncmp(const char *s1, const char *s2, size_t n);
extern int cgc_strcasecmp(const char *s1, const char *s2);
extern int cgc_strncasecmp(const char *s1, const char *s2, size_t n);
extern char *cgc_strcat(char *dest, const char *src);
extern char *cgc_strdup(const char *src);

#endif /* !STRING_H_ */
