#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#endif
#include "boat_directory.h"
#include "boat_net_enet.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdatomic.h>
#endif
#define JSMN_STATIC
#define JSMN_STRICT
#include "../third_party/jsmn/jsmn.h"

#define DIRECTORY_URL "https://wake.davve.net/servers.json"
enum { TOKENS_MAX = 2048 };

static int next_token(const jsmntok_t *tokens, int count, int index)
{
    int end = tokens[index].end;
    ++index;
    while (index < count && tokens[index].start < end)
        ++index;
    return index;
}

static int member(const char *json, const jsmntok_t *tokens, int count, int object, const char *key)
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
        i = next_token(tokens, count, value);
    }
    return found;
}

static bool read_text(const char *json, const jsmntok_t *t, char *out, size_t capacity)
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

static bool read_int(const char *json, const jsmntok_t *t, int *out)
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
static bool json_value(const char *json, size_t length, size_t *pos, unsigned depth)
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
                if (*pos >= length || json[*pos] != '"'
                    || !json_value(json, length, pos, depth + 1))
                    return false;
                whitespace(json, length, pos);
                if (*pos >= length || json[(*pos)++] != ':')
                    return false;
            }
            if (!json_value(json, length, pos, depth + 1))
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

bool boat_directory_parse(const char *json, size_t length, BoatDirectoryList *out)
{
    if (!json || !out || length > BOAT_DIRECTORY_BODY_MAX || memchr(json, 0, length))
        return false;
    size_t pos = 0;
    if (!json_value(json, length, &pos, 0))
        return false;
    whitespace(json, length, &pos);
    if (pos != length)
        return false;
    jsmntok_t tokens[TOKENS_MAX];
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, length, tokens, TOKENS_MAX);
    if (count < 1 || tokens[0].type != JSMN_OBJECT || next_token(tokens, count, 0) != count)
        return false;
    int version = member(json, tokens, count, 0, "version");
    int servers = member(json, tokens, count, 0, "servers"), v;
    if (version < 0 || servers < 0 || !read_int(json, &tokens[version], &v) || v != 1
        || tokens[servers].type != JSMN_ARRAY || tokens[servers].size > BOAT_DIRECTORY_MAX)
        return false;
    BoatDirectoryList list = { 0 };
    for (int i = servers + 1; i < count && tokens[i].start < tokens[servers].end;
        i = next_token(tokens, count, i)) {
        if (tokens[i].type != JSMN_OBJECT)
            return false;
        int name = member(json, tokens, count, i, "name");
        int desc = member(json, tokens, count, i, "description");
        int native = member(json, tokens, count, i, "native");
        int protocol = member(json, tokens, count, i, "protocol");
        int physics = member(json, tokens, count, i, "physics");
        if (name < 0 || desc < 0 || native == -2 || protocol < 0 || physics < 0)
            return false;
        BoatDirectoryEntry entry = { 0 };
        if (!read_text(json, &tokens[name], entry.name, sizeof(entry.name))
            || !read_text(json, &tokens[desc], entry.description, sizeof(entry.description))
            || !read_int(json, &tokens[protocol], &entry.protocol)
            || !read_int(json, &tokens[physics], &entry.physics))
            return false;
        if (native < 0)
            continue;
        char host[64];
        uint16_t port;
        if (!read_text(json, &tokens[native], entry.address, sizeof(entry.address))
            || !boat_net_parse_endpoint(entry.address, host, sizeof(host), &port)
            || strspn(host, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")
                != strlen(host))
            return false;
        list.entries[list.count++] = entry;
    }
    *out = list;
    return true;
}

typedef struct {
    char data[BOAT_DIRECTORY_BODY_MAX + 1];
    size_t length;
} Body;
static size_t receive(void *data, size_t size, size_t count, void *context)
{
    Body *body = context;
    if (size && count > (BOAT_DIRECTORY_BODY_MAX - body->length) / size)
        return 0;
    size_t bytes = size * count;
    memcpy(body->data + body->length, data, bytes);
    body->length += bytes;
    return bytes;
}
struct BoatDirectory {
    BoatDirectoryList list, result;
    bool running, success, available;
#ifdef _WIN32
    HANDLE thread;
#else
    atomic_bool done;
    pthread_t thread;
#endif
};
static void fetch(BoatDirectory *d)
{
    Body body = { 0 };
    CURL *curl = curl_easy_init();
    long status = 0;
    if (curl) {
        struct curl_slist *headers = curl_slist_append(NULL, "Cache-Control: no-cache");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, DIRECTORY_URL);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "WakeVelocity/1");
        CURLcode code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        d->success = code == CURLE_OK && status == 200
            && boat_directory_parse(body.data, body.length, &d->result);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
#ifndef _WIN32
    atomic_store(&d->done, true);
#endif
}
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID context)
{
    fetch(context);
    return 0;
}
#else
static void *worker(void *context)
{
    fetch(context);
    return NULL;
}
#endif
BoatDirectory *boat_directory_create(void)
{
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return NULL;
    BoatDirectory *d = calloc(1, sizeof(*d));
    if (!d)
        curl_global_cleanup();
    return d;
}
void boat_directory_poll(BoatDirectory *d)
{
    if (!d || !d->running)
        return;
#ifdef _WIN32
    // A signaled thread handle publishes the result and does not block the render loop.
    if (WaitForSingleObject(d->thread, 0) != WAIT_OBJECT_0)
        return;
    CloseHandle(d->thread);
#else
    if (!atomic_load(&d->done))
        return;
    pthread_join(d->thread, NULL);
#endif
    d->running = false;
    d->available = d->success;
    if (d->success)
        d->list = d->result;
}
void boat_directory_refresh(BoatDirectory *d)
{
    if (!d)
        return;
    boat_directory_poll(d);
    if (d->running)
        return;
    d->success = false;
#ifndef _WIN32
    atomic_store(&d->done, false);
#endif
#ifdef _WIN32
    d->thread = CreateThread(NULL, 0, worker, d, 0, NULL);
    d->running = d->thread != NULL;
#else
    d->running = pthread_create(&d->thread, NULL, worker, d) == 0;
#endif
    if (!d->running)
        d->available = false;
}
const BoatDirectoryList *boat_directory_list(const BoatDirectory *d)
{
    static const BoatDirectoryList empty = { 0 };
    return d ? &d->list : &empty;
}
const char *boat_directory_status(const BoatDirectory *d)
{
    return d && d->running    ? "LOADING SERVER LIST..."
        : !d || !d->available ? "LIST UNAVAILABLE; MANUAL ADDRESS STILL WORKS"
        : !d->list.count      ? "NO PUBLIC SERVERS LISTED"
                              : "CHOOSE A SERVER; ENTER TO JOIN";
}
void boat_directory_destroy(BoatDirectory *d)
{
    if (!d)
        return;
    if (d->running) {
#ifdef _WIN32
        WaitForSingleObject(d->thread, INFINITE);
        CloseHandle(d->thread);
#else
        pthread_join(d->thread, NULL);
#endif
    }
    free(d);
    curl_global_cleanup();
}
