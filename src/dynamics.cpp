#include "emberfall/dynamics.hpp"

#include <stdexcept>

namespace ember {
namespace {

constexpr std::size_t body_limit = 4096;
constexpr std::size_t contact_limit = 32768;

Vec3 extent(const RigidBody& body) {
    if (body.shape == Shape::box) return body.half;
    return {body.radius,body.radius+body.half_segment,body.radius};
}

Box bounds(const RigidBody& body) {
    const auto half = extent(body);
    return {body.position-half,body.position+half};
}

bool dynamic(const RigidBody& body) { return !body.kinematic && body.inverse_mass > 0; }
float mass(const RigidBody& body) { return dynamic(body) && !body.sleeping ? body.inverse_mass : 0; }

std::optional<Contact> boxes(const RigidBody& a,const RigidBody& b) {
    const auto delta = b.position-a.position;
    const auto half = a.half+b.half;
    const Vec3 depth{half.x-std::abs(delta.x),half.y-std::abs(delta.y),half.z-std::abs(delta.z)};
    if (std::min({depth.x,depth.y,depth.z}) <= 0) return {};
    if (depth.y <= depth.x && depth.y <= depth.z) {
        return Contact{a.id,b.id,{0,std::copysign(1.0f,delta.y),0},depth.y,a.trigger || b.trigger};
    }
    if (depth.x <= depth.z) {
        return Contact{a.id,b.id,{std::copysign(1.0f,delta.x),0,0},depth.x,a.trigger || b.trigger};
    }
    return Contact{a.id,b.id,{0,0,std::copysign(1.0f,delta.z)},depth.z,a.trigger || b.trigger};
}

std::pair<float,float> nearest_intervals(float amin,float amax,float bmin,float bmax) {
    if (amax < bmin) return {amax,bmin};
    if (bmax < amin) return {amin,bmax};
    const float overlap = (std::max(amin,bmin)+std::min(amax,bmax))*0.5f;
    return {overlap,overlap};
}

std::optional<Contact> round_pair(const RigidBody& a,const RigidBody& b) {
    const auto [ay,by] = nearest_intervals(a.position.y-a.half_segment,a.position.y+a.half_segment,
                                            b.position.y-b.half_segment,b.position.y+b.half_segment);
    const auto delta = Vec3{b.position.x-a.position.x,by-ay,b.position.z-a.position.z};
    const auto length2 = dot(delta,delta);
    const float sum = a.radius+b.radius;
    if (length2 >= sum*sum) return {};
    const auto distance = std::sqrt(length2);
    const auto normal = distance > 1e-6f ? delta/distance : Vec3{1,0,0};
    return Contact{a.id,b.id,normal,sum-distance,a.trigger || b.trigger};
}

std::optional<Contact> rounded_box(const RigidBody& round,const RigidBody& box) {
    const auto limits = bounds(box);
    const auto [ry,by] = nearest_intervals(round.position.y-round.half_segment,round.position.y+round.half_segment,
                                           limits.minimum.y,limits.maximum.y);
    const auto center = Vec3{round.position.x,ry,round.position.z};
    const auto nearest = Vec3{std::clamp(center.x,limits.minimum.x,limits.maximum.x),by,
                              std::clamp(center.z,limits.minimum.z,limits.maximum.z)};
    const auto delta = nearest-center;
    const auto length2 = dot(delta,delta);
    if (length2 >= round.radius*round.radius) return {};
    if (length2 > 1e-12f) {
        const auto length = std::sqrt(length2);
        return Contact{round.id,box.id,delta/length,round.radius-length,round.trigger || box.trigger};
    }
    // The segment penetrates the box. Compute separating translations using its whole span.
    const std::array distances{center.x-limits.minimum.x,limits.maximum.x-center.x,
        round.position.y+round.half_segment-limits.minimum.y,limits.maximum.y-round.position.y+round.half_segment,
        center.z-limits.minimum.z,limits.maximum.z-center.z};
    constexpr std::array normals{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}};
    const auto nearest_face = std::ranges::min_element(distances);
    const auto axis = static_cast<std::size_t>(nearest_face-distances.begin());
    return Contact{round.id,box.id,normals[axis],round.radius+*nearest_face,round.trigger || box.trigger};
}

void wake(RigidBody& body) {
    body.sleeping = false;
    body.calm_ticks = 0;
}

void validate(const RigidBody& body) {
    const auto dimensions = extent(body);
    if (!body.id || static_cast<unsigned>(body.shape) > static_cast<unsigned>(Shape::capsule) ||
        !finite(body.position) || !finite(body.velocity) || !finite(dimensions) ||
        std::min({dimensions.x,dimensions.y,dimensions.z}) < 0.01f ||
        std::max({dimensions.x,dimensions.y,dimensions.z}) > 100 || !std::isfinite(body.radius) ||
        !std::isfinite(body.half_segment) || body.half_segment < 0 || body.radius <= 0 ||
        !std::isfinite(body.inverse_mass) || body.inverse_mass < 0 || body.inverse_mass > 1000 ||
        !std::isfinite(body.restitution) || body.restitution < 0 || body.restitution > 1 ||
        !std::isfinite(body.friction) || body.friction < 0 || body.friction > 2 || !body.layer) {
        throw std::runtime_error("Rigid body outside supported bounds");
    }
}

}

