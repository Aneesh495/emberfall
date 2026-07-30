#include "convex_contact.hpp"

#include <limits>

namespace ember::dynamics_detail {
namespace {

struct Segment { Vec3 a,b; };
struct Plane { Vec3 normal,point; };
struct Closest {
    Vec3 a{},b{};
    float distance_squared{std::numeric_limits<float>::infinity()};

    void consider(Vec3 first,Vec3 second) {
        const auto difference = second-first;
        const auto squared = dot(difference,difference);
        if (squared<distance_squared) {
            a = first;
            b = second;
            distance_squared = squared;
        }
    }
};

bool polygon(const RigidBody& body) { return body.shape==Shape::box || body.shape==Shape::convex; }

Segment capsule_axis(const RigidBody& body) {
    const auto axis = rotate(body.orientation,{0,body.shape==Shape::capsule ? body.half_segment : 0,0});
    return {body.position-axis,body.position+axis};
}

const ConvexHull& box_topology() {
    static const ConvexHull topology(
        {{-1,-1,-1},{1,-1,-1},{-1,1,-1},{1,1,-1},{-1,-1,1},{1,-1,1},{-1,1,1},{1,1,1}},
        {{0,1,2},{1,3,2},{4,6,5},{5,6,7},{0,4,1},{1,4,5},
         {2,3,6},{3,7,6},{0,2,4},{2,6,4},{1,5,3},{3,5,7}});
    return topology;
}

struct Polytope {
    const ConvexHull& source;
    std::array<Vec3,ConvexHull::vertex_limit> positions;
    std::array<Plane,ConvexHull::face_limit> planes;

    explicit Polytope(const RigidBody& body)
        : source(body.shape==Shape::box ? box_topology() : *body.hull) {
        for (std::size_t i=0; i<source.vertices().size(); ++i) {
            auto local = source.vertices()[i];
            if (body.shape==Shape::box) local = {local.x*body.half.x,local.y*body.half.y,local.z*body.half.z};
            positions[i] = body.position+rotate(body.orientation,local);
        }
        for (std::size_t i=0; i<source.faces().size(); ++i) {
            const auto [a,b,c] = source.faces()[i];
            planes[i] = {unit(cross(positions[b]-positions[a],positions[c]-positions[a])),positions[a]};
        }
    }

