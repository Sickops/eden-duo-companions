// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <string_view>
namespace za {
// Lumiose outdoor plan: measured against the player's in-game map at separate locations.
// Coordinates are in the 640x640 texture, not canvas pixels. Never clamp another area
// or an invalid position into a plausible-looking player marker.
inline bool MapPoint(std::string_view area, float x, float z, float& u, float& v) {
    if(area!="t1"||!std::isfinite(x)||!std::isfinite(z))return false;
    u=6.25f-0.625f*x;v=6.25f-0.625f*z;
    return u>=0&&u<=640&&v>=0&&v<=640;
}
}
