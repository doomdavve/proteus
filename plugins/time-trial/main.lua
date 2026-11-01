-- The standard S course. The digest matches BOAT_COURSE_STANDARD_DIGEST, so existing personal
-- bests keep their established file name.
return {
  course = {
    start = { x = 0, z = 50, heading = 0 },
    gates = {
      { x = 0, z = 35, half_width = 12 },
      { x = 22, z = 7, half_width = 10 },
      { x = -22, z = -23, half_width = 10 },
      { x = 0, z = -53, half_width = 12 },
    },
  },
}
