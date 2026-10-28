#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L // open with O_NOFOLLOW and fstat under strict C17.
#define _DARWIN_C_SOURCE // macOS hides O_NOFOLLOW from strict POSIX.
#endif
#include "boat_plugin_io.h"

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

// Read DIRECTORY/NAME into OUT, at most LIMIT bytes. The file is opened without following a
// symbolic link and must be a regular file.
static BoatPluginStatus slurp(const char *directory, const char *name, size_t limit, char *out,
    size_t *size, BoatPluginDiagnostic *d)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return failed(d, BOAT_PLUGIN_IO, name, "the plugin directory's path is too long");
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return failed(d, BOAT_PLUGIN_IO, name, "cannot be opened");
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        return failed(d, BOAT_PLUGIN_IO, name, "must be a regular file, not a link or directory");
    FILE *file = fopen(path, "rb");
#else
    int descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0)
        return failed(d, BOAT_PLUGIN_IO, name, "cannot be opened (or is a symbolic link)");
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

BoatPluginStatus boat_plugin_read(const char *directory, const BoatPluginLimits *limits,
    BoatPluginFiles *files, BoatPluginDiagnostic *diagnostic)
{
    if (!limits)
        limits = &boat_plugin_default_limits;
    BoatPluginFiles *read = malloc(sizeof(*read));
    if (!read)
        return failed(diagnostic, BOAT_PLUGIN_IO, "", "not enough memory to read the plugin");
    BoatPluginStatus status
        = slurp(directory, "plugin.json", smaller(limits->manifest_bytes, BOAT_PLUGIN_MANIFEST_MAX),
            read->manifest, &read->manifest_size, diagnostic);
    BoatPluginManifest manifest;
    if (status == BOAT_PLUGIN_OK)
        status = boat_plugin_parse_manifest(
            read->manifest, read->manifest_size, limits, &manifest, diagnostic);
    // The manifest restricts the entry to a plain file name in DIRECTORY.
    if (status == BOAT_PLUGIN_OK)
        status = slurp(directory, manifest.entry,
            smaller(limits->source_bytes, BOAT_PLUGIN_SOURCE_MAX), read->source, &read->source_size,
            diagnostic);
    if (status == BOAT_PLUGIN_OK) {
        memcpy(files->manifest, read->manifest, read->manifest_size);
        files->manifest_size = read->manifest_size;
        memcpy(files->source, read->source, read->source_size);
        files->source_size = read->source_size;
        if (diagnostic)
            *diagnostic = (BoatPluginDiagnostic) { .status = BOAT_PLUGIN_OK };
    }
    free(read);
    return status;
}

BoatPluginBundle boat_plugin_bundle(const BoatPluginFiles *files)
{
    return (BoatPluginBundle) { files->manifest, files->manifest_size, files->source,
        files->source_size };
}
