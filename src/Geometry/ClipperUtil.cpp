#include "Geometry/ClipperUtil.h"

#include "Base/Tolerance.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoTransform.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cad {

namespace {

using CPath64 = int64_t*;
using CPaths64 = int64_t*;

extern "C" {
__declspec(dllimport) int BooleanOp64(
    uint8_t cliptype,
    uint8_t fillrule,
    CPaths64 subjects,
    CPaths64 subjects_open,
    CPaths64 clips,
    CPaths64& solution,
    CPaths64& solution_open,
    bool preserve_collinear = true,
    bool reverse_solution = false);

__declspec(dllimport) CPaths64 InflatePaths64(
    CPaths64 paths,
    double delta,
    uint8_t jointype,
    uint8_t endtype,
    double miter_limit = 2.0,
    double arc_tolerance = 0.0,
    bool reverse_solution = false);

__declspec(dllimport) void DisposeArray64(CPaths64& p);
}

constexpr uint8_t kClipUnion = 2;
constexpr uint8_t kClipDifference = 3;
constexpr uint8_t kClipIntersection = 1;
constexpr uint8_t kFillNonZero = 1;
constexpr uint8_t kJoinRound = 2;
constexpr uint8_t kEndPolygon = 0;

void DisposeLocalPaths(CPaths64 paths)
{
    delete[] paths;
}

void DisposeClipperPaths(CPaths64& paths)
{
    DisposeArray64(paths);
}

CPath64 ToClipperPath(const std::vector<GeoPoint>& points)
{
    if (points.size() < 3) {
        return nullptr;
    }

    const std::size_t arrayLen = 2 + points.size() * 2;
    CPath64 path = new int64_t[arrayLen];
    int64_t* cursor = path;
    *cursor++ = static_cast<int64_t>(points.size());
    *cursor++ = 0;
    for (const GeoPoint& point : points) {
        *cursor++ = static_cast<int64_t>(std::llround(point.X() * ClipperUtil::Scale));
        *cursor++ = static_cast<int64_t>(std::llround(point.Y() * ClipperUtil::Scale));
    }
    return path;
}

CPaths64 ToClipperPaths(const std::vector<GeoRegion>& regions)
{
    std::size_t pathCount = 0;
    std::size_t arrayLen = 2;
    for (const GeoRegion& region : regions) {
        if (region.Outer().size() >= 3) {
            arrayLen += 2 + region.Outer().size() * 2;
            ++pathCount;
        }
    }
    if (pathCount == 0) {
        return nullptr;
    }

    CPaths64 paths = new int64_t[arrayLen];
    int64_t* cursor = paths;
    *cursor++ = static_cast<int64_t>(arrayLen);
    *cursor++ = static_cast<int64_t>(pathCount);
    for (const GeoRegion& region : regions) {
        const auto& outer = region.Outer();
        if (outer.size() < 3) {
            continue;
        }
        *cursor++ = static_cast<int64_t>(outer.size());
        *cursor++ = 0;
        for (const GeoPoint& point : outer) {
            *cursor++ = static_cast<int64_t>(std::llround(point.X() * ClipperUtil::Scale));
            *cursor++ = static_cast<int64_t>(std::llround(point.Y() * ClipperUtil::Scale));
        }
    }
    return paths;
}

std::vector<GeoRegion> FromClipperPaths(CPaths64 paths)
{
    std::vector<GeoRegion> regions;
    if (!paths) {
        return regions;
    }

    int64_t* cursor = paths + 1;
    const std::size_t pathCount = static_cast<std::size_t>(*cursor++);
    regions.reserve(pathCount);
    for (std::size_t i = 0; i < pathCount; ++i) {
        const std::size_t count = static_cast<std::size_t>(*cursor);
        cursor += 2;
        std::vector<GeoPoint> points;
        points.reserve(count);
        for (std::size_t j = 0; j < count; ++j) {
            const double x = static_cast<double>(*cursor++) / ClipperUtil::Scale;
            const double y = static_cast<double>(*cursor++) / ClipperUtil::Scale;
            points.emplace_back(x, y);
        }
        if (points.size() >= 3) {
            regions.emplace_back(std::move(points));
        }
    }
    return regions;
}

uint8_t ToClipType(BooleanOp op)
{
    switch (op) {
    case BooleanOp::Union:
        return kClipUnion;
    case BooleanOp::Intersection:
        return kClipIntersection;
    case BooleanOp::Difference:
        return kClipDifference;
    case BooleanOp::Xor:
        return 4;
    }
    return kClipUnion;
}

} // namespace

GeoRegion ClipperUtil::CircleToRegion(const GeoPoint& center, double radius, int segments)
{
    std::vector<GeoPoint> points;
    points.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double angle = (2.0 * 3.14159265358979323846 * i) / segments;
        points.emplace_back(center.X() + std::cos(angle) * radius, center.Y() + std::sin(angle) * radius);
    }
    return GeoRegion(std::move(points));
}

