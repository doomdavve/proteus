#ifndef BOAT_TEST_MODULE_EDIT_H
#define BOAT_TEST_MODULE_EDIT_H

// Validators only (Marina M5 W6): the package FILES with one module's text edited in memory. The
// source image is unpacked, FROM (which must occur exactly once in MODULE) is replaced by TO, and
// the image is packed again in the same file order, so the edit is the bytes a save of that module
// file would produce. OUT receives the manifest and the new image; IN is unchanged.
#include "plugins/boat_plugin_image.h"
#include "plugins/boat_plugin_io.h"

#include <string.h>

static inline bool boat_test_edit_module(const BoatPluginFiles *in, const char *module,
    const char *from, const char *to, BoatPluginFiles *out)
{
    static char text[BOAT_PLUGIN_SOURCE_MAX];
    BoatPluginImageFile files[BOAT_PLUGIN_MODULES_MAX + 1];
    unsigned count;
    if (!boat_plugin_image_is(in->source, in->source_size)
        || !boat_plugin_image_unpack(in->source, in->source_size, files, &count))
        return false;
    const BoatPluginImageFile *file = boat_plugin_image_find(files, count, module);
    if (!file)
        return false;
    size_t from_size = strlen(from), to_size = strlen(to);
    const char *at = NULL;
    for (size_t i = 0; i + from_size <= file->size; ++i)
        if (!memcmp(file->data + i, from, from_size)) {
            if (at)
                return false; // Not unique.
            at = file->data + i;
        }
    if (!at || file->size - from_size + to_size > sizeof(text))
        return false;
    size_t before = (size_t)(at - file->data);
    memcpy(text, file->data, before);
    memcpy(text + before, to, to_size);
    memcpy(text + before + to_size, at + from_size, file->size - before - from_size);
    BoatPluginImageFile edited[BOAT_PLUGIN_MODULES_MAX + 1];
    for (unsigned i = 0; i < count; ++i)
        edited[i] = files[i];
    edited[file - files] = (BoatPluginImageFile) { .name = file->name,
        .name_size = file->name_size,
        .data = text,
        .size = file->size - from_size + to_size };
    static char image[BOAT_PLUGIN_SOURCE_MAX];
    size_t size;
    if (!boat_plugin_image_pack(edited, count, image, sizeof(image), &size))
        return false;
    *out = *in;
    memcpy(out->source, image, size);
    out->source_size = size;
    return true;
}

#endif
