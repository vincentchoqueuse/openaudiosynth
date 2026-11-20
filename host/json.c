/* json.c — recursive-descent JSON parser (see json.h). Nesting depth is limited to 64. */
#include "json.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *start;
    char *err;
    size_t errlen;
    int failed;
} parser_t;

static int line_of(const parser_t *ps)
{
    int line = 1;
    for (const char *c = ps->start; c < ps->p; c++)
        line += *c == '\n';
    return line;
}

static void fail(parser_t *ps, const char *msg)
{
    if (!ps->failed && ps->err)
        snprintf(ps->err, ps->errlen, "line %d: %s", line_of(ps), msg);
    ps->failed = 1;
}

static void skip_ws(parser_t *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')
        ps->p++;
}

static json_t *node(json_type_t t)
{
    json_t *j = calloc(1, sizeof *j);
    if (!j) {
        fputs("json: out of memory\n", stderr);
        abort(); /* host tools only: a parse cannot go on without its nodes */
    }
    j->type = t;
    return j;
}

/* realloc that stops the program when memory runs out (host tools: nothing sensible to do then) */
static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q) {
        fputs("json: out of memory\n", stderr);
        abort();
    }
    return q;
}

static char *parse_string_raw(parser_t *ps)
{
    if (*ps->p != '"') {
        fail(ps, "expected '\"'");
        return NULL;
    }
    ps->p++;
    size_t cap = 32, n = 0;
    char *s = xrealloc(NULL, cap);
    while (*ps->p && *ps->p != '"') {
        char c = *ps->p++;
        if (c == '\\') {
            const char e = *ps->p++;
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': { /* keep ASCII, replace anything else with '?' */
                unsigned v = 0;
                for (int i = 0; i < 4; i++) {
                    const char h = *ps->p;
                    const int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (d < 0) { /* also stops at the end of the text */
                        fail(ps, "bad \\u escape");
                        free(s);
                        return NULL;
                    }
                    v = v * 16 + (unsigned)d;
                    ps->p++;
                }
                c = v < 128 ? (char)v : '?';
                break;
            }
            default:
                fail(ps, "bad escape");
                free(s);
                return NULL;
            }
        }
        if (n + 2 > cap)
            s = xrealloc(s, cap *= 2);
        s[n++] = c;
    }
    if (*ps->p != '"') {
        fail(ps, "unterminated string");
        free(s);
        return NULL;
    }
    ps->p++;
    s[n] = 0;
    return s;
}

static json_t *parse_value(parser_t *ps, int depth);

static json_t *parse_container(parser_t *ps, int depth, int is_obj)
{
    json_t *j = node(is_obj ? JSON_OBJ : JSON_ARR), **tail = &j->child;
    const char close = is_obj ? '}' : ']';
    ps->p++;
    skip_ws(ps);
    if (*ps->p == close) {
        ps->p++;
        return j;
    }
    for (;;) {
        char *key = NULL;
        skip_ws(ps);
        if (is_obj) {
            key = parse_string_raw(ps);
            if (!key)
                break;
            skip_ws(ps);
            if (*ps->p != ':') {
                fail(ps, "expected ':'");
                free(key);
                break;
            }
            ps->p++;
        }
        json_t *v = parse_value(ps, depth + 1);
        if (!v) {
            free(key);
            break;
        }
        v->key = key;
        *tail = v;
        tail = &v->next;
        skip_ws(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == close) {
            ps->p++;
            return j;
        }
        fail(ps, is_obj ? "expected ',' or '}'" : "expected ',' or ']'");
        break;
    }
    json_free(j);
    return NULL;
}

static json_t *parse_value(parser_t *ps, int depth)
{
    if (depth > 64) {
        fail(ps, "nesting too deep");
        return NULL;
    }
    skip_ws(ps);
    const char c = *ps->p;
    if (c == '{' || c == '[')
        return parse_container(ps, depth, c == '{');
    if (c == '"') {
        char *s = parse_string_raw(ps);
        if (!s)
            return NULL;
        json_t *j = node(JSON_STR);
        j->str = s;
        return j;
    }
    if (!strncmp(ps->p, "true", 4) || !strncmp(ps->p, "false", 5)) {
        json_t *j = node(JSON_BOOL);
        j->boolean = c == 't';
        ps->p += j->boolean ? 4 : 5;
        return j;
    }
    if (!strncmp(ps->p, "null", 4)) {
        ps->p += 4;
        return node(JSON_NULL);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        /* the JSON number grammar, scanned here; strtod then reads a copy whose '.' is the locale's decimal
         * point (a host such as a DAW may run under a locale that writes "0,5") */
        const char *q = ps->p;
        if (*q == '-')
            q++;
        const char *digits = q;
        while (*q >= '0' && *q <= '9')
            q++;
        int ok = q > digits;
        if (*q == '.') {
            const char *f = ++q;
            while (*q >= '0' && *q <= '9')
                q++;
            ok &= q > f;
        }
        if (*q == 'e' || *q == 'E') {
            q++;
            if (*q == '+' || *q == '-')
                q++;
            const char *x = q;
            while (*q >= '0' && *q <= '9')
                q++;
            ok &= q > x;
        }
        char buf[64];
        const size_t len = (size_t)(q - ps->p);
        if (!ok || len >= sizeof buf) {
            fail(ps, "bad number");
            return NULL;
        }
        memcpy(buf, ps->p, len);
        buf[len] = 0;
        const char point = localeconv()->decimal_point[0];
        for (char *b = buf; *b; b++)
            if (*b == '.')
                *b = point;
        char *end;
        const double v = strtod(buf, &end);
        if (end != buf + len || !(v - v == 0.0)) { /* out of range: inf */
            fail(ps, "bad number");
            return NULL;
        }
        ps->p = q;
        json_t *j = node(JSON_NUM);
        j->num = v;
        return j;
    }
    fail(ps, "unexpected character");
    return NULL;
}

json_t *json_parse(const char *text, char *err, size_t errlen)
{
    parser_t ps = {text, text, err, errlen, 0};
    json_t *j = parse_value(&ps, 0);
    if (j) {
        skip_ws(&ps);
        if (*ps.p) {
            fail(&ps, "trailing characters");
            json_free(j);
            return NULL;
        }
    }
    return j;
}

json_t *json_parse_file(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = size >= 0 ? malloc((size_t)size + 1) : NULL; /* size < 0: not a regular file (pipe...) */
    if (!buf) {
        fclose(f);
        snprintf(err, errlen, "cannot read %s (not a regular file, or out of memory)", path);
        return NULL;
    }
    const size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    json_t *j = json_parse(buf, err, errlen);
    free(buf);
    return j;
}

void json_free(json_t *j)
{
    while (j) {
        json_t *next = j->next;
        json_free(j->child);
        free(j->key);
        free(j->str);
        free(j);
        j = next;
    }
}

const json_t *json_get(const json_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJ)
        return NULL;
    for (const json_t *c = obj->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key))
            return c;
    return NULL;
}
