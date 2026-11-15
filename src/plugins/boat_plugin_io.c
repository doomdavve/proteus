#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // open with O_NOFOLLOW and fstat under strict C17.
#define _DARWIN_C_SOURCE // macOS hides O_NOFOLLOW from strict POSIX.
#endif
#include "plugins/boat_plugin_io.h"

#include "plugins/boat_plugin_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI // raylib's Rectangle, CloseWindow and ShowCursor.
#define NOUSER
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static BoatPluginStatus failed(
    BoatPluginDiagnostic *d, BoatPluginStatus status, const char *file, const char *message)
{
    if (d) {
        *d = (BoatPluginDiagnostic) { .status = status };
        snprintf(d->file, sizeof(d->file), "%s", file);
        snprintf(d->message, sizeof(d->message), "%s", message);
    }
    return status;
}

// Read DIRECTORY/NAME into OUT, at most LIMIT bytes. NAME is a plain file name or a relative path
// of plain components ("modules/marina/berths.lua"). Every component is opened without following a
// symbolic link, each but the last must be a directory, and the file must be a regular file.
static BoatPluginStatus slurp(const char *directory, const char *name, size_t limit, char *out,
    size_t *size, BoatPluginDiagnostic *d)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return failed(d, BOAT_PLUGIN_IO, name, "the plugin directory's path is too long");