std::optional<Contact> intersect(const RigidBody& a,const RigidBody& b) {
    if (a.shape == Shape::box && b.shape == Shape::box) return boxes(a,b);
    if (a.shape != Shape::box && b.shape != Shape::box) return round_pair(a,b);
    if (b.shape == Shape::box) return rounded_box(a,b);
    auto contact = rounded_box(b,a);
    if (contact) {
        std::swap(contact->first,contact->second);
        contact->normal = -contact->normal;
    }
    return contact;
}

struct Dynamics::Constraint {
    std::size_t first{}, second{};
    Contact contact;
    float lambda{}, bounce{}, friction{};
    Vec3 tangent;
};

Dynamics::Dynamics(Vec3 gravity) : gravity_(gravity) {
    if (!finite(gravity) || length(gravity)>100) throw std::runtime_error("Invalid gravity");
}
Dynamics::~Dynamics() = default;

std::size_t Dynamics::index(SceneId id) const {
    const auto where = std::ranges::lower_bound(bodies_,id,{},&RigidBody::id);
    if (where == bodies_.end() || where->id != id) throw std::out_of_range("Unknown rigid body");
    return static_cast<std::size_t>(where-bodies_.begin());
}

const RigidBody& Dynamics::body(SceneId id) const { return bodies_[index(id)]; }

void Dynamics::add(RigidBody body) {
    validate(body);
    if (bodies_.size() == body_limit) throw std::length_error("Rigid body limit");
    if (body.shape != Shape::capsule) body.half_segment = 0;
    const auto where = std::ranges::lower_bound(bodies_,body.id,{},&RigidBody::id);
    if (where != bodies_.end() && where->id == body.id) throw std::runtime_error("Duplicate rigid body ID");
    bodies_.insert(where,body);
}

void Dynamics::impulse(SceneId id,Vec3 value) {
    if (!finite(value) || length(value)>10000) throw std::runtime_error("Impulse outside supported range");
    auto& body = bodies_[index(id)];
    if (!dynamic(body)) return;
    wake(body);
    body.velocity += value*body.inverse_mass;
}

