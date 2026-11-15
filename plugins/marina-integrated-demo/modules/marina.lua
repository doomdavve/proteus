-- The larger marina: the shared Eriksberg marina plus a guest dock for the two serviced vessels.
-- The shared module's table is cached for the whole evaluation, so copy it before changing it.
local function copy(value)
    if type(value) ~= "table" then
        return value
    end
    local result = {}
    for key, item in pairs(value) do
        result[key] = copy(item)
    end
    return result
end

local scene = copy(require("eriksberg_marina"))
local marina = scene.marina

-- A slip has a solid stern, and a service path is forward-only and must start and end at its berth
-- pose, so the guest dock is two side-to floating platforms north of the passing craft's loop
-- (z 184..198), where there is open water. Each berth is empty until a vessel's service uses it.
local east = 1.5707963267948966
table.insert(marina.berths, { id = "home", recipe = "runabout", mooring = "none",
    x = -16, z = 231, heading = east, empty = true })
table.insert(marina.berths, { id = "slip", recipe = "runabout", mooring = "none",
    x = 12, z = 231, heading = east, empty = true })
table.insert(marina.modules, { id = "tender_dock", kind = "platform", x = -16, z = 239,
    heading = 0, length = 6, width = 6, deck_height = .74 })
table.insert(marina.modules, { id = "guest_dock", kind = "platform", x = 12, z = 239,
    heading = 0, length = 6, width = 6, deck_height = .74 })

return scene
