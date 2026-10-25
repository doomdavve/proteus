#ifndef PROTEUS_RENDERER3D_H
#define PROTEUS_RENDERER3D_H

#include "geometry3d.h"

#include <stdbool.h>
#include <stddef.h>

// The camera looks toward target, with +Z forward in view space.
typedef struct {
    Vector3 position;
    Vector3 target;
    Vector3 up; // Must not be parallel to the viewing direction.
    float vertical_fov; // Radians, strictly between zero and PI.
    float near_plane;
    float far_plane;
} View3D;

// Indexed, repeating nearest-neighbour texture with 16 albedo entries and 16 light levels.
// Palette layout is [light level * 16 + texel]. All texels must be in [0, 15].
// Coordinates are world-aligned planar projections, at texels_per_unit density.
typedef struct {
    const unsigned char *texels;
    const Color *palette; // 256 entries, darkest to brightest ramps.
    int width, height;
    float texels_per_unit;
    bool emissive;
} Material3D;

typedef struct {
    Vector3 position;
    float radius;
    float intensity;
} PointLight3D;

// Work submitted since the last successful begin. Counting never changes the drawn pixels.
typedef struct {
    size_t meshes; // Mesh and box submissions.
    size_t triangles; // Submitted triangles.
    size_t front_triangles; // Triangles facing the camera, before frustum clipping.
    size_t rasterized; // Clipped triangles handed to the rasterizer.
} Renderer3DStats;

// Zero-initialize before first use. Owns only its depth buffer; pixels belong to the caller.
typedef struct {
    Color *pixels;
    float *depth;
    size_t capacity;
    int width;
    int height;
    View3D camera;
    Vector3 right;
    Vector3 up;
    Vector3 forward;
    Vector3 light_direction; // World-space direction toward the light, normalized by begin.
    float focal_length;
    float ghost_opacity; // Zero normally; 0..1 draws cyan screen-door coverage.
    PointLight3D point_light; // Disabled by begin; set before drawing the frame.
    Renderer3DStats stats; // Reset by begin.
} Renderer3D;

// Starts a frame and clears depth, preserving the caller's background pixels.
// Returns false on invalid camera/target size or allocation failure; do not draw that frame.
bool renderer3d_begin(Renderer3D *renderer, Color *pixels, int width, int height, View3D camera,
    Vector3 light_direction);
void renderer3d_draw_mesh(Renderer3D *renderer, const Mesh3D *mesh, Transform3D transform);
// Unlit vertex colors, with the same clipping, culling and depth as ordinary meshes.
void renderer3d_draw_emissive_mesh(Renderer3D *renderer, const Mesh3D *mesh, Transform3D transform);
void renderer3d_destroy(Renderer3D *renderer);

// Shared unit box centered at the origin, spanning [-0.5, 0.5] on each axis.
void renderer3d_draw_box(Renderer3D *renderer, Transform3D transform, Color color);

void renderer3d_draw_textured_box(
    Renderer3D *renderer, Transform3D transform, const Material3D *material);

#endif