PhysicsReport Dynamics::step(float dt) {
    if (!std::isfinite(dt) || dt<=0 || dt>0.03334f) throw std::runtime_error("Unsupported dynamics timestep");
    PhysicsReport report{bodies_.size()};
    obstacles_.clear();
    for (const auto& body : bodies_) {
        if (!body.kinematic && !dynamic(body) && !body.trigger && body.shape == Shape::box &&
            body.layer == 1 && body.mask == 0xffff) obstacles_.push_back({body.id,bounds(body)});
    }
    static_space_.rebuild(obstacles_);
    for (auto& body : bodies_) {
        if (!body.kinematic && (!dynamic(body) || body.sleeping)) continue;
        if (dynamic(body)) body.velocity += gravity_*dt;
        auto delta = body.velocity*dt;
        if (body.continuous && dynamic(body) && !body.trigger && body.layer==1 && (body.mask&1)) {
            const auto hit = static_space_.sweep(body.position,extent(body),delta);
            if (hit.surface) {
                delta = delta*hit.fraction + hit.normal*0.00025f;
                const float approach = dot(body.velocity,hit.normal);
                body.velocity = body.velocity-hit.normal*(std::min(0.0f,approach)*(1+body.restitution));
                ++report.swept;
            }
        }
        body.position += delta;
    }

    boxes_.resize(bodies_.size());
    order_.resize(bodies_.size());
    supported_.assign(bodies_.size(),0);
    for (std::size_t i=0; i<bodies_.size(); ++i) {
        boxes_[i] = bounds(bodies_[i]);
        order_[i] = i;
    }
    std::ranges::sort(order_,[&](auto a, auto b) {
        if (boxes_[a].minimum.x == boxes_[b].minimum.x) return a < b;
        return boxes_[a].minimum.x < boxes_[b].minimum.x;
    });
    previous_.swap(constraints_);
    constraints_.clear();
    contacts_.clear();
    for (std::size_t row=0; row<order_.size(); ++row) {
        const auto i = order_[row];
        for (std::size_t other=row+1; other<order_.size(); ++other) {
            const auto j = order_[other];
            if (boxes_[j].minimum.x >= boxes_[i].maximum.x) break;
            if (!overlaps(boxes_[i],boxes_[j])) continue;
            const auto first = std::min(i,j), second = std::max(i,j);
            auto& a = bodies_[first];
            auto& b = bodies_[second];
            if (!(a.mask & b.layer) || !(b.mask & a.layer) ||
                (!dynamic(a) && !dynamic(b) && !a.trigger && !b.trigger)) continue;
            ++report.candidates;
            auto contact = intersect(a,b);
            if (!contact) continue;
            if (contacts_.size() == contact_limit) throw std::length_error("Dynamics contact budget exceeded");
            contacts_.push_back(*contact);
            if (contact->trigger) continue;
            const auto relative = b.velocity-a.velocity;
            if (dot(relative,relative)>0.15f) {
                if (a.sleeping && (dynamic(b) || b.kinematic)) wake(a);
                if (b.sleeping && (dynamic(a) || a.kinematic)) wake(b);
            }
            const auto approach = dot(relative,contact->normal);
            const float restitution = approach < -0.85f ? -approach*std::min(a.restitution,b.restitution) : 0;
            if (contact->normal.y < -0.6f) supported_[first] = 1;
            if (contact->normal.y > 0.6f) supported_[second] = 1;
            constraints_.push_back({first,second,*contact,0,restitution,std::sqrt(a.friction*b.friction),{}});
        }
    }
    const auto key = [](const auto& c) { return std::pair{c.contact.first,c.contact.second}; };
    std::ranges::sort(constraints_,{},key);
    std::ranges::sort(contacts_,{},[](const auto& c) { return std::pair{c.first,c.second}; });
    for (auto& row : constraints_) {
        const auto old = std::ranges::lower_bound(previous_,key(row),{},key);
        if (old == previous_.end() || key(*old) != key(row) || dot(old->contact.normal,row.contact.normal)<0.97f) continue;
        row.lambda = old->lambda*0.82f;
        row.tangent = old->tangent*0.82f;
        const auto impulse = row.contact.normal*row.lambda+row.tangent;
        bodies_[row.first].velocity += -impulse*mass(bodies_[row.first]);
        bodies_[row.second].velocity += impulse*mass(bodies_[row.second]);
    }
    for (unsigned iteration=0; iteration<10; ++iteration) {
        for (auto& row : constraints_) {
            auto& a = bodies_[row.first];
            auto& b = bodies_[row.second];
            const auto inv_a = mass(a), inv_b = mass(b), total = inv_a+inv_b;
            if (total <= 0) continue;
            const auto n = row.contact.normal;
            const auto relative = b.velocity-a.velocity;
            const auto lambda = std::max(0.0f,row.lambda+(row.bounce-dot(relative,n))/total);
            auto impulse = n*(lambda-row.lambda);
            row.lambda = lambda;
            const auto tangential = relative-n*dot(relative,n);
            auto friction = row.tangent-tangential/total;
            const auto friction_limit = row.friction*lambda;
            const auto size = length(friction);
            if (size > friction_limit && size > 0) friction = friction*(friction_limit/size);
            impulse += friction-row.tangent;
            row.tangent = friction;
            a.velocity += -impulse*inv_a;
            b.velocity += impulse*inv_b;
        }
    }
    for (unsigned iteration=0; iteration<8; ++iteration) {
        for (const auto& row : constraints_) {
            auto& a = bodies_[row.first];
            auto& b = bodies_[row.second];
            const auto inv_a = mass(a), inv_b = mass(b), total = inv_a+inv_b;
            if (total <= 0) continue;
            const auto current = intersect(a,b);
            if (!current) continue;
            const auto correction = current->normal*(std::max(0.0f,current->penetration-0.0006f)*0.52f/total);
            a.position += -correction*inv_a;
            b.position += correction*inv_b;
        }
    }
    for (const auto& row : constraints_) {
        const auto remaining = intersect(bodies_[row.first],bodies_[row.second]);
        if (remaining && remaining->penetration > 0.012f) {
            supported_[row.first] = supported_[row.second] = 0;
        }
    }
    for (std::size_t i=0; i<bodies_.size(); ++i) {
        auto& body = bodies_[i];
        if (!finite(body.position) || !finite(body.velocity)) throw std::runtime_error("Nonfinite dynamics state");
        if (!dynamic(body)) continue;
        if (supported_[i] && dot(body.velocity,body.velocity)<0.00025f) {
            if (body.calm_ticks < 150) ++body.calm_ticks;
            if (body.calm_ticks >= 150) {
                body.sleeping = true;
                body.velocity = {};
            }
        } else wake(body);
        report.sleeping += body.sleeping;
    }
    report.contacts = contacts_.size();
    return report;
}

}
