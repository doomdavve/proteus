#ifndef PROTEUS_GEOMETRY3D_H
#define PROTEUS_GEOMETRY3D_H

#include "raylib.h"

#include <stddef.h>

// Data-only geometry shared by simulation queries and renderers; no drawing declarations.
// World and model coordinates are Y-up. Mesh triangles wind counterclockwise when viewed
// from outside.
typedef struct {
    unsigned int vertices[3];
    Color color;
} Triangle3D;

typedef struct {
    const Vector3 *vertices;
    const Triangle3D *triangles;
    size_t triangle_count;
} Mesh3D;

typedef struct {
    Vector3 position;
    Vector3 rotation; // Radians: local X, then Y, then Z.
    Vector3 scale; // Positive components; zero or mirrored scales are unsupported.
} Transform3D;

#endif
