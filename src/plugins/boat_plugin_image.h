#ifndef PROTEUS_BOAT_PLUGIN_IMAGE_H
#define PROTEUS_BOAT_PLUGIN_IMAGE_H

// The source image (Marina M5, D1/D2): a package's entry and its declared modules packed into one
// length-prefixed byte string that travels in the existing (source, source_size) buffers, so the
// eval request, revision, wire and record carriers keep their sizes. Data only: no files, no Lua.
//
// Rule: a package that declares no modules keeps its source as the raw entry bytes, exactly as
// before. boat_plugin_read produces an image only when the manifest declares modules. An image
// starts with the magic below (a NUL byte, which Lua text never contains), so the two forms are
// told apart by boat_plugin_image_is; an entry whose text starts with that magic is refused.
//
// Layout, all integers little-endian:
//   u8[5] magic "\0PMI" + version 1   ("\0", 'P', 'M', 'I', 0x01)
//   u8    file count N, 1..BOAT_PLUGIN_MODULES_MAX + 1; file 0 is the entry
//   N x { u8 name length (1..32), name bytes, u32 byte length, bytes }
// The entry is named by its file name ("main.lua"), each module by its dotted name
// ("marina.berths"). The whole image, header included, is at most BOAT_PLUGIN_SOURCE_MAX bytes;
// there are no trailing bytes, no duplicate names and no names outside the module grammar.
#include <stdbool.h>
#include <stddef.h>

#define BOAT_PLUGIN_MODULES_MAX 8
#define BOAT_PLUGIN_MODULE_NAME_MAX 32 // Bytes, without the terminator.
#define BOAT_PLUGIN_MODULE_SEGMENTS_MAX 3
#define BOAT_PLUGIN_IMAGE_VERSION 1
#define BOAT_PLUGIN_IMAGE_COUNT_AT 5 // Offset of the file count, after the magic.

// A named view of bytes owned by the caller (or, after unpack, by the image).
typedef struct {
    const char *name;
    size_t name_size;
    const char *data;
    size_t size;
} BoatPluginImageFile;

// A module name: 1-3 dot-separated segments of [a-z][a-z0-9_]*, at most 32 bytes in all.
bool boat_plugin_module_name_valid(const char *name);
// The package-relative file of a valid module name: "a" is "modules/a.lua", "a.b" is
// "modules/a/b.lua". False when NAME is invalid or CAPACITY is too small.
bool boat_plugin_module_path(const char *name, char *out, size_t capacity);

// Whether SOURCE is image-form rather than raw entry text.
bool boat_plugin_image_is(const char *source, size_t size);
// The bytes pack would write for FILES (entry first), or 0 when FILES is not packable or the
// image would exceed BOAT_PLUGIN_SOURCE_MAX.
size_t boat_plugin_image_size(const BoatPluginImageFile *files, unsigned count);
// Pack FILES into OUT; *WRITTEN is the image's size. False leaves OUT's contents unspecified.
bool boat_plugin_image_pack(
    const BoatPluginImageFile *files, unsigned count, char *out, size_t capacity, size_t *written);
// Check IMAGE completely and fill FILES (capacity BOAT_PLUGIN_MODULES_MAX + 1) with views into
// it. Rejects anything pack could not have written.
bool boat_plugin_image_unpack(
    const char *image, size_t size, BoatPluginImageFile *files, unsigned *count);
// The file named NAME among COUNT unpacked files, or NULL.
const BoatPluginImageFile *boat_plugin_image_find(
    const BoatPluginImageFile *files, unsigned count, const char *name);

#endif
