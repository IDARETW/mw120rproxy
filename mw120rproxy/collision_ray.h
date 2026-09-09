#pragma once
#include "collision_file.h"
#include <numeric>
#include <limits>

// Ray queries use the same convex volumes as the custom Havok bodies. The tree
// is immutable after loading; shots allocate no memory and test only nearby hulls.
namespace collisionray {
using Vec = std::array<double, 3>;
inline Vec Sub(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline Vec Cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double Dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
struct Plane {
    Vec normal{};
    double distance = 0;
};
struct Hull {
    Vec mins{}, maxs{};
    std::vector<Plane> planes;
    uint32_t contents = collisionfile::Solid;
};
inline bool BuildHull(const collisionfile::Brush& brush, Hull& result) {
    result = {};
    result.contents = brush.contents;
    for (unsigned k = 0; k < 3; ++k) {
        result.mins[k] = brush.mins[k];
        result.maxs[k] = brush.maxs[k];
    }
    if (brush.vertices.empty()) {
        for (unsigned k = 0; k < 3; ++k) {
            Plane a{}, b{};
            a.normal[k] = 1;
            a.distance = result.maxs[k];
            b.normal[k] = -1;
            b.distance = -result.mins[k];
            result.planes.push_back(a);
            result.planes.push_back(b);
        }
        return true;
    }
    std::vector<Vec> points;
    for (const auto& p : brush.vertices)
        points.push_back({p[0], p[1], p[2]});
    if (points.size() < 4)
        return false;
    // Input hulls can contain almost coincident vertices from the IW3 BSP.
    // Enumerate supporting planes rather than merging those vertices, which
    // would erase thin trim, window frames and collision triangle prisms.
    for (unsigned i = 0; i < points.size(); ++i)
        for (unsigned j = i + 1; j < points.size(); ++j)
            for (unsigned k = j + 1; k < points.size(); ++k) {
                auto n = Cross(Sub(points[j], points[i]), Sub(points[k], points[i]));
                const double length = std::sqrt(Dot(n, n));
                if (length < 1e-10)
                    continue;
                for (auto& v : n)
                    v /= length;
                double distance = Dot(n, points[i]);
                bool positive = false, negative = false;
                for (const auto& p : points) {
                    const double side = Dot(n, p) - distance;
                    positive |= side > 1e-7;
                    negative |= side < -1e-7;
                    if (positive && negative)
                        break;
                }
                if (positive == negative)
                    continue;
                if (positive) {
                    for (auto& v : n)
                        v = -v;
                    distance = -distance;
                }
                bool duplicate = false;
                for (const auto& p : result.planes)
                    if (Dot(p.normal, n) > 1 - 1e-12 && std::abs(p.distance - distance) < 1e-7) {
                        duplicate = true;
                        break;
                    }
                if (!duplicate)
                    result.planes.push_back({n, distance});
            }
    return result.planes.size() >= 4;
}
struct Hit {
    double fraction = 1;
    Vec normal{};
    bool startSolid = false, allSolid = false;
    uint32_t contents = 0;
};
inline bool Cast(const Hull& hull, const Vec& start, const Vec& end, Hit& hit, bool detectInside) {
    double enter = -1, leave = 1;
    Vec normal{};
    bool outside = false, endOutside = false;
    for (const auto& plane : hull.planes) {
        const double a = Dot(plane.normal, start) - plane.distance;
        const double b = Dot(plane.normal, end) - plane.distance;
        outside |= a > 1e-6;
        endOutside |= b > 1e-6;
        if (a > 0 && b >= a)
            return false;
        if (a <= 0 && b <= 0)
            continue;
        if (a > b) {
            const double t = a / (a - b);
            if (t > enter) {
                enter = t;
                normal = plane.normal;
            }
        } else {
            leave = std::min(leave, a / (a - b));
        }
    }
    if (!outside) {
        if (!detectInside)
            return false;
        const auto direction = Sub(start, end);
        const auto length = std::sqrt(Dot(direction, direction));
        normal = length > 1e-9
                     ? Vec{direction[0] / length, direction[1] / length, direction[2] / length}
                     : Vec{0, 0, 1};
        enter = 0;
    }
    if (enter > leave || enter < 0 || enter >= hit.fraction)
        return false;
    hit = {enter, normal, !outside, !outside && !endOutside, hull.contents};
    return true;
}
class World {
    struct Node {
        Vec mins{}, maxs{};
        unsigned first = 0, count = 0, left = 0, right = 0;
        uint32_t contents = 0;
    };
    std::vector<Hull> hulls;
    std::vector<unsigned> order;
    std::vector<Node> nodes;
    unsigned BuildNode(unsigned first, unsigned count) {
        Node node;
        node.first = first;
        node.count = count;
        node.mins.fill(std::numeric_limits<double>::max());
        node.maxs.fill(std::numeric_limits<double>::lowest());
        for (unsigned i = first; i < first + count; ++i) {
            node.contents |= hulls[order[i]].contents;
            for (unsigned k = 0; k < 3; ++k) {
                node.mins[k] = std::min(node.mins[k], hulls[order[i]].mins[k]);
                node.maxs[k] = std::max(node.maxs[k], hulls[order[i]].maxs[k]);
            }
        }
        const unsigned index = unsigned(nodes.size());
        nodes.push_back(node);
        if (count > 8) {
            unsigned axis = 0;
            for (unsigned k = 1; k < 3; ++k)
                if (node.maxs[k] - node.mins[k] > node.maxs[axis] - node.mins[axis])
                    axis = k;
            const auto mid = first + count / 2;
            std::nth_element(order.begin() + first, order.begin() + mid,
                             order.begin() + first + count, [&](unsigned a, unsigned b) {
                                 return hulls[a].mins[axis] + hulls[a].maxs[axis] <
                                        hulls[b].mins[axis] + hulls[b].maxs[axis];
                             });
            nodes[index].left = BuildNode(first, mid - first);
            nodes[index].right = BuildNode(mid, first + count - mid);
            nodes[index].count = 0;
        }
        return index;
    }
    bool CastNode(unsigned index,
                  const Vec& start,
                  const Vec& end,
                  Hit& hit,
                  bool detectInside,
                  uint32_t mask) const {
        const auto& node = nodes[index];
        if (!(node.contents & mask))
            return false;
        double enter = 0, leave = hit.fraction;
        for (unsigned k = 0; k < 3; ++k) {
            const double delta = end[k] - start[k];
            if (std::abs(delta) < 1e-12) {
                if (start[k] < node.mins[k] || start[k] > node.maxs[k])
                    return false;
            } else {
                double a = (node.mins[k] - start[k]) / delta;
                double b = (node.maxs[k] - start[k]) / delta;
                if (a > b)
                    std::swap(a, b);
                enter = std::max(enter, a);
                leave = std::min(leave, b);
                if (enter > leave)
                    return false;
            }
        }
        bool found = false;
        if (node.count) {
            for (unsigned i = node.first; i < node.first + node.count; ++i)
                if (hulls[order[i]].contents & mask)
                    found |= Cast(hulls[order[i]], start, end, hit, detectInside);
        } else {
            found = CastNode(node.left, start, end, hit, detectInside, mask);
            found |= CastNode(node.right, start, end, hit, detectInside, mask);
        }
        return found;
    }

  public:
    bool Build(const std::vector<collisionfile::Brush>& brushes) {
        hulls.clear();
        nodes.clear();
        order.clear();
        hulls.resize(brushes.size());
        for (unsigned i = 0; i < brushes.size(); ++i)
            if (!BuildHull(brushes[i], hulls[i])) {
                hulls.clear();
                return false;
            }
        order.resize(hulls.size());
        std::iota(order.begin(), order.end(), 0);
        if (!hulls.empty())
            BuildNode(0, unsigned(hulls.size()));
        return !nodes.empty();
    }
    bool Trace(const Vec& start,
               const Vec& end,
               Hit& hit,
               bool detectInside = true,
               uint32_t mask = collisionfile::Solid) const {
        return !nodes.empty() && CastNode(0, start, end, hit, detectInside, mask);
    }
};
}
