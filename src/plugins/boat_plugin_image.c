#include "plugins/boat_plugin_image.h"

#include "plugins/boat_plugin.h"

#include <stdint.h>
#include <string.h>

static const unsigned char MAGIC[5] = { 0, 'P', 'M', 'I', BOAT_PLUGIN_IMAGE_VERSION };
#define HEADER (sizeof(MAGIC) + 1)
#define FILES_MAX (BOAT_PLUGIN_MODULES_MAX + 1)

static bool name_valid(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > BOAT_PLUGIN_MODULE_NAME_MAX)
        return false;
    unsigned segments = 1;
    bool start = true;
    for (size_t i = 0; i < n; ++i) {
        char c = name[i];
        if (start) {
            if (!(c >= 'a' && c <= 'z'))
                return false;
            start = false;
        } else if (c == '.') {
            if (++segments > BOAT_PLUGIN_MODULE_SEGMENTS_MAX)
                return false;
            start = true;
        } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return !start;
}

bool boat_plugin_module_name_valid(const char *name) { return name_valid(name); }

bool boat_plugin_module_path(const char *name, char *out, size_t capacity)
{
    if (!name_valid(name))
        return false;
    size_t n = strlen(name);
    if (capacity < sizeof("modules/") + n + sizeof(".lua") - 1)
        return false;
    memcpy(out, "modules/", 8);
    for (size_t i = 0; i < n; ++i)
        out[8 + i] = name[i] == '.' ? '/' : name[i];
    memcpy(out + 8 + n, ".lua", 5);
    return true;
}

bool boat_plugin_image_is(const char *source, size_t size)
{
    return size >= sizeof(MAGIC) && !memcmp(source, MAGIC, sizeof(MAGIC));
}

static void put32(char *out, size_t value)
{
    for (int i = 0; i < 4; ++i)
        out[i] = (char)((value >> (8 * i)) & 0xff);
}

static size_t get32(const unsigned char *in)
{
    return (size_t)in[0] | (size_t)in[1] << 8 | (size_t)in[2] << 16 | (size_t)in[3] << 24;
}

size_t boat_plugin_image_size(const BoatPluginImageFile *files, unsigned count)
{
    if (count < 1 || count > FILES_MAX)
        return 0;
    size_t total = HEADER;
    for (unsigned i = 0; i < count; ++i) {
        const BoatPluginImageFile *f = &files[i];
        if (f->name_size < 1 || f->name_size > BOAT_PLUGIN_MODULE_NAME_MAX
            || f->size > BOAT_PLUGIN_SOURCE_MAX)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (files[j].name_size == f->name_size && !memcmp(files[j].name, f->name, f->name_size))
                return 0;
        total += 1 + f->name_size + 4 + f->size;
        if (total > BOAT_PLUGIN_SOURCE_MAX)
            return 0;
    }
    return total;
}

bool boat_plugin_image_pack(
    const BoatPluginImageFile *files, unsigned count, char *out, size_t capacity, size_t *written)
{
    size_t total = boat_plugin_image_size(files, count);
    if (total == 0 || total > capacity)
        return false;
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[sizeof(MAGIC)] = (char)count;
    size_t at = HEADER;
    for (unsigned i = 0; i < count; ++i) {
        out[at++] = (char)files[i].name_size;
        memcpy(out + at, files[i].name, files[i].name_size);
        at += files[i].name_size;
        put32(out + at, files[i].size);
        at += 4;
        if (files[i].size)
            memmove(out + at, files[i].data, files[i].size);
        at += files[i].size;
    }
    *written = at;
    return true;
}

bool boat_plugin_image_unpack(
    const char *image, size_t size, BoatPluginImageFile *files, unsigned *count)
{
    const unsigned char *in = (const unsigned char *)image;
    if (!boat_plugin_image_is(image, size) || size > BOAT_PLUGIN_SOURCE_MAX || size < HEADER)
        return false;
    unsigned n = in[sizeof(MAGIC)];
    if (n < 1 || n > FILES_MAX)
        return false;
    size_t at = HEADER;
    for (unsigned i = 0; i < n; ++i) {
        if (at >= size)
            return false;
        size_t name_size = in[at++];
        if (name_size < 1 || name_size > BOAT_PLUGIN_MODULE_NAME_MAX || size - at < name_size + 4)
            return false;
        const char *name = image + at;
        at += name_size;
        size_t bytes = get32(in + at);
        at += 4;
        if (bytes > size - at)
            return false;
        if (i > 0) { // Modules follow the grammar; file 0 is the entry's file name.
            char text[BOAT_PLUGIN_MODULE_NAME_MAX + 1];
            memcpy(text, name, name_size);
            text[name_size] = 0;
            if (!name_valid(text))
                return false;
        }
        for (unsigned j = 0; j < i; ++j)
            if (files[j].name_size == name_size && !memcmp(files[j].name, name, name_size))
                return false;
        files[i] = (BoatPluginImageFile) { name, name_size, image + at, bytes };
        at += bytes;
    }
    if (at != size)
        return false;
    *count = n;
    return true;
}

const BoatPluginImageFile *boat_plugin_image_find(
    const BoatPluginImageFile *files, unsigned count, const char *name)
{
    size_t n = strlen(name);
    for (unsigned i = 0; i < count; ++i)
        if (files[i].name_size == n && !memcmp(files[i].name, name, n))
            return &files[i];
    return NULL;
}