    std::span<const Vec3> vertices() const { return std::span(positions).first(source.vertices().size()); }
    std::span<const Plane> faces() const { return std::span(planes).first(source.faces().size()); }
    Segment segment(ConvexHull::Edge edge) const { return {positions[edge[0]],positions[edge[1]]}; }
};

float distance(const Plane& plane,Vec3 point) { return dot(plane.normal,point-plane.point); }

std::optional<Segment> clip(Segment segment,const Polytope& hull) {
    float enter = 0, exit = 1;
    for (const auto& plane : hull.faces()) {
        const auto a = distance(plane,segment.a);
        const auto b = distance(plane,segment.b);
        if (a<=0 && b<=0) continue;
        if (a>0 && b>0) return {};
        const auto t = a/(a-b);
        if (a>0) enter = std::max(enter,t);
        else exit = std::min(exit,t);
        if (enter>exit) return {};
    }
    return Segment{lerp(segment.a,segment.b,enter),lerp(segment.a,segment.b,exit)};
}

std::pair<float,float> interval(std::span<const Vec3> vertices,Vec3 axis,Vec3 origin) {
    float low = std::numeric_limits<float>::infinity(), high = -low;
    for (auto p : vertices) {
        const auto projected = dot(p-origin,axis);
        low = std::min(low,projected);
        high = std::max(high,projected);
    }
    return {low,high};
}

std::optional<Contact> poly_pair(const RigidBody& first,const RigidBody& second) {
    const Polytope a(first), b(second);
    float depth = std::numeric_limits<float>::infinity();
    Vec3 normal;
    const auto test = [&](Vec3 candidate) {
        const auto size_squared = dot(candidate,candidate);
        if (size_squared<1e-12f) return true;
        const auto n = candidate/std::sqrt(size_squared);
        const auto [amin,amax] = interval(a.vertices(),n,first.position);
        const auto [bmin,bmax] = interval(b.vertices(),n,first.position);
        const auto forward = amax-bmin, backward = bmax-amin;
        if (forward<=0 || backward<=0) return false;
        const auto overlap = std::min(forward,backward);
        if (overlap<depth) {
            depth = overlap;
            normal = forward<backward ? n : -n;
        }
        return true;
    };
    for (const auto& face : a.faces()) if (!test(face.normal)) return {};
    for (const auto& face : b.faces()) if (!test(face.normal)) return {};
    for (const auto edge_a : a.source.edges()) {
        const auto ea = a.segment(edge_a);
        const auto along = unit(ea.b-ea.a);
        for (const auto edge_b : b.source.edges()) {
            const auto eb = b.segment(edge_b);
            if (!test(cross(along,unit(eb.b-eb.a)))) return {};
        }
    }

    Vec3 sample{};
    unsigned count = 0;
    const auto gather = [&](const Polytope& source,const Polytope& target) {
        for (const auto edge : source.source.edges()) {
            if (const auto overlap = clip(source.segment(edge),target)) {
                sample += overlap->a+overlap->b;
                count += 2;
            }
        }
    };
    gather(a,b);
    gather(b,a);
    const auto point = count ? sample/static_cast<float>(count) : (first.position+second.position)*0.5f;
    return Contact{first.id,second.id,normal,depth,first.trigger||second.trigger,point};
}

Vec3 nearest(Segment edge,Vec3 point) {
    const auto direction = edge.b-edge.a;
    const auto square = dot(direction,direction);
    if (square<1e-12f) return edge.a;
    return edge.a+direction*std::clamp(dot(point-edge.a,direction)/square,0.0f,1.0f);
}

void segment_pair(Segment a,Segment b,Closest& result) {
    result.consider(a.a,nearest(b,a.a));
    result.consider(a.b,nearest(b,a.b));
    result.consider(nearest(a,b.a),b.a);
    result.consider(nearest(a,b.b),b.b);

    const auto u = a.b-a.a, v = b.b-b.a, delta = a.a-b.a;
    const auto aa = dot(u,u), ab = dot(u,v), bb = dot(v,v);
    const auto determinant = aa*bb-ab*ab;
    if (determinant<=aa*bb*1e-7f || determinant<=1e-16f) return;
    const auto ud = dot(u,delta), vd = dot(v,delta);
    const auto s = (ab*vd-bb*ud)/determinant;
    const auto t = (aa*vd-ab*ud)/determinant;
    if (s>=0 && s<=1 && t>=0 && t<=1) result.consider(a.a+u*s,b.a+v*t);
}

bool in_triangle(Vec3 p,const std::array<Segment,3>& edges,Vec3 n) {
    return std::ranges::all_of(edges,[&](Segment edge) {
        return dot(cross(edge.b-edge.a,p-edge.a),n)>=-1e-6f;
    });
}

void segment_triangle(Segment segment,const std::array<Segment,3>& edges,Plane plane,Closest& result) {
    for (const auto edge : edges) segment_pair(segment,edge,result);
    const auto first = distance(plane,segment.a), second = distance(plane,segment.b);
    const auto projected_first = segment.a-plane.normal*first;
    const auto projected_second = segment.b-plane.normal*second;
    if (in_triangle(projected_first,edges,plane.normal)) result.consider(segment.a,projected_first);
    if (in_triangle(projected_second,edges,plane.normal)) result.consider(segment.b,projected_second);
    if ((first<0 && second>0) || (first>0 && second<0)) {
        const auto crossing = lerp(segment.a,segment.b,first/(first-second));
        if (in_triangle(crossing,edges,plane.normal)) result.consider(crossing,crossing);
    }
}

std::optional<Contact> capsule_poly(const RigidBody& round,const RigidBody& solid) {
    const Polytope hull(solid);
    const auto segment = capsule_axis(round);
    if (clip(segment,hull)) {
        float depth = std::numeric_limits<float>::infinity();
        Vec3 normal, point;
        for (const auto& plane : hull.faces()) {
            const auto a = distance(plane,segment.a), b = distance(plane,segment.b);
            const auto amount = round.radius-std::min(a,b);
            if (amount<depth) {
                depth = amount;
                normal = -plane.normal;
                point = (a<b ? segment.a : segment.b) + plane.normal*(depth*0.5f-round.radius);
            }
        }
        return Contact{round.id,solid.id,normal,depth,round.trigger||solid.trigger,point};
    }
    Closest closest;
    for (std::size_t i=0; i<hull.source.faces().size(); ++i) {
        const auto [a,b,c] = hull.source.faces()[i];
        const std::array edges{hull.segment({a,b}),hull.segment({b,c}),hull.segment({c,a})};
        segment_triangle(segment,edges,hull.planes[i],closest);
    }
    if (closest.distance_squared>=round.radius*round.radius) return {};
    const auto length = std::sqrt(closest.distance_squared);
    const auto normal = length>1e-7f ? (closest.b-closest.a)/length : unit(solid.position-round.position);
    if (normal==Vec3{}) return {};
    return Contact{round.id,solid.id,normal,round.radius-length,round.trigger||solid.trigger,
                   (closest.a+normal*round.radius+closest.b)*0.5f};
}

}

bool needs_convex_path(const RigidBody& body) {
    return body.shape==Shape::convex ||
        (body.shape!=Shape::sphere && (body.orientation.x!=0 || body.orientation.y!=0 || body.orientation.z!=0));
}

Box rotated_bounds(const RigidBody& body) {
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    if (body.shape==Shape::convex) {
        if (!body.hull) return {body.position,body.position};
        Box box{{infinity,infinity,infinity},{-infinity,-infinity,-infinity}};
        for (auto point : body.hull->vertices()) {
            const auto p = rotate(body.orientation,point)+body.position;
            box.minimum = {std::min(box.minimum.x,p.x),std::min(box.minimum.y,p.y),std::min(box.minimum.z,p.z)};
            box.maximum = {std::max(box.maximum.x,p.x),std::max(box.maximum.y,p.y),std::max(box.maximum.z,p.z)};
        }
        return box;
    }
    if (body.shape==Shape::box) return transformed_box(transform_matrix(body.position,body.orientation,body.half*2));
    const auto segment = capsule_axis(body);
    const Vec3 radius{body.radius,body.radius,body.radius};
    return Box{{std::min(segment.a.x,segment.b.x),std::min(segment.a.y,segment.b.y),std::min(segment.a.z,segment.b.z)},
               {std::max(segment.a.x,segment.b.x),std::max(segment.a.y,segment.b.y),std::max(segment.a.z,segment.b.z)}}.expanded(radius);
}

std::optional<Contact> convex_contact(const RigidBody& a,const RigidBody& b) {
    if ((a.shape==Shape::convex && !a.hull) || (b.shape==Shape::convex && !b.hull)) return {};
    if (polygon(a) && polygon(b)) return poly_pair(a,b);
    if (polygon(b)) return capsule_poly(a,b);
    if (polygon(a)) {
        auto contact = capsule_poly(b,a);
        if (contact) {
            std::swap(contact->first,contact->second);
            contact->normal = -contact->normal;
        }
        return contact;
    }

    Closest closest;
    segment_pair(capsule_axis(a),capsule_axis(b),closest);
    const auto radius = a.radius+b.radius;
    if (closest.distance_squared>=radius*radius) return {};
    const auto length = std::sqrt(closest.distance_squared);
    Vec3 normal = length>1e-7f ? (closest.b-closest.a)/length : unit(b.position-a.position);
    if (normal==Vec3{}) normal = {1,0,0};
    const auto point = (closest.a+normal*a.radius+closest.b-normal*b.radius)*0.5f;
    return Contact{a.id,b.id,normal,radius-length,a.trigger||b.trigger,point};
}

}
