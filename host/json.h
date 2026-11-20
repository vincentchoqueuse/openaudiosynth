/* json.h — minimal JSON reader (RFC 8259 subset: objects, arrays, strings with basic escapes,
 * numbers, true/false/null) building a small tree. Host only: allocates, not real-time. */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef enum { JSON_NULL = 0, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } json_type_t;

typedef struct json {
    json_type_t type;
    char *key;           /* member name when inside an object, else NULL */
    char *str;           /* JSON_STR */
    double num;          /* JSON_NUM */
    int boolean;         /* JSON_BOOL */
    struct json *child;  /* first element / member */
    struct json *next;   /* next sibling */
} json_t;

/* Parses text; returns NULL on error and writes a message with line number into err. */
json_t *json_parse(const char *text, char *err, size_t errlen);
/* Reads and parses a whole file. */
json_t *json_parse_file(const char *path, char *err, size_t errlen);
void json_free(json_t *j);
/* Member of an object, or NULL. */
const json_t *json_get(const json_t *obj, const char *key);

#endif /* JSON_H */
