#pragma once

#include "emberfall/dynamics.hpp"

namespace ember::dynamics_detail {

bool needs_convex_path(const RigidBody& body);
Box rotated_bounds(const RigidBody& body);
std::optional<Contact> convex_contact(const RigidBody& a,const RigidBody& b);

}
