// jsmn's definitions live here; other users include only its declarations through boat_json.h.
#define BOAT_JSON_IMPLEMENTATION
#define JSMN_STRICT
#include "boat_json.h"

#include <stdlib.h>
#include <string.h>

int boat_json_next(const jsmntok_t *tokens, int count, int index)
{
    int end = tokens[index].end;
    ++index;
    while (index < count && tokens[index].start < end)
        ++index;
    return index;
}

int boat_json_member(
    const char *json, const jsmntok_t *tokens, int count, int object, const char *key)
{
    int found = -1;
    for (int i = object + 1; i < count && tokens[i].start < tokens[object].end;) {
        int value = i + 1;
        if (tokens[i].type != JSMN_STRING || value >= count
            || tokens[value].start >= tokens[object].end)
            return -2;
        if ((size_t)(tokens[i].end - tokens[i].start) == strlen(key)
            && !memcmp(json + tokens[i].start, key, strlen(key))) {
            if (found >= 0)
                return -2;
            found = value;
        }
        i = boat_json_next(tokens, count, value);
    }
    return found;
}

bool boat_json_text(const char *json, const jsmntok_t *t, char *out, size_t capacity)
{
    if (t->type != JSMN_STRING)
        return false;
    size_t n = 0;
    for (int i = t->start; i < t->end; ++i) {
        unsigned char c = (unsigned char)json[i];
        if (c == '\\') {
            if (++i >= t->end)
                return false;
            c = (unsigned char)json[i];
            if (c == 'u') {
                if (i + 4 >= t->end || json[i + 1] != '0' || json[i + 2] != '0')
                    return false;
                char hex[3] = { json[i + 3], json[i + 4], 0 }, *end;
                long value = strtol(hex, &end, 16);
                if (*end || end != hex + 2)
                    return false;
                c = (unsigned char)value;
                i += 4;
            } else if (c != '"' && c != '\\' && c != '/')
                return false;
        }
        if (c < 32 || c > 126 || n + 1 >= capacity)
            return false;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n != 0;
}

bool boat_json_int(const char *json, const jsmntok_t *t, int *out)
{
    if (t->type != JSMN_PRIMITIVE || t->end <= t->start || t->end - t->start > 9)
        return false;
    int n = 0;
    for (int i = t->start; i < t->end; ++i) {
        if (json[i] < '0' || json[i] > '9')
            return false;
        n = n * 10 + json[i] - '0';
    }
    *out = n;
    return true;
}

// jsmn tokenizes strings and nesting but deliberately does not enforce separators.
static void whitespace(const char *json, size_t length, size_t *pos)
{
    while (*pos < length && strchr(" \t\r\n", json[*pos]))
        ++*pos;
}
static bool value(const char *json, size_t length, size_t *pos, unsigned depth)
{
    whitespace(json, length, pos);
    if (*pos >= length || depth > 32)
        return false;
    char c = json[(*pos)++];
    if (c == '"') {
        while (*pos < length) {
            c = json[(*pos)++];
            if (c == '"')
                return true;
            if ((unsigned char)c < 32)
                return false;
            if (c == '\\') {
                if (*pos >= length || !strchr("\"\\/bfnrtu", json[*pos]))
                    return false;
                c = json[(*pos)++];
                if (c == 'u') {
                    for (int i = 0; i < 4; ++i)
                        if (*pos >= length || !strchr("0123456789abcdefABCDEF", json[(*pos)++]))
                            return false;
                }
            }
        }
        return false;
    }
    if (c == '{' || c == '[') {
        bool object = c == '{';
        char close = object ? '}' : ']';
        whitespace(json, length, pos);
        if (*pos < length && json[*pos] == close) {
            ++*pos;
            return true;
        }
        for (;;) {
            whitespace(json, length, pos);
            if (object) {
                if (*pos >= length || json[*pos] != '"' || !value(json, length, pos, depth + 1))
                    return false;
                whitespace(json, length, pos);
                if (*pos >= length || json[(*pos)++] != ':')
                    return false;
            }
            if (!value(json, length, pos, depth + 1))
                return false;
            whitespace(json, length, pos);
            if (*pos >= length)
                return false;
            c = json[(*pos)++];
            if (c == close)
                return true;
            if (c != ',')
                return false;
        }
    }
    --*pos;
    size_t start = *pos;
    while (*pos < length && !strchr(" \t\r\n,}]", json[*pos]))
        ++*pos;
    size_t n = *pos - start;
    if ((n == 4 && !memcmp(json + start, "true", n)) || (n == 4 && !memcmp(json + start, "null", n))
        || (n == 5 && !memcmp(json + start, "false", n)))
        return true;
    // Unknown numerical fields must still have JSON number syntax.
    size_t i = start;
    if (i < *pos && json[i] == '-')
        ++i;
    if (i >= *pos)
        return false;
    if (json[i] == '0')
        ++i;
    else {
        if (json[i] < '1' || json[i] > '9')
            return false;
        while (i < *pos && json[i] >= '0' && json[i] <= '9')
            ++i;
    }
    if (i < *pos && json[i] == '.') {
        ++i;
        size_t first = i;
        while (i < *pos && json[i] >= '0' && json[i] <= '9')
            ++i;
        if (i == first)
            return false;
    }
    if (i < *pos && (json[i] == 'e' || json[i] == 'E')) {
        ++i;
        if (i < *pos && (json[i] == '+' || json[i] == '-'))
            ++i;
        size_t first = i;
        while (i < *pos && json[i] >= '0' && json[i] <= '9')
            ++i;
        if (i == first)
            return false;
    }
    return i == *pos;
}

int boat_json_parse(const char *json, size_t length, jsmntok_t *tokens, int capacity)
{
    size_t pos = 0;
    if (!json || memchr(json, 0, length) || !value(json, length, &pos, 0))
        return -1;
    whitespace(json, length, &pos);
    if (pos != length)
        return -1;
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, length, tokens, (unsigned)capacity);
    if (count < 1 || tokens[0].type != JSMN_OBJECT || boat_json_next(tokens, count, 0) != count)
        return -1;
    return count;
}
