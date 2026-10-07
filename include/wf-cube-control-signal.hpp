#pragma once

#define CUBE_ZOOM_BASE 1.0
#define CUBE_ZOOM_MAX 1.1

enum cube_spin_state
{
    CUBE_SPIN_DISABLED,
    CUBE_SPIN_RUNNING,
    CUBE_SPIN_STOPPING,
};

using namespace wf::animation;
class cube_spin_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t rot{*this};
    timed_transition_t zoom{*this};
    timed_transition_t ease{*this};
};

/* A private signal, copied from wayfire since it is not installed
 *
 * It is used to rotate the cube arbitrarily from other plugins.
 */

/* Rotate cube to given angle and zoom level */
struct cube_control_signal
{
    double angle; // cube rotation in radians
    double zoom; // 1.0 means 100%; increase value to zoom
    double ease; // for cube deformation; range 0.0-1.0
    bool last_frame; // ends cube animation if true
    bool carried_out; // false if cube is disabled
};