GeoRegion ClipperUtil::PolylineToRegion(const std::vector<GeoPoint>& points)
{
    return GeoRegion(points);
}

std::vector<GeoRegion> ClipperUtil::BooleanOperation(
    BooleanOp op,
    const std::vector<GeoRegion>& subjects,
    const std::vector<GeoRegion>& clips)
{
    CPaths64 subjectPaths = ToClipperPaths(subjects);
    CPaths64 clipPaths = ToClipperPaths(clips);
    if (!subjectPaths) {
        DisposeLocalPaths(clipPaths);
        return {};
    }

    CPaths64 solution = nullptr;
    CPaths64 solutionOpen = nullptr;
    const int result = BooleanOp64(
        ToClipType(op),
        kFillNonZero,
        subjectPaths,
        nullptr,
        clipPaths,
        solution,
        solutionOpen);

    DisposeLocalPaths(subjectPaths);
    DisposeLocalPaths(clipPaths);

    if (result != 0) {
        DisposeClipperPaths(solution);
        DisposeClipperPaths(solutionOpen);
        return {};
    }

    const std::vector<GeoRegion> regions = FromClipperPaths(solution);
    DisposeClipperPaths(solution);
    DisposeClipperPaths(solutionOpen);
    return regions;
}

std::vector<GeoRegion> ClipperUtil::OffsetRegion(const GeoRegion& region, double delta)
{
    CPaths64 inputPaths = ToClipperPaths({region});
    if (!inputPaths) {
        return {};
    }

    CPaths64 solution = InflatePaths64(
        inputPaths,
        delta * Scale,
        kJoinRound,
        kEndPolygon,
        2.0,
        Tolerance::ClipperArcTolerance * Scale);
    DisposeLocalPaths(inputPaths);

    if (!solution) {
        return {};
    }

    const std::vector<GeoRegion> regions = FromClipperPaths(solution);
    DisposeClipperPaths(solution);
    return regions;
}

std::vector<GeoRegion> ClipperUtil::ClipRegion(const GeoRegion& region, const GeoRegion& clipRect)
{
    return BooleanOperation(BooleanOp::Intersection, {region}, {clipRect});
}

std::vector<GeoLine> ClipperUtil::GenerateHatch(const GeoRegion& region, double spacing, double angleRadians)
{
    const auto& outer = region.Outer();
    if (outer.size() < 3 || spacing <= Tolerance::Model) {
        return {};
    }

    GeoPoint center(0.0, 0.0);
    for (const GeoPoint& point : outer) {
        center = GeoPoint(center.X() + point.X(), center.Y() + point.Y());
    }
    center = GeoPoint(center.X() / static_cast<double>(outer.size()), center.Y() / static_cast<double>(outer.size()));

    std::vector<GeoPoint> rotated;
    rotated.reserve(outer.size());
    for (const GeoPoint& point : outer) {
        rotated.push_back(GeoTransform::RotatePoint(point, center, -angleRadians));
    }

    double minY = rotated.front().Y();
    double maxY = minY;
    for (const GeoPoint& point : rotated) {
        minY = std::min(minY, point.Y());
        maxY = std::max(maxY, point.Y());
    }

    std::vector<GeoLine> hatchLines;
    const std::size_t vertexCount = rotated.size();
    for (double scanY = minY; scanY <= maxY + spacing * 0.5; scanY += spacing) {
        std::vector<double> intersections;
        intersections.reserve(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const GeoPoint& start = rotated[i];
            const GeoPoint& end = rotated[(i + 1) % vertexCount];
            const double y1 = start.Y();
            const double y2 = end.Y();

            if (std::abs(y1 - y2) <= Tolerance::Model) {
                if (std::abs(scanY - y1) <= Tolerance::Model) {
                    intersections.push_back(start.X());
                    intersections.push_back(end.X());
                }
                continue;
            }

            const double t = (scanY - y1) / (y2 - y1);
            if (t < 0.0 || t > 1.0) {
                continue;
            }
            intersections.push_back(start.X() + t * (end.X() - start.X()));
        }

        if (intersections.size() < 2) {
            continue;
        }

        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 0; i + 1 < intersections.size(); i += 2) {
            const double x1 = intersections[i];
            const double x2 = intersections[i + 1];
            if (x2 - x1 <= Tolerance::Model) {
                continue;
            }
            GeoPoint lineStart = GeoTransform::RotatePoint(GeoPoint(x1, scanY), center, angleRadians);
            GeoPoint lineEnd = GeoTransform::RotatePoint(GeoPoint(x2, scanY), center, angleRadians);
            hatchLines.emplace_back(lineStart, lineEnd);
        }
    }

    return hatchLines;
}

} // namespace cad
