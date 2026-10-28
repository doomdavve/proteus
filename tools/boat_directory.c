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
#include "boat_json.h"

#define DIRECTORY_URL "https://wake.davve.net/servers.json"
enum { TOKENS_MAX = 2048 };

bool boat_directory_parse(const char *json, size_t length, BoatDirectoryList *out)
{
    if (!json || !out || length > BOAT_DIRECTORY_BODY_MAX || memchr(json, 0, length))
        return false;
    jsmntok_t tokens[TOKENS_MAX];
    int count = boat_json_parse(json, length, tokens, TOKENS_MAX);
    if (count < 0)
        return false;
    int version = boat_json_member(json, tokens, count, 0, "version");
    int servers = boat_json_member(json, tokens, count, 0, "servers"), v;
    if (version < 0 || servers < 0 || !boat_json_int(json, &tokens[version], &v) || v != 1
        || tokens[servers].type != JSMN_ARRAY || tokens[servers].size > BOAT_DIRECTORY_MAX)
        return false;
    BoatDirectoryList list = { 0 };
    for (int i = servers + 1; i < count && tokens[i].start < tokens[servers].end;
        i = boat_json_next(tokens, count, i)) {
        if (tokens[i].type != JSMN_OBJECT)
            return false;
        int name = boat_json_member(json, tokens, count, i, "name");
        int desc = boat_json_member(json, tokens, count, i, "description");
        int native = boat_json_member(json, tokens, count, i, "native");
        int protocol = boat_json_member(json, tokens, count, i, "protocol");
        int physics = boat_json_member(json, tokens, count, i, "physics");
        if (name < 0 || desc < 0 || native == -2 || protocol < 0 || physics < 0)
            return false;
        BoatDirectoryEntry entry = { 0 };
        if (!boat_json_text(json, &tokens[name], entry.name, sizeof(entry.name))
            || !boat_json_text(json, &tokens[desc], entry.description, sizeof(entry.description))
            || !boat_json_int(json, &tokens[protocol], &entry.protocol)
            || !boat_json_int(json, &tokens[physics], &entry.physics))
            return false;
        if (native < 0)
            continue;
        char host[64];
        uint16_t port;
        if (!boat_json_text(json, &tokens[native], entry.address, sizeof(entry.address))
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