#ifdef _WIN32
    // Each component below DIRECTORY: no reparse point, and a directory unless it is the file.
    for (const char *slash = strchr(name, '/');; slash = strchr(slash + 1, '/')) {
        size_t prefix = (size_t)n - strlen(name) + (slash ? (size_t)(slash - name) : strlen(name));
        char part[1024];
        memcpy(part, path, prefix);
        part[prefix] = 0;
        DWORD attributes = GetFileAttributesA(part);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return failed(d, BOAT_PLUGIN_IO, name, "cannot be opened");
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return failed(
                d, BOAT_PLUGIN_IO, name, "must be a regular file, not a link or directory");
        bool last = !slash;
        if (last ? (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                 : (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return failed(
                d, BOAT_PLUGIN_IO, name, "must be a regular file, not a link or directory");
        if (last)
            break;
    }
    FILE *file = fopen(path, "rb");
#else
    char walk[512];
    if (snprintf(walk, sizeof(walk), "%s", name) >= (int)sizeof(walk))
        return failed(d, BOAT_PLUGIN_IO, name, "has a path that is too long");
    int folder = open(directory, O_RDONLY | O_DIRECTORY);
    if (folder < 0)
        return failed(d, BOAT_PLUGIN_IO, name, "cannot be opened");
    int descriptor = -1;
    for (char *part = walk; part;) {
        char *slash = strchr(part, '/');
        if (slash)
            *slash = 0;
        descriptor = slash ? openat(folder, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)
                           : openat(folder, part, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        close(folder);
        if (descriptor < 0)
            return failed(d, BOAT_PLUGIN_IO, name,
                "cannot be opened (missing, or it or a directory above it is a symbolic link)");
        folder = descriptor;
        part = slash ? slash + 1 : NULL;
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        close(descriptor);
        return failed(d, BOAT_PLUGIN_IO, name, "must be a regular file");
    }
    FILE *file = fdopen(descriptor, "rb");
    if (!file)
        close(descriptor);
#endif
    if (!file)
        return failed(d, BOAT_PLUGIN_IO, name, "cannot be opened");
    // One byte beyond the limit tells an oversized file from one that fits exactly.
    size_t total = 0, got;
    char extra;
    while (total < limit && (got = fread(out + total, 1, limit - total, file)) > 0)
        total += got;
    bool over = total == limit && fread(&extra, 1, 1, file) == 1;
    bool error = ferror(file);
    fclose(file);
    if (error)
        return failed(d, BOAT_PLUGIN_IO, name, "could not be read");
    if (over) {
        BoatPluginStatus status = failed(d, BOAT_PLUGIN_LIMIT, name, "");
        if (d)
            snprintf(d->message, sizeof(d->message), "is larger than the %zu-byte limit", limit);
        return status;
    }
    *size = total;
    return BOAT_PLUGIN_OK;
}

static size_t smaller(size_t a, size_t b) { return a < b ? a : b; }

// Rewrite an oversize diagnostic for a module: the entry and every module share one budget.
static void shared_budget(BoatPluginDiagnostic *d, size_t room, size_t limit)
{
    snprintf(d->message, sizeof(d->message),
        "is larger than the %zu bytes left of the %zu-byte source budget (entry, modules and "
        "framing share it)",
        room, limit);
}

BoatPluginStatus boat_plugin_read(const char *directory, const BoatPluginLimits *limits,
    BoatPluginFiles *files, BoatPluginDiagnostic *diagnostic)
{
    if (!limits)
        limits = &boat_plugin_default_limits;
    BoatPluginFiles *read = malloc(sizeof(*read));
    if (!read)
        return failed(diagnostic, BOAT_PLUGIN_IO, "", "not enough memory to read the plugin");
    size_t source_limit = smaller(limits->source_bytes, BOAT_PLUGIN_SOURCE_MAX);
    BoatPluginStatus status
        = slurp(directory, "plugin.json", smaller(limits->manifest_bytes, BOAT_PLUGIN_MANIFEST_MAX),
            read->manifest, &read->manifest_size, diagnostic);
    BoatPluginManifest manifest;
    if (status == BOAT_PLUGIN_OK)
        status = boat_plugin_parse_manifest(
            read->manifest, read->manifest_size, limits, &manifest, diagnostic);
    // The manifest restricts the entry to a plain file name in DIRECTORY.
    if (status == BOAT_PLUGIN_OK)
        status = slurp(
            directory, manifest.entry, source_limit, read->source, &read->source_size, diagnostic);
    if (status == BOAT_PLUGIN_OK && boat_plugin_image_is(read->source, read->source_size))
        status = failed(diagnostic, BOAT_PLUGIN_SYNTAX, manifest.entry, "is not Lua text");
    // Declared modules are packed after the entry into one image (boat_plugin_image.h), built in
    // place: each file is read straight into the image after its framing.
    char *image = NULL;
    size_t image_size = 0;
    if (status == BOAT_PLUGIN_OK && manifest.module_count > 0) {
        image = malloc(source_limit);
        if (!image)
            status = failed(diagnostic, BOAT_PLUGIN_IO, "", "not enough memory to read the plugin");
        else {
            BoatPluginImageFile entry
                = { manifest.entry, strlen(manifest.entry), read->source, read->source_size };
            if (!boat_plugin_image_pack(&entry, 1, image, source_limit, &image_size))
                status = failed(diagnostic, BOAT_PLUGIN_LIMIT, manifest.entry, "");
            if (status != BOAT_PLUGIN_OK && diagnostic)
                shared_budget(diagnostic, source_limit, source_limit);
        }
        for (unsigned i = 0; status == BOAT_PLUGIN_OK && i < manifest.module_count; ++i) {
            char relative[96];
            boat_plugin_module_path(manifest.modules[i], relative, sizeof(relative));
            size_t name_size = strlen(manifest.modules[i]);
            size_t framing = 1 + name_size + 4;
            if (image_size + framing > source_limit) {
                status = failed(diagnostic, BOAT_PLUGIN_LIMIT, relative, "");
                if (diagnostic)
                    shared_budget(diagnostic, 0, source_limit);
                break;
            }
            size_t room = source_limit - image_size - framing;
            size_t got = 0;
            status
                = slurp(directory, relative, room, image + image_size + framing, &got, diagnostic);
            if (status == BOAT_PLUGIN_LIMIT && diagnostic)
                shared_budget(diagnostic, room, source_limit);
            if (status != BOAT_PLUGIN_OK)
                break;
            image[image_size] = (char)name_size;
            memcpy(image + image_size + 1, manifest.modules[i], name_size);
            for (int b = 0; b < 4; ++b)
                image[image_size + 1 + name_size + (size_t)b] = (char)((got >> (8 * b)) & 0xff);
            image_size += framing + got;
        }
        if (status == BOAT_PLUGIN_OK) {
            // pack wrote the entry alone; declare the modules now that they are all framed.
            image[BOAT_PLUGIN_IMAGE_COUNT_AT] = (char)(1 + manifest.module_count);
            BoatPluginImageFile check[BOAT_PLUGIN_MODULES_MAX + 1];
            unsigned count;
            if (!boat_plugin_image_unpack(image, image_size, check, &count)
                || count != 1 + manifest.module_count)
                status = failed(diagnostic, BOAT_PLUGIN_IO, "", "could not be packed");
        }
    }
    if (status == BOAT_PLUGIN_OK) {
        memcpy(files->manifest, read->manifest, read->manifest_size);
        files->manifest_size = read->manifest_size;
        if (image) {
            memcpy(files->source, image, image_size);
            files->source_size = image_size;
        } else {
            memcpy(files->source, read->source, read->source_size);
            files->source_size = read->source_size;
        }
        if (diagnostic)
            *diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK };
    }
    free(image);
    free(read);
    return status;
}

BoatPluginBundle boat_plugin_bundle(const BoatPluginFiles *files)
{
    return (BoatPluginBundle) { files->manifest, files->manifest_size, files->source,
        files->source_size };
}
