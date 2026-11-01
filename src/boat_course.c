#include "boat_course.h"

#include <math.h>
#include <string.h>

static void fold(uint64_t *hash, const void *bytes, size_t size)
{
    const unsigned char *p = bytes;
    for (size_t i = 0; i < size; ++i) {
        *hash ^= p[i];
        *hash *= 1099511628211ull;
    }
}

static void number(uint64_t *hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    unsigned char bytes[4] = { (unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24) };
    fold(hash, bytes, sizeof(bytes));
}

uint64_t boat_course_digest(const BoatCourseDefinition *definition)
{
    if (!definition)
        return 0;
    uint64_t hash = 14695981039346656037ull;
    number(&hash, definition->start.x);
    number(&hash, definition->start.y);
    number(&hash, definition->heading);
    for (int i = 0; i < BOAT_COURSE_GATES; ++i) {
        number(&hash, definition->gates[i].centre.x);
        number(&hash, definition->gates[i].centre.y);
        number(&hash, definition->gates[i].half_width);
    }
    return hash;
}

void boat_course_step(BoatCourse *course, Vector2 previous, Vector2 position, double dt)
{
    for (int i = 0; i < BOAT_COURSE_GATES; ++i)
        course->crossing[i] = -1;
    if (!course->enabled || !course->definition || course->next_gate == BOAT_COURSE_GATES)
        return;
    const BoatCourseGate *gates = course->definition->gates;
    // Swept crossings avoid missed gates at speed. Fractions account for the portions of the
    // start/finish ticks actually spent on course, independent of the frame rate.
    double running_from = course->next_gate > 0 ? 0 : 1;
    double running_until = 1;
    while (course->next_gate < BOAT_COURSE_GATES) {
        BoatCourseGate gate = gates[course->next_gate];
        if (previous.y <= gate.centre.y || position.y > gate.centre.y)
            break;
        double fraction = (previous.y - gate.centre.y) / (previous.y - position.y);
        float x = previous.x + (position.x - previous.x) * (float)fraction;
        if (fabsf(x - gate.centre.x) > gate.half_width)
            break;
        if (course->next_gate == 0)
            running_from = fraction;
        course->crossing[course->next_gate] = fraction;
        course->splits[course->next_gate] = course->elapsed + (fraction - running_from) * dt;
        if (++course->next_gate == BOAT_COURSE_GATES)
            running_until = fraction;
    }
    course->elapsed += fmax(0, running_until - running_from) * dt;
}
