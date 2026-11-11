#ifndef PROTEUS_BOAT_COURSE_VIEW_H
#define PROTEUS_BOAT_COURSE_VIEW_H

#include "boat_course.h"
#include "renderer3d.h"

typedef void (*BoatCourseBoxDraw)(void *context, Transform3D transform, Color color);
void boat_course_visit(const BoatCourse *course, BoatCourseBoxDraw draw, void *context);
void boat_course_draw(const BoatCourse *course, Renderer3D *renderer);
void boat_course_hud(const BoatCourse *course, Vector2 position, int width);

#endif
