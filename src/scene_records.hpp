#pragma once

#include "emberfall/scene.hpp"

#include <iosfwd>

namespace ember::scene_records {

Node read_node(std::istream& input,unsigned line);
void write_node(std::ostream& output,const Node& node);

}
