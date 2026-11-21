-- The resident's racetrack and the visitor's approach, as authored open paths. A path's berth end
-- is checked against the berth's pose (see scene.marina.berths "home" and "slip" in marina).
local z = 231 -- The berths' z; every piece below is laid out from it.
local east = 1.5707963267948966
local pi = 3.141592653589793

return {
  resident = "racetrack",
  visitor = "visit",
  paths = {
    -- Leaves "home" at the berth pose, loops 12 m across and returns to the same pose.
    { id = "racetrack", recipe = "runabout", route = "open", pieces = {
      {kind="line",x0=-16,z0=z,x1=-6,z1=z,ticks=480,dwell=0,max_speed=5,max_acceleration=5},
      {kind="arc",x=-6,z=z-6,radius=6,start_angle=east,sweep=-pi,ticks=720,dwell=0,max_speed=5,max_acceleration=7},
      {kind="line",x0=-6,z0=z-12,x1=-16,z1=z-12,ticks=480,dwell=0,max_speed=5,max_acceleration=7},
      {kind="arc",x=-16,z=z-6,radius=6,start_angle=-east,sweep=-pi,ticks=720,dwell=0,max_speed=5,max_acceleration=7}
    } },
    -- Arrives from the north, docks at "slip" (piece 3 ends at the berth, 240 ticks of dwell) and leaves.
    { id = "visit", recipe = "runabout", route = "open", pieces = {
      {kind="line",x0=0,z0=z-25,x1=0,z1=z-10,ticks=600,dwell=0,max_speed=5,max_acceleration=5},
      {kind="arc",x=10,z=z-10,radius=10,start_angle=pi,sweep=-east,ticks=600,dwell=0,max_speed=5,max_acceleration=7},
      {kind="line",x0=10,z0=z,x1=12,z1=z,ticks=240,dwell=240,max_speed=5,max_acceleration=7},
      {kind="arc",x=12,z=z-10,radius=10,start_angle=east,sweep=-east,ticks=600,dwell=0,max_speed=5,max_acceleration=7}
    } }
  }
}
