#ifndef PROTEUS_BOAT_FLAG_H
#define PROTEUS_BOAT_FLAG_H

#include "renderer3d.h"

#define BOAT_FLAG_COLS 9
#define BOAT_FLAG_ROWS 6
#define BOAT_FLAG_POINTS (BOAT_FLAG_COLS * BOAT_FLAG_ROWS)
#define BOAT_FLAG_WIDTH .64f
#define BOAT_FLAG_HEIGHT .40f
#define BOAT_FLAG_STAFF_BASE_Y .82f
#define BOAT_FLAG_STAFF_HEIGHT .75f
#define BOAT_FLAG_HOIST_TOP (BOAT_FLAG_STAFF_BASE_Y + BOAT_FLAG_STAFF_HEIGHT - .04f)
#define BOAT_FLAG_STEP (1.0f / 120.0f)

typedef struct {
    Vector3 points[BOAT_FLAG_POINTS]; // World coordinates; snapshot shared by all render passes.
} BoatFlagSurface;

typedef struct {
    BoatFlagSurface surface;
    Vector3 previous[BOAT_FLAG_POINTS];
    float flutter_phase; // Integrated shedding phase; depends on apparent air speed.
    bool initialized;
} BoatFlag;

// Gentle world-space breeze in renderer coordinates (metres/second), independent of water flow.
// Time is double world seconds; each periodic component is reduced before narrowing.
Vector3 boat_flag_wind(Vector3 position, double time);
Vector3 boat_flag_attachment(Transform3D hull, int row);
// Fixed 120 Hz cloth integration. Hull scale is one; attachments are in physical metres.
void boat_flag_reset(BoatFlag *flag, Transform3D hull, Vector3 velocity, double time);
void boat_flag_step(BoatFlag *flag, Transform3D hull, Vector3 velocity, double time);
// Render-only subdivisions give Norway's blue cross its white border without adding
// simulation particles or changing any cloth forces. Every skin uses the same topology.
#define BOAT_FLAG_MESH_COLS (BOAT_FLAG_COLS + 2)
#define BOAT_FLAG_MESH_ROWS (BOAT_FLAG_ROWS + 2)
#define BOAT_FLAG_MESH_POINTS (BOAT_FLAG_MESH_COLS * BOAT_FLAG_MESH_ROWS)
#define BOAT_FLAG_MESH_TRIANGLES ((BOAT_FLAG_MESH_COLS - 1) * (BOAT_FLAG_MESH_ROWS - 1) * 4)
typedef struct {
    Vector3 vertices[BOAT_FLAG_MESH_POINTS];
    Triangle3D triangles[BOAT_FLAG_MESH_TRIANGLES];
} BoatFlagMesh;

// Caller-owned render scratch: safe for simultaneous players and ghosts. Unknown skins
// fall back to Sweden. The source cloth snapshot is never changed.
Mesh3D boat_flag_skin_mesh(const BoatFlagSurface *surface, unsigned int skin, BoatFlagMesh *mesh);
// Legacy two-sided Swedish cross geometry; returned view borrows the snapshot and static topology.
Mesh3D boat_flag_mesh(const BoatFlagSurface *surface);

#endif
