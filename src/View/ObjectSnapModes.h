#pragma once

namespace cad {

struct ObjectSnapModes {
    bool endpoint = false;
    bool midpoint = false;
    bool center = false;
    bool geometricCenter = false;
    bool node = false;
    bool quadrant = false;
    bool intersection = false;
    bool extension = false;
    bool insertion = false;
    bool perpendicular = false;
    bool tangent = false;
    bool nearest = false;
    bool apparentIntersection = false;
    bool parallel = false;
    bool grid = false;

    static ObjectSnapModes Default()
    {
        ObjectSnapModes modes;
        modes.endpoint = true;
        modes.center = true;
        modes.intersection = true;
        modes.extension = true;
        return modes;
    }

    bool AnyEnabled() const
    {
        return endpoint || midpoint || center || geometricCenter || node || quadrant || intersection
            || extension || insertion || perpendicular || tangent || nearest || apparentIntersection
            || parallel || grid;
    }
};

} // namespace cad
