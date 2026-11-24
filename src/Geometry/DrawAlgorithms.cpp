#include "Geometry/DrawAlgorithms.h"

#include "Base/Tolerance.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

namespace cad {

namespace {
constexpr double kPi = 3.14159265358979323846;

double CrossZ(const GeoPoint& origin, const GeoPoint& a, const GeoPoint& b)
{
    return (a.X() - origin.X()) * (b.Y() - origin.Y()) - (a.Y() - origin.Y()) * (b.X() - origin.X());
}

std::optional<GeoPoint> LineIntersection(const GeoLine& a, const GeoLine& b)
{
    const double x1 = a.Start().X();
    const double y1 = a.Start().Y();
    const double x2 = a.End().X();
    const double y2 = a.End().Y();
    const double x3 = b.Start().X();
    const double y3 = b.Start().Y();
    const double x4 = b.End().X();
    const double y4 = b.End().Y();
    const double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) <= Tolerance::Model) {
        return std::nullopt;
    }
    const double px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / denom;
    const double py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / denom;
    return GeoPoint(px, py);
}

GeoPoint Normalize(const GeoPoint& vector)
{
    const double len = std::hypot(vector.X(), vector.Y());
    if (len <= Tolerance::Model) {
        return GeoPoint(1.0, 0.0);
    }
    return GeoPoint(vector.X() / len, vector.Y() / len);
}

GeoPoint LineDirection(const GeoLine& line)
{
    return Normalize(GeoPoint(line.End().X() - line.Start().X(), line.End().Y() - line.Start().Y()));
}

GeoPoint LineNormal(const GeoLine& line)
{
    const GeoPoint dir = LineDirection(line);
    return GeoPoint(-dir.Y(), dir.X());
}

double SignedDistanceToLine(const GeoLine& line, const GeoPoint& point)
{
    const GeoPoint normal = LineNormal(line);
    const GeoPoint anchor = line.Start();
    return (point.X() - anchor.X()) * normal.X() + (point.Y() - anchor.Y()) * normal.Y();
}

GeoLine OffsetLine(const GeoLine& line, double offset)
{
    const GeoPoint normal = LineNormal(line);
    const GeoPoint delta(normal.X() * offset, normal.Y() * offset);
    return GeoLine(line.Start().Translated(delta.X(), delta.Y()), line.End().Translated(delta.X(), delta.Y()));
}

GeoPoint ProjectOnInfiniteLine(const GeoLine& line, const GeoPoint& point)
{
    const GeoPoint dir = LineDirection(line);
    const double t = (point.X() - line.Start().X()) * dir.X() + (point.Y() - line.Start().Y()) * dir.Y();
    return GeoPoint(line.Start().X() + t * dir.X(), line.Start().Y() + t * dir.Y());
}

bool PointOnArcSpan(const GeoArc& arc, const GeoPoint& point)
{
    const double ang = std::atan2(point.Y() - arc.Center().Y(), point.X() - arc.Center().X());
    double sweep = arc.EndAngleRadians() - arc.StartAngleRadians();
    while (sweep > 2.0 * kPi) {
        sweep -= 2.0 * kPi;
    }
    while (sweep < -2.0 * kPi) {
        sweep += 2.0 * kPi;
    }
    auto relative = [](double angle, double base) {
        double relativeAngle = angle - base;
        while (relativeAngle < 0.0) {
            relativeAngle += 2.0 * kPi;
        }
        while (relativeAngle >= 2.0 * kPi) {
            relativeAngle -= 2.0 * kPi;
        }
        return relativeAngle;
    };
    if (std::abs(sweep) <= 1.0e-9) {
        return relative(ang, arc.StartAngleRadians()) <= 1.0e-6
            || relative(ang, arc.StartAngleRadians()) >= 2.0 * kPi - 1.0e-6;
    }
    if (sweep >= 0.0) {
        return relative(ang, arc.StartAngleRadians()) <= sweep + 1.0e-6;
    }
    return relative(arc.StartAngleRadians(), ang) <= -sweep + 1.0e-6;
}

GeoPoint TangentPointOnCircle(const GeoCircle& circle, const GeoPoint& newCenter, double newRadius)
{
    const double dx = newCenter.X() - circle.Center().X();
    const double dy = newCenter.Y() - circle.Center().Y();
    const double dist = std::hypot(dx, dy);
    const double ux = dist > Tolerance::Model ? dx / dist : 1.0;
    const double uy = dist > Tolerance::Model ? dy / dist : 0.0;
    const GeoPoint plus(
        circle.Center().X() + ux * circle.Radius(),
        circle.Center().Y() + uy * circle.Radius());
    const GeoPoint minus(
        circle.Center().X() - ux * circle.Radius(),
        circle.Center().Y() - uy * circle.Radius());
    return std::abs(Distance2D(newCenter, plus) - newRadius)
            <= std::abs(Distance2D(newCenter, minus) - newRadius)
        ? plus
        : minus;
}

std::vector<GeoPoint> IntersectInfiniteLineCircle(const GeoLine& line, const GeoCircle& circle)
{
    const GeoPoint dir = LineDirection(line);
    const double fx = line.Start().X() - circle.Center().X();
    const double fy = line.Start().Y() - circle.Center().Y();
    const double b = 2.0 * (fx * dir.X() + fy * dir.Y());
    const double c = fx * fx + fy * fy - circle.Radius() * circle.Radius();
    const double disc = b * b - 4.0 * c;
    std::vector<GeoPoint> points;
    if (disc < -Tolerance::Model) {
        return points;
    }
    const double sqrtDisc = disc <= 0.0 ? 0.0 : std::sqrt(disc);
    const double t1 = (-b - sqrtDisc) * 0.5;
    points.emplace_back(line.Start().X() + t1 * dir.X(), line.Start().Y() + t1 * dir.Y());
    if (sqrtDisc > Tolerance::Model) {
        const double t2 = (-b + sqrtDisc) * 0.5;
        points.emplace_back(line.Start().X() + t2 * dir.X(), line.Start().Y() + t2 * dir.Y());
    }
    return points;
}

std::vector<GeoPoint> IntersectCircles(const GeoCircle& a, const GeoCircle& b)
{
    const double dx = b.Center().X() - a.Center().X();
    const double dy = b.Center().Y() - a.Center().Y();
    const double dist = std::hypot(dx, dy);
    std::vector<GeoPoint> points;
    if (dist <= Tolerance::Model) {
        return points;
    }
    const double r0 = a.Radius();
    const double r1 = b.Radius();
    if (dist > r0 + r1 + Tolerance::Model || dist < std::abs(r0 - r1) - Tolerance::Model) {
        return points;
    }
    const double aLen = (r0 * r0 - r1 * r1 + dist * dist) / (2.0 * dist);
    const double h2 = r0 * r0 - aLen * aLen;
    const double h = h2 <= 0.0 ? 0.0 : std::sqrt(h2);
    const double ux = dx / dist;
    const double uy = dy / dist;
    const GeoPoint mid(a.Center().X() + ux * aLen, a.Center().Y() + uy * aLen);
    if (h <= Tolerance::Model) {
        points.push_back(mid);
        return points;
    }
    points.emplace_back(mid.X() - uy * h, mid.Y() + ux * h);
    points.emplace_back(mid.X() + uy * h, mid.Y() - ux * h);
    return points;
}

std::optional<GeoPoint> TangentPointOnSupport(
    const DrawAlgorithms::TangentSupport& support,
    const GeoCircle& circle)
{
    switch (support.kind) {
    case DrawAlgorithms::TangentSupport::Kind::Line: {
        const GeoPoint tan = ProjectOnInfiniteLine(support.line, circle.Center());
        if (std::abs(Distance2D(circle.Center(), tan) - circle.Radius()) > std::max(Tolerance::Model, circle.Radius() * 1.0e-6)) {
            return std::nullopt;
        }
        return tan;
    }
    case DrawAlgorithms::TangentSupport::Kind::Circle: {
        if (support.circle.Radius() <= Tolerance::Model) {
            return std::nullopt;
        }
        const GeoPoint tan = TangentPointOnCircle(support.circle, circle.Center(), circle.Radius());
        if (std::abs(Distance2D(circle.Center(), tan) - circle.Radius())
            > std::max(Tolerance::Model, circle.Radius() * 1.0e-5)) {
            return std::nullopt;
        }
        return tan;
    }
    case DrawAlgorithms::TangentSupport::Kind::Arc: {
        if (support.arc.Radius() <= Tolerance::Model) {
            return std::nullopt;
        }
        const GeoCircle supporting(support.arc.Center(), support.arc.Radius());
        const GeoPoint tan = TangentPointOnCircle(supporting, circle.Center(), circle.Radius());
        if (std::abs(Distance2D(circle.Center(), tan) - circle.Radius())
            > std::max(Tolerance::Model, circle.Radius() * 1.0e-5)) {
            return std::nullopt;
        }
        if (!PointOnArcSpan(support.arc, tan)) {
            return std::nullopt;
        }
        return tan;
    }
    }
    return std::nullopt;
}

void ConsiderTtrCandidate(
    const GeoCircle& candidate,
    const DrawAlgorithms::TangentSupport& first,
    const DrawAlgorithms::TangentSupport& second,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const auto tan1 = TangentPointOnSupport(first, candidate);
    const auto tan2 = TangentPointOnSupport(second, candidate);
    if (!tan1 || !tan2) {
        return;
    }
    const double score = Distance2D(*tan1, first.pick) + Distance2D(*tan2, second.pick);
    if (!best || score < bestScore) {
        bestScore = score;
        best = candidate;
    }
}

GeoCircle SupportingCircle(const DrawAlgorithms::TangentSupport& support)
{
    if (support.kind == DrawAlgorithms::TangentSupport::Kind::Arc) {
        return GeoCircle(support.arc.Center(), support.arc.Radius());
    }
    return support.circle;
}

void SolveLineLineTtr(
    const DrawAlgorithms::TangentSupport& first,
    const DrawAlgorithms::TangentSupport& second,
    double radius,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const double dStart = std::abs(SignedDistanceToLine(first.line, second.line.Start()));
    const double dEnd = std::abs(SignedDistanceToLine(first.line, second.line.End()));
    if (dStart <= Tolerance::Model && dEnd <= Tolerance::Model) {
        return;
    }

    const double signs[] = {1.0, -1.0};
    for (double sign1 : signs) {
        for (double sign2 : signs) {
            const GeoLine o1 = OffsetLine(first.line, sign1 * radius);
            const GeoLine o2 = OffsetLine(second.line, sign2 * radius);
            if (const auto center = LineIntersection(o1, o2)) {
                ConsiderTtrCandidate(GeoCircle(*center, radius), first, second, bestScore, best);
                continue;
            }
            const double gap = std::abs(SignedDistanceToLine(first.line, second.line.Start()));
            if (std::abs(gap - 2.0 * radius) > std::max(Tolerance::Model, radius * 1.0e-6)) {
                continue;
            }
            const double midSign = SignedDistanceToLine(first.line, second.line.Start()) >= 0.0 ? 1.0 : -1.0;
            const GeoLine mid = OffsetLine(first.line, midSign * radius);
            const GeoPoint midPick(
                (first.pick.X() + second.pick.X()) * 0.5,
                (first.pick.Y() + second.pick.Y()) * 0.5);
            ConsiderTtrCandidate(
                GeoCircle(ProjectOnInfiniteLine(mid, midPick), radius),
                first,
                second,
                bestScore,
                best);
        }
    }
}

void SolveLineCircleTtr(
    const DrawAlgorithms::TangentSupport& lineSupport,
    const DrawAlgorithms::TangentSupport& circleSupport,
    double radius,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const GeoCircle other = SupportingCircle(circleSupport);
    if (other.Radius() <= Tolerance::Model) {
        return;
    }
    const double radii[] = {other.Radius() + radius, std::abs(other.Radius() - radius)};
    const double signs[] = {1.0, -1.0};
    for (double sign : signs) {
        const GeoLine offset = OffsetLine(lineSupport.line, sign * radius);
        for (double cr : radii) {
            if (cr <= Tolerance::Model) {
                continue;
            }
            for (const GeoPoint& center : IntersectInfiniteLineCircle(offset, GeoCircle(other.Center(), cr))) {
                ConsiderTtrCandidate(GeoCircle(center, radius), lineSupport, circleSupport, bestScore, best);
            }
        }
    }
}

void SolveCircleCircleTtr(
    const DrawAlgorithms::TangentSupport& first,
    const DrawAlgorithms::TangentSupport& second,
    double radius,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const GeoCircle a = SupportingCircle(first);
    const GeoCircle b = SupportingCircle(second);
    if (a.Radius() <= Tolerance::Model || b.Radius() <= Tolerance::Model) {
        return;
    }
    const double radiiA[] = {a.Radius() + radius, std::abs(a.Radius() - radius)};
    const double radiiB[] = {b.Radius() + radius, std::abs(b.Radius() - radius)};
    for (double ra : radiiA) {
        if (ra <= Tolerance::Model) {
            continue;
        }
        for (double rb : radiiB) {
            if (rb <= Tolerance::Model) {
                continue;
            }
            for (const GeoPoint& center : IntersectCircles(GeoCircle(a.Center(), ra), GeoCircle(b.Center(), rb))) {
                ConsiderTtrCandidate(GeoCircle(center, radius), first, second, bestScore, best);
            }
        }
    }
}

std::optional<GeoCircle> CircleFromCenterAndSignedRadius(const GeoPoint& center, double signedRadius)
{
    const double radius = std::abs(signedRadius);
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    return GeoCircle(center, radius);
}

std::vector<double> BuildUniformKnots(int controlCount, int degree)
{
    const int knotCount = controlCount + degree + 1;
    std::vector<double> knots(static_cast<std::size_t>(knotCount), 0.0);
    for (int i = 0; i < knotCount; ++i) {
        if (i <= degree) {
            knots[static_cast<std::size_t>(i)] = 0.0;
        } else if (i >= controlCount + 1) {
            knots[static_cast<std::size_t>(i)] = 1.0;
        } else {
            knots[static_cast<std::size_t>(i)] =
                static_cast<double>(i - degree) / static_cast<double>(controlCount - degree);
        }
    }
    return knots;
}

std::vector<double> EffectiveKnots(int controlCount, int degree, const std::vector<double>& knots)
{
    if (controlCount + degree + 1 == static_cast<int>(knots.size())) {
        return knots;
    }
    return BuildUniformKnots(controlCount, degree);
}

std::vector<double> UniqueKnotSpans(const std::vector<double>& knots, int degree, int controlCount)
{
    std::vector<double> spans;
    if (knots.empty() || controlCount <= 0) {
        spans.push_back(0.0);
        spans.push_back(1.0);
        return spans;
    }
    const int last = std::min(controlCount, static_cast<int>(knots.size()) - 1);
    const int first = std::clamp(degree, 0, last);
    spans.push_back(knots[static_cast<std::size_t>(first)]);
    for (int i = first + 1; i <= last; ++i) {
        const double value = knots[static_cast<std::size_t>(i)];
        if (value > spans.back() + 1.0e-12) {
            spans.push_back(value);
        }
    }
    if (spans.size() < 2) {
        spans.push_back(spans.front() + 1.0);
    }
    return spans;
}

GeoPoint DeBoor(double t, int degree, const std::vector<GeoPoint>& controls, const std::vector<double>& knots)
{
    std::vector<GeoPoint> d(static_cast<std::size_t>(degree + 1));
    int span = degree;
    while (span < static_cast<int>(controls.size()) - 1 && t >= knots[static_cast<std::size_t>(span + 1)]) {
        ++span;
    }
    for (int j = 0; j <= degree; ++j) {
        d[static_cast<std::size_t>(j)] = controls[static_cast<std::size_t>(span - degree + j)];
    }
    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const int idx = span - degree + j;
            const double denom = knots[static_cast<std::size_t>(idx + degree - r + 1)] - knots[static_cast<std::size_t>(idx)];
            const double alpha = denom <= Tolerance::Model ? 0.0 : (t - knots[static_cast<std::size_t>(idx)]) / denom;
            d[static_cast<std::size_t>(j)] = GeoPoint(
                (1.0 - alpha) * d[static_cast<std::size_t>(j - 1)].X() + alpha * d[static_cast<std::size_t>(j)].X(),
                (1.0 - alpha) * d[static_cast<std::size_t>(j - 1)].Y() + alpha * d[static_cast<std::size_t>(j)].Y());
        }
    }
    return d[static_cast<std::size_t>(degree)];
}
} // namespace

std::optional<GeoCircle> DrawAlgorithms::CircleFromThreePoints(const GeoPoint& p1, const GeoPoint& p2, const GeoPoint& p3)
{
    const double ax = p2.X() - p1.X();
    const double ay = p2.Y() - p1.Y();
    const double bx = p3.X() - p1.X();
    const double by = p3.Y() - p1.Y();
    const double cross = ax * by - ay * bx;
    if (std::abs(cross) <= Tolerance::Model) {
        return std::nullopt;
    }
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double cx = (by * a2 - ay * b2) / (2.0 * cross);
    const double cy = (ax * b2 - bx * a2) / (2.0 * cross);
    const GeoPoint center(p1.X() + cx, p1.Y() + cy);
    return GeoCircle(center, Distance2D(center, p1));
}

std::optional<GeoArc> DrawAlgorithms::ArcFromThreePoints(const GeoPoint& p1, const GeoPoint& p2, const GeoPoint& p3)
{
    const auto circle = CircleFromThreePoints(p1, p2, p3);
    if (!circle) {
        return std::nullopt;
    }
    const GeoPoint& center = circle->Center();
    const double radius = circle->Radius();

    const double start = std::atan2(p1.Y() - center.Y(), p1.X() - center.X());
    const double mid = std::atan2(p2.Y() - center.Y(), p2.X() - center.X());
    const double end = std::atan2(p3.Y() - center.Y(), p3.X() - center.X());

    const auto relativeAngle = [](double angle, double base) {
        double relative = angle - base;
        while (relative < 0.0) {
            relative += 2.0 * kPi;
        }
        while (relative >= 2.0 * kPi) {
            relative -= 2.0 * kPi;
        }
        return relative;
    };

    const auto pointOnSweep = [&](double sweep) {
        const double midRelative = relativeAngle(mid, start);
        if (sweep >= 0.0) {
            return midRelative <= sweep + 1.0e-9;
        }
        return midRelative >= 2.0 * kPi + sweep - 1.0e-9;
    };

    const double ccwSweep = relativeAngle(end, start);
    if (ccwSweep <= Tolerance::Model) {
        return std::nullopt;
    }

    double sweep = ccwSweep;
    if (!pointOnSweep(sweep)) {
        sweep = ccwSweep - 2.0 * kPi;
        if (!pointOnSweep(sweep) || std::abs(sweep) <= Tolerance::Model) {
            return std::nullopt;
        }
    }

    if (std::abs(sweep) >= 2.0 * kPi - 1.0e-6) {
        return std::nullopt;
    }

    return GeoArc(center, radius, start, start + sweep);
}

std::optional<GeoCircle> DrawAlgorithms::CircleFromTwoPoints(const GeoPoint& p1, const GeoPoint& p2)
{
    const GeoPoint center((p1.X() + p2.X()) * 0.5, (p1.Y() + p2.Y()) * 0.5);
    return GeoCircle(center, Distance2D(center, p1));
}

std::optional<GeoCircle> DrawAlgorithms::CircleFromCenterRadius(const GeoPoint& center, const GeoPoint& radiusPoint)
{
    const double radius = Distance2D(center, radiusPoint);
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    return GeoCircle(center, radius);
}

std::optional<GeoCircle> DrawAlgorithms::CircleFromCenterDiameter(const GeoPoint& center, const GeoPoint& diameterPoint)
{
    const double diameter = Distance2D(center, diameterPoint);
    if (diameter <= Tolerance::Model) {
        return std::nullopt;
    }
    return GeoCircle(center, diameter * 0.5);
}

std::optional<GeoCircle> DrawAlgorithms::CircleTanTanRadius(
    const GeoLine& line1,
    const GeoLine& line2,
    double radius,
    const GeoPoint& pickNear)
{
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    const double sign1 = SignedDistanceToLine(line1, pickNear) >= 0.0 ? 1.0 : -1.0;
    const double sign2 = SignedDistanceToLine(line2, pickNear) >= 0.0 ? 1.0 : -1.0;
    const GeoLine offset1 = OffsetLine(line1, sign1 * radius);
    const GeoLine offset2 = OffsetLine(line2, sign2 * radius);
    const auto centerOpt = LineIntersection(offset1, offset2);
    if (!centerOpt) {
        return std::nullopt;
    }
    return GeoCircle(*centerOpt, radius);
}

std::optional<GeoCircle> DrawAlgorithms::CircleTanTanRadius(
    const TangentSupport& first,
    const TangentSupport& second,
    double radius)
{
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }

    const auto isLinear = [](const TangentSupport& support) {
        return support.kind == TangentSupport::Kind::Line;
    };

    double bestScore = std::numeric_limits<double>::max();
    std::optional<GeoCircle> best;
    if (isLinear(first) && isLinear(second)) {
        SolveLineLineTtr(first, second, radius, bestScore, best);
    } else if (isLinear(first) && !isLinear(second)) {
        SolveLineCircleTtr(first, second, radius, bestScore, best);
    } else if (!isLinear(first) && isLinear(second)) {
        SolveLineCircleTtr(second, first, radius, bestScore, best);
    } else {
        SolveCircleCircleTtr(first, second, radius, bestScore, best);
    }
    return best;
}

void ConsiderTttCandidate(
    const GeoCircle& candidate,
    const DrawAlgorithms::TangentSupport& a,
    const DrawAlgorithms::TangentSupport& b,
    const DrawAlgorithms::TangentSupport& c,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    if (candidate.Radius() <= Tolerance::Model) {
        return;
    }
    const auto tan1 = TangentPointOnSupport(a, candidate);
    const auto tan2 = TangentPointOnSupport(b, candidate);
    const auto tan3 = TangentPointOnSupport(c, candidate);
    if (!tan1 || !tan2 || !tan3) {
        return;
    }
    const double score =
        Distance2D(*tan1, a.pick) + Distance2D(*tan2, b.pick) + Distance2D(*tan3, c.pick);
    if (!best || score < bestScore) {
        bestScore = score;
        best = candidate;
    }
}

void AddQuadraticRoots(double a, double b, double c, std::vector<double>& roots)
{
    if (std::abs(a) <= 1.0e-14) {
        if (std::abs(b) > 1.0e-14) {
            roots.push_back(-c / b);
        }
        return;
    }
    double disc = b * b - 4.0 * a * c;
    if (disc < -Tolerance::Model) {
        return;
    }
    if (disc < 0.0) {
        disc = 0.0;
    }
    const double sqrtDisc = std::sqrt(disc);
    roots.push_back((-b - sqrtDisc) / (2.0 * a));
    roots.push_back((-b + sqrtDisc) / (2.0 * a));
}

void RadiiFromCircleConstraint(
    const GeoPoint& p,
    const GeoPoint& q,
    const GeoCircle& circle,
    double k,
    std::vector<double>& roots)
{
    const double dx = p.X() - circle.Center().X();
    const double dy = p.Y() - circle.Center().Y();
    const double r0 = circle.Radius();
    const double A = q.X() * q.X() + q.Y() * q.Y() - k * k;
    const double B = 2.0 * (dx * q.X() + dy * q.Y() - k * r0);
    const double C = dx * dx + dy * dy - r0 * r0;
    AddQuadraticRoots(A, B, C, roots);
}

bool CenterLinearInRadius(
    double ax, double ay, double aR, double a0,
    double bx, double by, double bR, double b0,
    GeoPoint* p,
    GeoPoint* q)
{
    const double det = ax * by - ay * bx;
    if (std::abs(det) <= Tolerance::Model) {
        return false;
    }
    *p = GeoPoint((a0 * by - ay * b0) / det, (ax * b0 - a0 * bx) / det);
    *q = GeoPoint((-aR * by + ay * bR) / det, (-ax * bR + aR * bx) / det);
    return true;
}

void LineConstraint(const GeoLine& line, double sign, double* nx, double* ny, double* nR, double* rhs)
{
    const GeoPoint n = LineNormal(line);
    *nx = n.X();
    *ny = n.Y();
    *nR = -sign;
    *rhs = n.X() * line.Start().X() + n.Y() * line.Start().Y();
}

void CircleDifferenceConstraint(
    const GeoCircle& c0,
    double k0,
    const GeoCircle& c1,
    double k1,
    double* ax,
    double* ay,
    double* aR,
    double* rhs)
{
    *ax = 2.0 * (c0.Center().X() - c1.Center().X());
    *ay = 2.0 * (c0.Center().Y() - c1.Center().Y());
    *aR = 2.0 * (k1 * c1.Radius() - k0 * c0.Radius());
    const double x0 = c0.Center().X();
    const double y0 = c0.Center().Y();
    const double x1 = c1.Center().X();
    const double y1 = c1.Center().Y();
    *rhs = (x0 * x0 + y0 * y0 - c0.Radius() * c0.Radius())
        - (x1 * x1 + y1 * y1 - c1.Radius() * c1.Radius());
}

void TryCentersFromRadiusParam(
    const GeoPoint& p,
    const GeoPoint& q,
    const std::vector<double>& radii,
    const DrawAlgorithms::TangentSupport& a,
    const DrawAlgorithms::TangentSupport& b,
    const DrawAlgorithms::TangentSupport& c,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    for (double radius : radii) {
        if (radius <= Tolerance::Model) {
            continue;
        }
        ConsiderTttCandidate(
            GeoCircle(GeoPoint(p.X() + radius * q.X(), p.Y() + radius * q.Y()), radius),
            a,
            b,
            c,
            bestScore,
            best);
    }
}

bool IsLinearSupport(const DrawAlgorithms::TangentSupport& support)
{
    return support.kind == DrawAlgorithms::TangentSupport::Kind::Line;
}

void SolveParallelLinesPlusThird(
    const DrawAlgorithms::TangentSupport& lineA,
    const DrawAlgorithms::TangentSupport& lineB,
    const DrawAlgorithms::TangentSupport& third,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const double dist = SignedDistanceToLine(lineA.line, lineB.line.Start());
    const double gap = std::abs(dist);
    if (gap <= Tolerance::Model) {
        return;
    }
    const double radius = gap * 0.5;
    const double s1 = dist >= 0.0 ? 1.0 : -1.0;
    const GeoLine mid = OffsetLine(lineA.line, s1 * radius);
    const GeoPoint dir = LineDirection(mid);
    const GeoPoint m0 = mid.Start();

    if (IsLinearSupport(third)) {
        const double signs[] = {1.0, -1.0};
        for (double s3 : signs) {
            double nx = 0.0;
            double ny = 0.0;
            double nR = 0.0;
            double rhs = 0.0;
            LineConstraint(third.line, s3, &nx, &ny, &nR, &rhs);
            const double denom = nx * dir.X() + ny * dir.Y();
            if (std::abs(denom) <= Tolerance::Model) {
                const double residual = nx * m0.X() + ny * m0.Y() + nR * radius - rhs;
                if (std::abs(residual) > std::max(Tolerance::Model, radius * 1.0e-6)) {
                    continue;
                }
                const GeoPoint avg(
                    (lineA.pick.X() + lineB.pick.X() + third.pick.X()) / 3.0,
                    (lineA.pick.Y() + lineB.pick.Y() + third.pick.Y()) / 3.0);
                ConsiderTttCandidate(
                    GeoCircle(ProjectOnInfiniteLine(mid, avg), radius),
                    lineA,
                    lineB,
                    third,
                    bestScore,
                    best);
                continue;
            }
            const double t = (rhs - nR * radius - nx * m0.X() - ny * m0.Y()) / denom;
            ConsiderTttCandidate(
                GeoCircle(GeoPoint(m0.X() + t * dir.X(), m0.Y() + t * dir.Y()), radius),
                lineA,
                lineB,
                third,
                bestScore,
                best);
        }
        return;
    }

    const GeoCircle c0 = SupportingCircle(third);
    const double ks[] = {1.0, -1.0};
    for (double k : ks) {
        const double tgt = c0.Radius() + k * radius;
        const double dx = m0.X() - c0.Center().X();
        const double dy = m0.Y() - c0.Center().Y();
        std::vector<double> ts;
        AddQuadraticRoots(
            1.0,
            2.0 * (dx * dir.X() + dy * dir.Y()),
            dx * dx + dy * dy - tgt * tgt,
            ts);
        for (double t : ts) {
            ConsiderTttCandidate(
                GeoCircle(GeoPoint(m0.X() + t * dir.X(), m0.Y() + t * dir.Y()), radius),
                lineA,
                lineB,
                third,
                bestScore,
                best);
        }
    }
}

void SolveThreeLinesTtt(
    const DrawAlgorithms::TangentSupport& a,
    const DrawAlgorithms::TangentSupport& b,
    const DrawAlgorithms::TangentSupport& c,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const double signs[] = {1.0, -1.0};
    for (double s1 : signs) {
        for (double s2 : signs) {
            double ax = 0.0, ay = 0.0, aR = 0.0, a0 = 0.0;
            double bx = 0.0, by = 0.0, bR = 0.0, b0 = 0.0;
            LineConstraint(a.line, s1, &ax, &ay, &aR, &a0);
            LineConstraint(b.line, s2, &bx, &by, &bR, &b0);
            GeoPoint p;
            GeoPoint q;
            if (!CenterLinearInRadius(ax, ay, aR, a0, bx, by, bR, b0, &p, &q)) {
                SolveParallelLinesPlusThird(a, b, c, bestScore, best);
                continue;
            }
            for (double s3 : signs) {
                double nx = 0.0, ny = 0.0, nR = 0.0, rhs = 0.0;
                LineConstraint(c.line, s3, &nx, &ny, &nR, &rhs);
                const double denom = nx * q.X() + ny * q.Y() + nR;
                if (std::abs(denom) <= Tolerance::Model) {
                    continue;
                }
                TryCentersFromRadiusParam(
                    p,
                    q,
                    {(rhs - nx * p.X() - ny * p.Y()) / denom},
                    a,
                    b,
                    c,
                    bestScore,
                    best);
            }
        }
    }
}

void SolveTwoLinesOneCircleTtt(
    const DrawAlgorithms::TangentSupport& lineA,
    const DrawAlgorithms::TangentSupport& lineB,
    const DrawAlgorithms::TangentSupport& circleSupport,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const GeoCircle c0 = SupportingCircle(circleSupport);
    const double signs[] = {1.0, -1.0};
    for (double s1 : signs) {
        for (double s2 : signs) {
            double ax = 0.0, ay = 0.0, aR = 0.0, a0 = 0.0;
            double bx = 0.0, by = 0.0, bR = 0.0, b0 = 0.0;
            LineConstraint(lineA.line, s1, &ax, &ay, &aR, &a0);
            LineConstraint(lineB.line, s2, &bx, &by, &bR, &b0);
            GeoPoint p;
            GeoPoint q;
            if (!CenterLinearInRadius(ax, ay, aR, a0, bx, by, bR, b0, &p, &q)) {
                SolveParallelLinesPlusThird(lineA, lineB, circleSupport, bestScore, best);
                continue;
            }
            for (double k : signs) {
                std::vector<double> radii;
                RadiiFromCircleConstraint(p, q, c0, k, radii);
                TryCentersFromRadiusParam(p, q, radii, lineA, lineB, circleSupport, bestScore, best);
            }
        }
    }
}

void SolveOneLineTwoCirclesTtt(
    const DrawAlgorithms::TangentSupport& lineSupport,
    const DrawAlgorithms::TangentSupport& circleA,
    const DrawAlgorithms::TangentSupport& circleB,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const GeoCircle c0 = SupportingCircle(circleA);
    const GeoCircle c1 = SupportingCircle(circleB);
    const double signs[] = {1.0, -1.0};
    for (double s : signs) {
        double lx = 0.0, ly = 0.0, lR = 0.0, l0 = 0.0;
        LineConstraint(lineSupport.line, s, &lx, &ly, &lR, &l0);
        for (double k0 : signs) {
            for (double k1 : signs) {
                double cx = 0.0, cy = 0.0, cR = 0.0, cRhs = 0.0;
                CircleDifferenceConstraint(c0, k0, c1, k1, &cx, &cy, &cR, &cRhs);
                GeoPoint p;
                GeoPoint q;
                if (!CenterLinearInRadius(lx, ly, lR, l0, cx, cy, cR, cRhs, &p, &q)) {
                    continue;
                }
                std::vector<double> radii;
                RadiiFromCircleConstraint(p, q, c0, k0, radii);
                TryCentersFromRadiusParam(p, q, radii, lineSupport, circleA, circleB, bestScore, best);
            }
        }
    }
}

void SolveThreeCirclesTtt(
    const DrawAlgorithms::TangentSupport& a,
    const DrawAlgorithms::TangentSupport& b,
    const DrawAlgorithms::TangentSupport& c,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const GeoCircle c0 = SupportingCircle(a);
    const GeoCircle c1 = SupportingCircle(b);
    const GeoCircle c2 = SupportingCircle(c);
    const double signs[] = {1.0, -1.0};
    for (double k0 : signs) {
        for (double k1 : signs) {
            for (double k2 : signs) {
                double ax = 0.0, ay = 0.0, aR = 0.0, a0 = 0.0;
                double bx = 0.0, by = 0.0, bR = 0.0, b0 = 0.0;
                CircleDifferenceConstraint(c0, k0, c1, k1, &ax, &ay, &aR, &a0);
                CircleDifferenceConstraint(c0, k0, c2, k2, &bx, &by, &bR, &b0);
                GeoPoint p;
                GeoPoint q;
                if (!CenterLinearInRadius(ax, ay, aR, a0, bx, by, bR, b0, &p, &q)) {
                    continue;
                }
                std::vector<double> radii;
                RadiiFromCircleConstraint(p, q, c0, k0, radii);
                TryCentersFromRadiusParam(p, q, radii, a, b, c, bestScore, best);
            }
        }
    }
}

void SolveTanTanTan(
    const DrawAlgorithms::TangentSupport& a,
    const DrawAlgorithms::TangentSupport& b,
    const DrawAlgorithms::TangentSupport& c,
    double& bestScore,
    std::optional<GeoCircle>& best)
{
    const bool la = IsLinearSupport(a);
    const bool lb = IsLinearSupport(b);
    const bool lc = IsLinearSupport(c);
    const int lines = (la ? 1 : 0) + (lb ? 1 : 0) + (lc ? 1 : 0);
    if (lines == 3) {
        SolveThreeLinesTtt(a, b, c, bestScore, best);
    } else if (lines == 2) {
        if (!la) {
            SolveTwoLinesOneCircleTtt(b, c, a, bestScore, best);
        } else if (!lb) {
            SolveTwoLinesOneCircleTtt(a, c, b, bestScore, best);
        } else {
            SolveTwoLinesOneCircleTtt(a, b, c, bestScore, best);
        }
    } else if (lines == 1) {
        if (la) {
            SolveOneLineTwoCirclesTtt(a, b, c, bestScore, best);
        } else if (lb) {
            SolveOneLineTwoCirclesTtt(b, a, c, bestScore, best);
        } else {
            SolveOneLineTwoCirclesTtt(c, a, b, bestScore, best);
        }
    } else {
        SolveThreeCirclesTtt(a, b, c, bestScore, best);
    }
}

std::optional<GeoCircle> DrawAlgorithms::CircleTanTanTan(
    const GeoLine& line1,
    const GeoLine& line2,
    const GeoLine& line3,
    const GeoPoint& pickNear)
{
    TangentSupport first;
    first.kind = TangentSupport::Kind::Line;
    first.line = line1;
    first.pick = pickNear;
    TangentSupport second;
    second.kind = TangentSupport::Kind::Line;
    second.line = line2;
    second.pick = pickNear;
    TangentSupport third;
    third.kind = TangentSupport::Kind::Line;
    third.line = line3;
    third.pick = pickNear;
    return CircleTanTanTan(first, second, third);
}

std::optional<GeoCircle> DrawAlgorithms::CircleTanTanTan(
    const TangentSupport& first,
    const TangentSupport& second,
    const TangentSupport& third)
{
    double bestScore = std::numeric_limits<double>::max();
    std::optional<GeoCircle> best;
    SolveTanTanTan(first, second, third, bestScore, best);
    return best;
}

std::optional<GeoArc> DrawAlgorithms::ArcFromCenterStartEnd(
    const GeoPoint& center,
    const GeoPoint& start,
    const GeoPoint& end)
{
    const double radius = Distance2D(center, start);
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    const double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    double endAngle = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    if (endAngle <= startAngle) {
        endAngle += 2.0 * kPi;
    }
    return GeoArc(center, radius, startAngle, endAngle);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartCenterEnd(
    const GeoPoint& start,
    const GeoPoint& center,
    const GeoPoint& end)
{
    return ArcFromCenterStartEnd(center, start, end);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartEndRadius(
    const GeoPoint& start,
    const GeoPoint& end,
    double radius,
    bool largeArc,
    bool clockwise)
{
    const double chord = Distance2D(start, end);
    if (radius <= Tolerance::Model || chord > 2.0 * radius + Tolerance::Model) {
        return std::nullopt;
    }
    const GeoPoint mid((start.X() + end.X()) * 0.5, (start.Y() + end.Y()) * 0.5);
    const double h = std::sqrt(std::max(0.0, radius * radius - (chord * 0.5) * (chord * 0.5)));
    const GeoPoint dir(end.X() - start.X(), end.Y() - start.Y());
    const GeoPoint normal(-dir.Y(), dir.X());
    const GeoPoint unitNormal = Normalize(normal);
    // 圆心侧只由 clockwise 决定（光标相对弦的一侧）；largeArc 只改扫掠，不挪圆心。
    // 否则 Ctrl/优弧 会把圆心翻到对侧，扫掠仍走短弧，看起来永远是劣弧。
    const double sign = clockwise ? -1.0 : 1.0;
    const GeoPoint center(
        mid.X() + unitNormal.X() * h * sign,
        mid.Y() + unitNormal.Y() * h * sign);
    double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    double endAngle = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    bool sweepClockwise = clockwise;
    if (largeArc) {
        sweepClockwise = !sweepClockwise;
    }
    if (sweepClockwise && endAngle > startAngle) {
        endAngle -= 2.0 * kPi;
    } else if (!sweepClockwise && endAngle < startAngle) {
        endAngle += 2.0 * kPi;
    }
    return GeoArc(center, radius, startAngle, endAngle);
}

GeoPoint DrawAlgorithms::ArcPointAt(const GeoPoint& center, double radius, double angleRadians)
{
    return GeoPoint(
        center.X() + std::cos(angleRadians) * radius,
        center.Y() + std::sin(angleRadians) * radius);
}

double DrawAlgorithms::ToPositiveAngleDegrees(double angleDegrees)
{
    if (!std::isfinite(angleDegrees)) {
        return 0.0;
    }
    double degrees = std::fmod(angleDegrees, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartCenterAngle(
    const GeoPoint& start,
    const GeoPoint& center,
    double angleDegrees)
{
    const double radius = Distance2D(center, start);
    // 负角 = 顺时针；幅值换算到 (0, 360)，方向由符号保留。
    const bool clockwise = angleDegrees < 0.0;
    const double sweepDeg = ToPositiveAngleDegrees(std::abs(angleDegrees));
    if (radius <= Tolerance::Model || sweepDeg <= Tolerance::Model
        || sweepDeg >= 360.0 - Tolerance::Model) {
        return std::nullopt;
    }
    const double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    const double sweepRadians = sweepDeg * kPi / 180.0 * (clockwise ? -1.0 : 1.0);
    return GeoArc(center, radius, startAngle, startAngle + sweepRadians);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartCenterChord(
    const GeoPoint& start,
    const GeoPoint& center,
    double chordLength,
    const GeoPoint& sidePick,
    bool ctrlFlip)
{
    const double radius = Distance2D(center, start);
    const double chord = std::abs(chordLength);
    // 弦长必须落在 (0, 2R]；等于直径时为半圆。
    if (radius <= Tolerance::Model || chord <= Tolerance::Model
        || chord > 2.0 * radius + Tolerance::Model) {
        return std::nullopt;
    }

    const double halfCentral = std::asin(std::clamp(chord / (2.0 * radius), -1.0, 1.0));
    const double alpha = 2.0 * halfCentral; // 劣弧圆心角 α ∈ (0, π]

    const double csx = start.X() - center.X();
    const double csy = start.Y() - center.Y();
    (void)sidePick;
    // 第三步鼠标只提供弦长幅值，不参与选择圆上端点侧。
    // 固定采用 AutoCAD 图3所示的 +α 圆弧侧；第三点无论向上或向下，
    // 都保持相同端点侧和圆弧方向。
    constexpr double sideSign = 1.0;
    double sweep = sideSign * alpha;

    // AutoCAD：正弦长 = 该侧劣弧；负弦长 / Ctrl = 同一端点改走优弧（长扫掠）。
    // 按住 Ctrl 拖动时预览接近整圆，而不是把劣弧简单改成顺时针短弧。
    bool major = chordLength < 0.0;
    if (ctrlFlip) {
        major = !major;
    }
    if (major) {
        sweep = (sweep > 0.0) ? (sweep - 2.0 * kPi) : (sweep + 2.0 * kPi);
    }

    const double startAngle = std::atan2(csy, csx);
    return GeoArc(center, radius, startAngle, startAngle + sweep);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartEndAngle(
    const GeoPoint& start,
    const GeoPoint& end,
    double angleDegrees,
    const GeoPoint& pickNear)
{
    const double chord = Distance2D(start, end);
    // 负角仅表示顺时针；幅值一律换算为正数后再构造。
    const bool ccw = angleDegrees >= 0.0;
    const double sweepDeg = ToPositiveAngleDegrees(std::abs(angleDegrees));
    if (chord <= Tolerance::Model || sweepDeg <= Tolerance::Model) {
        return std::nullopt;
    }
    const double angleRad = sweepDeg * kPi / 180.0;
    if (angleRad >= 2.0 * kPi - Tolerance::Model) {
        return std::nullopt;
    }
    const double radius = chord / (2.0 * std::sin(angleRad * 0.5));
    const GeoPoint mid((start.X() + end.X()) * 0.5, (start.Y() + end.Y()) * 0.5);
    const GeoPoint dir(end.X() - start.X(), end.Y() - start.Y());
    const GeoPoint unitNormal = Normalize(GeoPoint(-dir.Y(), dir.X()));
    const double h = std::sqrt(std::max(0.0, radius * radius - (chord * 0.5) * (chord * 0.5)));
    const GeoPoint leftCenter(mid.X() + unitNormal.X() * h, mid.Y() + unitNormal.Y() * h);
    const GeoPoint rightCenter(mid.X() - unitNormal.X() * h, mid.Y() - unitNormal.Y() * h);
    // AutoCAD SEA：输入包含角 θ = 实线弧的圆心角（正=逆时针）。
    // 两侧圆心分别对应劣弧 α 与优弧 (360°-α)；θ>180° 时必须取优弧侧。
    // Ctrl 切换方向在引擎层转为 (360°-θ)，此处只按传入的圆心角选侧。
    (void)pickNear;
    const bool wantMajor = sweepDeg > 180.0;
    const GeoPoint& center = (ccw != wantMajor) ? leftCenter : rightCenter;

    double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    double endAngle = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    if (ccw) {
        if (endAngle <= startAngle) {
            endAngle += 2.0 * kPi;
        }
    } else if (endAngle >= startAngle) {
        endAngle -= 2.0 * kPi;
    }
    return GeoArc(center, radius, startAngle, endAngle);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromTangentStartEnd(
    const GeoPoint& start,
    const GeoPoint& tangentUnit,
    const GeoPoint& end)
{
    const double tangentLength = std::hypot(tangentUnit.X(), tangentUnit.Y());
    if (tangentLength <= Tolerance::Model) {
        return std::nullopt;
    }
    const GeoPoint tangent(
        tangentUnit.X() / tangentLength,
        tangentUnit.Y() / tangentLength);
    const GeoPoint leftNormal(-tangent.Y(), tangent.X());
    const GeoPoint delta(end.X() - start.X(), end.Y() - start.Y());
    const double denominator =
        2.0 * (leftNormal.X() * delta.X() + leftNormal.Y() * delta.Y());
    if (std::abs(denominator) <= Tolerance::Model) {
        return std::nullopt;
    }

    // |end - (start + lambda*n)|² = lambda²
    // => lambda = |end-start|² / (2*n·(end-start)).
    // lambda > 0: center is on the left, sweep CCW.
    // lambda < 0: center is on the right, sweep CW.
    const double signedRadius =
        (delta.X() * delta.X() + delta.Y() * delta.Y()) / denominator;
    const double radius = std::abs(signedRadius);
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }

    const GeoPoint center(
        start.X() + signedRadius * leftNormal.X(),
        start.Y() + signedRadius * leftNormal.Y());
    const double startAngle =
        std::atan2(start.Y() - center.Y(), start.X() - center.X());
    double endAngle =
        std::atan2(end.Y() - center.Y(), end.X() - center.X());
    if (signedRadius > 0.0) {
        if (endAngle <= startAngle) {
            endAngle += 2.0 * kPi;
        }
    } else if (endAngle >= startAngle) {
        endAngle -= 2.0 * kPi;
    }
    return GeoArc(center, radius, startAngle, endAngle);
}

double DrawAlgorithms::BulgeFromArc(const GeoArc& arc)
{
    double sweep = arc.EndAngleRadians() - arc.StartAngleRadians();
    constexpr double kPi = 3.14159265358979323846;
    while (sweep <= -kPi) {
        sweep += 2.0 * kPi;
    }
    while (sweep > kPi) {
        sweep -= 2.0 * kPi;
    }
    return std::tan(sweep * 0.25);
}

std::optional<GeoArc> DrawAlgorithms::ArcFromBulge(
    const GeoPoint& start,
    const GeoPoint& end,
    double bulge)
{
    if (std::abs(bulge) <= 1.0e-12) {
        return std::nullopt;
    }
    const double dx = end.X() - start.X();
    const double dy = end.Y() - start.Y();
    const double chordLen = std::hypot(dx, dy);
    if (chordLen <= Tolerance::Model) {
        return std::nullopt;
    }
    const double included = 4.0 * std::atan(bulge);
    const double radius = chordLen * (1.0 + bulge * bulge) / (4.0 * std::abs(bulge));
    if (radius <= Tolerance::Model) {
        return std::nullopt;
    }
    const double half = chordLen * 0.5;
    double offset = radius * radius - half * half;
    offset = offset > 0.0 ? std::sqrt(offset) : 0.0;
    if (std::abs(included) > kPi) {
        offset = -offset;
    }
    const GeoPoint leftNormal(-dy / chordLen, dx / chordLen);
    const double side = bulge > 0.0 ? 1.0 : -1.0;
    const GeoPoint mid((start.X() + end.X()) * 0.5, (start.Y() + end.Y()) * 0.5);
    const GeoPoint center(
        mid.X() + leftNormal.X() * offset * side,
        mid.Y() + leftNormal.Y() * offset * side);
    const double startAngle = std::atan2(start.Y() - center.Y(), start.X() - center.X());
    double endAngle = std::atan2(end.Y() - center.Y(), end.X() - center.X());
    if (bulge > 0.0) {
        if (endAngle <= startAngle) {
            endAngle += 2.0 * kPi;
        }
    } else if (endAngle >= startAngle) {
        endAngle -= 2.0 * kPi;
    }
    return GeoArc(center, radius, startAngle, endAngle);
}

GeoPoint DrawAlgorithms::ArcEndTangentUnit(const GeoArc& arc)
{
    const GeoPoint end = ArcPointAt(arc.Center(), arc.Radius(), arc.EndAngleRadians());
    const GeoPoint radial = Normalize(GeoPoint(
        end.X() - arc.Center().X(),
        end.Y() - arc.Center().Y()));
    const double span = arc.EndAngleRadians() - arc.StartAngleRadians();
    const bool ccw = std::abs(span) <= Tolerance::Angular || span > 0.0;
    return ccw ? GeoPoint(-radial.Y(), radial.X()) : GeoPoint(radial.Y(), -radial.X());
}

GeoPoint DrawAlgorithms::PolylineEndTangentUnit(
    const std::vector<GeoPoint>& points,
    const std::vector<double>& bulges,
    const GeoPoint* fallbackTangent)
{
    if (points.size() >= 2) {
        const std::size_t prev = points.size() - 2;
        const double bulge = prev < bulges.size() ? bulges[prev] : 0.0;
        if (auto arc = ArcFromBulge(points[prev], points.back(), bulge)) {
            return ArcEndTangentUnit(*arc);
        }
        return Normalize(GeoPoint(
            points.back().X() - points[prev].X(),
            points.back().Y() - points[prev].Y()));
    }
    if (fallbackTangent) {
        return Normalize(*fallbackTangent);
    }
    return GeoPoint(1.0, 0.0);
}

void DrawAlgorithms::AppendPolylineGeometry(
    GeoShape& shape,
    const std::vector<GeoPoint>& points,
    const std::vector<double>& bulges,
    bool closed)
{
    if (points.size() < 2) {
        if (!points.empty()) {
            shape.AddPoint(points.front());
        }
        return;
    }
    const std::size_t count = points.size();
    const std::size_t segments = closed ? count : count - 1;
    bool allLines = true;
    for (std::size_t i = 0; i < segments; ++i) {
        const double bulge = i < bulges.size() ? bulges[i] : 0.0;
        if (std::abs(bulge) > 1.0e-12) {
            allLines = false;
            break;
        }
    }
    if (allLines) {
        shape.AddPolyline(points, closed);
        return;
    }
    for (std::size_t i = 0; i < segments; ++i) {
        const GeoPoint& start = points[i];
        const GeoPoint& end = points[(i + 1) % count];
        const double bulge = i < bulges.size() ? bulges[i] : 0.0;
        if (auto arc = ArcFromBulge(start, end, bulge)) {
            shape.AddArc(*arc);
        } else {
            shape.AddSegment(start, end);
        }
    }
}

std::optional<GeoArc> DrawAlgorithms::ArcFromStartEndDirection(
    const GeoPoint& start,
    const GeoPoint& end,
    const GeoPoint& directionPoint)
{
    const GeoPoint tangent = Normalize(GeoPoint(directionPoint.X() - start.X(), directionPoint.Y() - start.Y()));
    if (auto arc = ArcFromTangentStartEnd(start, tangent, end)) {
        return arc;
    }
    return std::nullopt;
}

std::vector<GeoPoint> DrawAlgorithms::RegularPolygonPoints(
    const GeoPoint& center,
    double radius,
    int sides,
    double rotationRadians)
{
    std::vector<GeoPoint> points;
    if (sides < 3 || radius <= Tolerance::Model) {
        return points;
    }
    points.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double angle = rotationRadians + (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(sides);
        points.emplace_back(center.X() + std::cos(angle) * radius, center.Y() + std::sin(angle) * radius);
    }
    return points;
}

void DrawAlgorithms::RegularPolygonFromCenterPick(
    const GeoPoint& center,
    const GeoPoint& pick,
    int sides,
    bool circumscribed,
    double& vertexRadius,
    double& rotationRadians)
{
    const double radius = Distance2D(center, pick);
    const double angle = std::atan2(pick.Y() - center.Y(), pick.X() - center.X());
    if (!circumscribed || sides < 3) {
        vertexRadius = radius;
        rotationRadians = angle;
        return;
    }
    const double half = kPi / static_cast<double>(sides);
    const double cosine = std::cos(half);
    vertexRadius = (std::abs(cosine) <= Tolerance::Model) ? radius : (radius / cosine);
    rotationRadians = angle + half;
}

bool DrawAlgorithms::RegularPolygonFromEdge(
    const GeoPoint& edgeStart,
    const GeoPoint& edgeEnd,
    int sides,
    GeoPoint& center,
    double& vertexRadius,
    double& rotationRadians)
{
    if (sides < 3) {
        return false;
    }
    const double dx = edgeEnd.X() - edgeStart.X();
    const double dy = edgeEnd.Y() - edgeStart.Y();
    const double sideLen = std::hypot(dx, dy);
    if (sideLen <= Tolerance::Model) {
        return false;
    }
    const double ux = dx / sideLen;
    const double uy = dy / sideLen;
    const double half = kPi / static_cast<double>(sides);
    const double tangent = std::tan(half);
    if (std::abs(tangent) <= Tolerance::Model) {
        return false;
    }
    const double apothem = (sideLen * 0.5) / tangent;
    const GeoPoint mid((edgeStart.X() + edgeEnd.X()) * 0.5, (edgeStart.Y() + edgeEnd.Y()) * 0.5);
    center = GeoPoint(mid.X() - uy * apothem, mid.Y() + ux * apothem);
    vertexRadius = Distance2D(center, edgeStart);
    rotationRadians = std::atan2(edgeStart.Y() - center.Y(), edgeStart.X() - center.X());
    return vertexRadius > Tolerance::Model;
}

std::vector<GeoPoint> DrawAlgorithms::RectanglePoints(const GeoPoint& corner1, const GeoPoint& corner2)
{
    return {
        corner1,
        GeoPoint(corner2.X(), corner1.Y()),
        corner2,
        GeoPoint(corner1.X(), corner2.Y()),
    };
}

std::vector<GeoPoint> DrawAlgorithms::RectangleFromCenter(const GeoPoint& center, const GeoPoint& corner)
{
    const double halfWidth = std::abs(corner.X() - center.X());
    const double halfHeight = std::abs(corner.Y() - center.Y());
    return {
        GeoPoint(center.X() - halfWidth, center.Y() - halfHeight),
        GeoPoint(center.X() + halfWidth, center.Y() - halfHeight),
        GeoPoint(center.X() + halfWidth, center.Y() + halfHeight),
        GeoPoint(center.X() - halfWidth, center.Y() + halfHeight),
    };
}

GeoShapeEllipse DrawAlgorithms::EllipseFromCenterAxis(
    const GeoPoint& center,
    const GeoPoint& majorAxisEnd,
    const GeoPoint& minorAxisPoint)
{
    GeoShapeEllipse ellipse;
    ellipse.center = center;
    ellipse.majorRadius = Distance2D(center, majorAxisEnd);
    ellipse.rotationRadians = std::atan2(majorAxisEnd.Y() - center.Y(), majorAxisEnd.X() - center.X());
    const GeoPoint majorDir(std::cos(ellipse.rotationRadians), std::sin(ellipse.rotationRadians));
    const GeoPoint minorDir(-majorDir.Y(), majorDir.X());
    const GeoPoint rel(minorAxisPoint.X() - center.X(), minorAxisPoint.Y() - center.Y());
    ellipse.minorRadius = std::abs(rel.X() * minorDir.X() + rel.Y() * minorDir.Y());
    return ellipse;
}

GeoShapeEllipse DrawAlgorithms::EllipseFromAxisEndpoints(
    const GeoPoint& majorStart,
    const GeoPoint& majorEnd,
    const GeoPoint& minorPoint)
{
    const GeoPoint center((majorStart.X() + majorEnd.X()) * 0.5, (majorStart.Y() + majorEnd.Y()) * 0.5);
    return EllipseFromCenterAxis(center, majorEnd, minorPoint);
}

std::vector<GeoPoint> DrawAlgorithms::DiscretizeParabola(
    const GeoPoint& vertex,
    const GeoPoint& focus,
    int segments,
    double extent)
{
    std::vector<GeoPoint> points;
    const GeoPoint axisDir = Normalize(GeoPoint(focus.X() - vertex.X(), focus.Y() - vertex.Y()));
    const GeoPoint directrixNormal(-axisDir.Y(), axisDir.X());
    const double p = Distance2D(vertex, focus) * 0.5;
    if (p <= Tolerance::Model) {
        points.push_back(vertex);
        return points;
    }
    points.reserve(static_cast<std::size_t>(segments * 2 + 1));
    for (int i = -segments; i <= segments; ++i) {
        const double t = (static_cast<double>(i) / static_cast<double>(segments)) * extent;
        const GeoPoint base(vertex.X() + axisDir.X() * t, vertex.Y() + axisDir.Y() * t);
        const double yLocal = (t * t) / (4.0 * p);
        points.emplace_back(
            base.X() + directrixNormal.X() * yLocal,
            base.Y() + directrixNormal.Y() * yLocal);
    }
    return points;
}

std::vector<GeoPoint> DrawAlgorithms::DiscretizeHyperbola(
    const GeoPoint& center,
    const GeoPoint& vertex,
    double semiMinor,
    int segments,
    double extent)
{
    std::vector<GeoPoint> points;
    const GeoPoint majorDir = Normalize(GeoPoint(vertex.X() - center.X(), vertex.Y() - center.Y()));
    const double a = Distance2D(center, vertex);
    const double b = semiMinor;
    if (a <= Tolerance::Model || b <= Tolerance::Model) {
        return points;
    }
    points.reserve(static_cast<std::size_t>(segments * 4));
    for (int branch = -1; branch <= 1; branch += 2) {
        for (int i = 0; i <= segments; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segments);
            const double x = a + t * extent;
            const double y = b * std::sqrt(std::max(0.0, (x * x) / (a * a) - 1.0));
            points.emplace_back(
                center.X() + majorDir.X() * x * branch - majorDir.Y() * y,
                center.Y() + majorDir.Y() * x * branch + majorDir.X() * y);
            if (i > 0) {
                points.emplace_back(
                    center.X() + majorDir.X() * x * branch + majorDir.Y() * y,
                    center.Y() + majorDir.Y() * x * branch - majorDir.X() * y);
            }
        }
    }
    return points;
}

namespace {

double PointToSegmentDistance(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return Distance2D(point, start);
    }
    const double t = std::clamp(((point.X() - start.X()) * vx + (point.Y() - start.Y()) * vy) / len2, 0.0, 1.0);
    return Distance2D(point, GeoPoint(start.X() + t * vx, start.Y() + t * vy));
}

void FlattenParametric(
    const std::function<GeoPoint(double)>& eval,
    double t0,
    double t1,
    const GeoPoint& p0,
    const GeoPoint& p1,
    double chordTol,
    int depth,
    std::vector<GeoPoint>& out)
{
    if (depth <= 0 || (t1 - t0) <= 1.0e-5) {
        out.push_back(p1);
        return;
    }
    const double tm = 0.5 * (t0 + t1);
    const GeoPoint pm = eval(tm);
    if (PointToSegmentDistance(pm, p0, p1) <= chordTol) {
        out.push_back(p1);
        return;
    }
    FlattenParametric(eval, t0, tm, p0, pm, chordTol, depth - 1, out);
    FlattenParametric(eval, tm, t1, pm, p1, chordTol, depth - 1, out);
}

} // namespace

std::vector<GeoPoint> DrawAlgorithms::EvaluateBSpline(
    const std::vector<GeoPoint>& controlPoints,
    int degree,
    int samplesPerSpan,
    const std::vector<double>& knots)
{
    std::vector<GeoPoint> result;
    if (controlPoints.size() < 2) {
        return controlPoints;
    }
    if (static_cast<int>(controlPoints.size()) <= degree) {
        degree = static_cast<int>(controlPoints.size()) - 1;
    }
    if (degree < 1) {
        return controlPoints;
    }
    const std::vector<double> resolved = EffectiveKnots(static_cast<int>(controlPoints.size()), degree, knots);
    const int sampleCount = std::max(2, samplesPerSpan * (static_cast<int>(controlPoints.size()) - degree));
    result.reserve(static_cast<std::size_t>(sampleCount + 1));
    for (int i = 0; i <= sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCount);
        result.push_back(DeBoor(t, degree, controlPoints, resolved));
    }
    return result;
}

std::vector<GeoPoint> DrawAlgorithms::EvaluateFitPointSpline(
    const std::vector<GeoPoint>& fitPoints,
    int samplesPerSegment)
{
    const std::size_t n = fitPoints.size();
    if (n < 2) {
        return fitPoints;
    }
    if (n == 2) {
        return fitPoints;
    }
    if (samplesPerSegment < 2) {
        samplesPerSegment = 2;
    }

    std::vector<double> t(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        t[i] = t[i - 1] + Distance2D(fitPoints[i - 1], fitPoints[i]);
    }
    if (t.back() <= Tolerance::Model) {
        return fitPoints;
    }
    for (std::size_t i = 1; i < n; ++i) {
        t[i] /= t.back();
    }

    std::vector<double> h(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = t[i + 1] - t[i];
        if (h[i] <= Tolerance::Model) {
            h[i] = Tolerance::Model;
        }
    }

    auto solveNaturalCubic = [&](const std::vector<double>& values) {
        std::vector<double> alpha(n - 1, 0.0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            alpha[i] = (3.0 / h[i]) * (values[i + 1] - values[i]) - (3.0 / h[i - 1]) * (values[i] - values[i - 1]);
        }
        std::vector<double> l(n, 0.0);
        std::vector<double> mu(n, 0.0);
        std::vector<double> z(n, 0.0);
        l[0] = 1.0;
        for (std::size_t i = 1; i + 1 < n; ++i) {
            l[i] = 2.0 * (t[i + 1] - t[i - 1]) - h[i - 1] * mu[i - 1];
            mu[i] = h[i] / l[i];
            z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
        }
        l[n - 1] = 1.0;
        std::vector<double> c(n, 0.0);
        for (int j = static_cast<int>(n) - 2; j >= 0; --j) {
            c[static_cast<std::size_t>(j)] = z[static_cast<std::size_t>(j)] - mu[static_cast<std::size_t>(j)] * c[static_cast<std::size_t>(j + 1)];
        }
        std::vector<double> b(n - 1);
        std::vector<double> d(n - 1);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            b[i] = (values[i + 1] - values[i]) / h[i] - h[i] * (c[i + 1] + 2.0 * c[i]) / 3.0;
            d[i] = (c[i + 1] - c[i]) / (3.0 * h[i]);
        }
        return std::make_tuple(c, b, d);
    };

    std::vector<double> xs(n);
    std::vector<double> ys(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = fitPoints[i].X();
        ys[i] = fitPoints[i].Y();
    }
    const auto [cx, bx, dx] = solveNaturalCubic(xs);
    const auto [cy, by, dy] = solveNaturalCubic(ys);

    std::vector<GeoPoint> result;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        for (int s = 0; s <= samplesPerSegment; ++s) {
            if (i > 0 && s == 0) {
                continue;
            }
            const double u = static_cast<double>(s) / static_cast<double>(samplesPerSegment);
            const double dt = u * h[i];
            const double x = xs[i] + bx[i] * dt + cx[i] * dt * dt + dx[i] * dt * dt * dt;
            const double y = ys[i] + by[i] * dt + cy[i] * dt * dt + dy[i] * dt * dt * dt;
            result.emplace_back(x, y);
        }
    }
    return result;
}

std::vector<GeoPoint> DrawAlgorithms::FlattenBSpline(
    const std::vector<GeoPoint>& controlPoints,
    int degree,
    double chordToleranceModel,
    const std::vector<double>& knots)
{
    if (controlPoints.size() < 2) {
        return controlPoints;
    }
    if (static_cast<int>(controlPoints.size()) <= degree) {
        degree = static_cast<int>(controlPoints.size()) - 1;
    }
    if (degree < 1) {
        return controlPoints;
    }
    if (!(chordToleranceModel > 0.0) || !std::isfinite(chordToleranceModel)) {
        return EvaluateBSpline(controlPoints, degree, 64, knots);
    }

    const std::vector<double> resolved = EffectiveKnots(static_cast<int>(controlPoints.size()), degree, knots);
    auto eval = [&](double t) {
        const double clamped = std::clamp(t, 0.0, 1.0);
        return DeBoor(clamped, degree, controlPoints, resolved);
    };

    const std::vector<double> spans = UniqueKnotSpans(resolved, degree, static_cast<int>(controlPoints.size()));
    std::vector<GeoPoint> result;
    result.push_back(eval(spans.front()));
    for (std::size_t i = 1; i < spans.size(); ++i) {
        FlattenParametric(eval, spans[i - 1], spans[i], eval(spans[i - 1]), eval(spans[i]), chordToleranceModel, 14, result);
    }
    return result;
}

std::vector<GeoPoint> DrawAlgorithms::FlattenFitPointSpline(
    const std::vector<GeoPoint>& fitPoints,
    double chordToleranceModel)
{
    const std::size_t n = fitPoints.size();
    if (n < 2) {
        return fitPoints;
    }
    if (n == 2) {
        return fitPoints;
    }
    if (!(chordToleranceModel > 0.0) || !std::isfinite(chordToleranceModel)) {
        return EvaluateFitPointSpline(fitPoints, 64);
    }

    // Evaluate the cubic directly: rebuild the same coefficients.
    std::vector<double> t(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        t[i] = t[i - 1] + Distance2D(fitPoints[i - 1], fitPoints[i]);
    }
    if (t.back() <= Tolerance::Model) {
        return fitPoints;
    }
    for (std::size_t i = 1; i < n; ++i) {
        t[i] /= t.back();
    }
    std::vector<double> h(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = t[i + 1] - t[i];
        if (h[i] <= Tolerance::Model) {
            h[i] = Tolerance::Model;
        }
    }

    auto solveNaturalCubic = [&](const std::vector<double>& values) {
        std::vector<double> alpha(n - 1, 0.0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            alpha[i] = (3.0 / h[i]) * (values[i + 1] - values[i]) - (3.0 / h[i - 1]) * (values[i] - values[i - 1]);
        }
        std::vector<double> l(n, 0.0);
        std::vector<double> mu(n, 0.0);
        std::vector<double> z(n, 0.0);
        l[0] = 1.0;
        for (std::size_t i = 1; i + 1 < n; ++i) {
            l[i] = 2.0 * (t[i + 1] - t[i - 1]) - h[i - 1] * mu[i - 1];
            mu[i] = h[i] / l[i];
            z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
        }
        l[n - 1] = 1.0;
        std::vector<double> c(n, 0.0);
        for (int j = static_cast<int>(n) - 2; j >= 0; --j) {
            c[static_cast<std::size_t>(j)] = z[static_cast<std::size_t>(j)] - mu[static_cast<std::size_t>(j)] * c[static_cast<std::size_t>(j + 1)];
        }
        std::vector<double> b(n - 1);
        std::vector<double> d(n - 1);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            b[i] = (values[i + 1] - values[i]) / h[i] - h[i] * (c[i + 1] + 2.0 * c[i]) / 3.0;
            d[i] = (c[i + 1] - c[i]) / (3.0 * h[i]);
        }
        return std::make_tuple(c, b, d);
    };

    std::vector<double> xs(n);
    std::vector<double> ys(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = fitPoints[i].X();
        ys[i] = fitPoints[i].Y();
    }
    const auto [cx, bx, dx] = solveNaturalCubic(xs);
    const auto [cy, by, dy] = solveNaturalCubic(ys);

    auto evalOnSpan = [&](std::size_t i, double u) {
        const double dt = std::clamp(u, 0.0, 1.0) * h[i];
        return GeoPoint(
            xs[i] + bx[i] * dt + cx[i] * dt * dt + dx[i] * dt * dt * dt,
            ys[i] + by[i] * dt + cy[i] * dt * dt + dy[i] * dt * dt * dt);
    };

    std::vector<GeoPoint> result;
    result.push_back(fitPoints.front());
    for (std::size_t i = 0; i + 1 < n; ++i) {
        auto eval = [&](double u) { return evalOnSpan(i, u); };
        FlattenParametric(eval, 0.0, 1.0, eval(0.0), eval(1.0), chordToleranceModel, 14, result);
    }
    return result;
}

std::vector<GeoPoint> DrawAlgorithms::SplineDisplayPoints(
    const std::vector<GeoPoint>& points,
    SplineMode mode,
    int degree,
    double chordToleranceModel,
    const std::vector<double>& knots)
{
    if (mode == SplineMode::FitPoints) {
        return FlattenFitPointSpline(points, chordToleranceModel);
    }
    return FlattenBSpline(points, degree, chordToleranceModel, knots);
}

std::vector<GeoPoint> DrawAlgorithms::SplinePreviewPoints(
    const std::vector<GeoPoint>& points,
    SplineMode mode,
    int degree,
    const std::vector<double>& knots)
{
    if (mode == SplineMode::FitPoints) {
        return EvaluateFitPointSpline(points, 64);
    }
    return EvaluateBSpline(points, degree, 64, knots);
}

namespace {

DrawAlgorithms::CubicBezierSegment LineAsBezier(const GeoPoint& start, const GeoPoint& end)
{
    DrawAlgorithms::CubicBezierSegment seg;
    seg.p0 = start;
    seg.p1 = GeoPoint(
        start.X() + (end.X() - start.X()) / 3.0,
        start.Y() + (end.Y() - start.Y()) / 3.0);
    seg.p2 = GeoPoint(
        start.X() + 2.0 * (end.X() - start.X()) / 3.0,
        start.Y() + 2.0 * (end.Y() - start.Y()) / 3.0);
    seg.p3 = end;
    return seg;
}

std::vector<DrawAlgorithms::CubicBezierSegment> FitPointsToBeziers(
    const std::vector<GeoPoint>& fitPoints,
    const GeoPoint* startTangent = nullptr,
    const GeoPoint* endTangent = nullptr)
{
    const std::size_t n = fitPoints.size();
    std::vector<DrawAlgorithms::CubicBezierSegment> result;
    if (n < 2) {
        return result;
    }
    if (n == 2) {
        result.push_back(LineAsBezier(fitPoints[0], fitPoints[1]));
        return result;
    }

    std::vector<double> t(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        t[i] = t[i - 1] + Distance2D(fitPoints[i - 1], fitPoints[i]);
    }
    if (t.back() <= Tolerance::Model) {
        result.push_back(LineAsBezier(fitPoints.front(), fitPoints.back()));
        return result;
    }
    for (std::size_t i = 1; i < n; ++i) {
        t[i] /= t.back();
    }
    std::vector<double> h(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = t[i + 1] - t[i];
        if (h[i] <= Tolerance::Model) {
            h[i] = Tolerance::Model;
        }
    }

    auto solveCubic = [&](const std::vector<double>& values, const double* startDeriv, const double* endDeriv) {
        std::vector<double> alpha(n - 1, 0.0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            alpha[i] = (3.0 / h[i]) * (values[i + 1] - values[i]) - (3.0 / h[i - 1]) * (values[i] - values[i - 1]);
        }
        std::vector<double> l(n, 0.0);
        std::vector<double> mu(n, 0.0);
        std::vector<double> z(n, 0.0);
        if (startDeriv) {
            const double rhs0 = 3.0 * ((values[1] - values[0]) / h[0] - *startDeriv) / h[0];
            l[0] = 2.0;
            mu[0] = 0.5;
            z[0] = rhs0 / 2.0;
        } else {
            l[0] = 1.0;
        }
        for (std::size_t i = 1; i + 1 < n; ++i) {
            l[i] = 2.0 * (t[i + 1] - t[i - 1]) - h[i - 1] * mu[i - 1];
            mu[i] = h[i] / l[i];
            z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
        }
        std::vector<double> c(n, 0.0);
        if (endDeriv) {
            const double rhsn = 3.0 * (*endDeriv - (values[n - 1] - values[n - 2]) / h[n - 2]) / h[n - 2];
            l[n - 1] = 2.0 - mu[n - 2];
            if (std::abs(l[n - 1]) > 1.0e-12) {
                c[n - 1] = (rhsn - z[n - 2]) / l[n - 1];
            }
        } else {
            l[n - 1] = 1.0;
        }
        for (int j = static_cast<int>(n) - 2; j >= 0; --j) {
            c[static_cast<std::size_t>(j)] = z[static_cast<std::size_t>(j)] - mu[static_cast<std::size_t>(j)] * c[static_cast<std::size_t>(j + 1)];
        }
        std::vector<double> b(n - 1);
        std::vector<double> d(n - 1);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            b[i] = (values[i + 1] - values[i]) / h[i] - h[i] * (c[i + 1] + 2.0 * c[i]) / 3.0;
            d[i] = (c[i + 1] - c[i]) / (3.0 * h[i]);
        }
        return std::make_tuple(c, b, d);
    };

    std::vector<double> xs(n);
    std::vector<double> ys(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = fitPoints[i].X();
        ys[i] = fitPoints[i].Y();
    }
    const double startDx = startTangent ? (startTangent->X() / h[0]) : 0.0;
    const double startDy = startTangent ? (startTangent->Y() / h[0]) : 0.0;
    const double endDx = endTangent ? (endTangent->X() / h[n - 2]) : 0.0;
    const double endDy = endTangent ? (endTangent->Y() / h[n - 2]) : 0.0;
    const auto [cx, bx, dx] = solveCubic(xs, startTangent ? &startDx : nullptr, endTangent ? &endDx : nullptr);
    const auto [cy, by, dy] = solveCubic(ys, startTangent ? &startDy : nullptr, endTangent ? &endDy : nullptr);

    result.reserve(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double hx = h[i];
        const double Bx = bx[i] * hx;
        const double Cx = cx[i] * hx * hx;
        const double Dx = dx[i] * hx * hx * hx;
        const double By = by[i] * hx;
        const double Cy = cy[i] * hx * hx;
        const double Dy = dy[i] * hx * hx * hx;
        DrawAlgorithms::CubicBezierSegment seg;
        seg.p0 = fitPoints[i];
        seg.p1 = GeoPoint(xs[i] + Bx / 3.0, ys[i] + By / 3.0);
        seg.p2 = GeoPoint(xs[i] + 2.0 * Bx / 3.0 + Cx / 3.0, ys[i] + 2.0 * By / 3.0 + Cy / 3.0);
        seg.p3 = fitPoints[i + 1];
        result.push_back(seg);
    }
    return result;
}

std::vector<DrawAlgorithms::CubicBezierSegment> ControlPointsToBeziers(
    const std::vector<GeoPoint>& controlPoints,
    int degree,
    const std::vector<double>& knots)
{
    std::vector<DrawAlgorithms::CubicBezierSegment> result;
    if (controlPoints.size() < 2) {
        return result;
    }
    if (static_cast<int>(controlPoints.size()) <= degree) {
        degree = static_cast<int>(controlPoints.size()) - 1;
    }
    if (degree < 1) {
        return result;
    }
    if (degree == 1) {
        for (std::size_t i = 1; i < controlPoints.size(); ++i) {
            result.push_back(LineAsBezier(controlPoints[i - 1], controlPoints[i]));
        }
        return result;
    }

    const std::vector<double> resolved = EffectiveKnots(static_cast<int>(controlPoints.size()), degree, knots);
    auto eval = [&](double t) {
        return DeBoor(std::clamp(t, 0.0, 1.0), degree, controlPoints, resolved);
    };

    const std::vector<double> spans = UniqueKnotSpans(resolved, degree, static_cast<int>(controlPoints.size()));
    for (std::size_t i = 1; i < spans.size(); ++i) {
        const double t0 = spans[i - 1];
        const double t1 = spans[i];
        const double dt = t1 - t0;
        if (dt <= Tolerance::Model) {
            continue;
        }
        const GeoPoint p0 = eval(t0);
        const GeoPoint p3 = eval(t1);
        const double eps = std::max(dt * 1.0e-4, 1.0e-8);
        const GeoPoint d0pt = eval(std::min(t0 + eps, t1));
        const GeoPoint d1pt = eval(std::max(t1 - eps, t0));
        const GeoPoint d0((d0pt.X() - p0.X()) / eps, (d0pt.Y() - p0.Y()) / eps);
        const GeoPoint d1((p3.X() - d1pt.X()) / eps, (p3.Y() - d1pt.Y()) / eps);
        DrawAlgorithms::CubicBezierSegment seg;
        seg.p0 = p0;
        seg.p1 = GeoPoint(p0.X() + d0.X() * dt / 3.0, p0.Y() + d0.Y() * dt / 3.0);
        seg.p2 = GeoPoint(p3.X() - d1.X() * dt / 3.0, p3.Y() - d1.Y() * dt / 3.0);
        seg.p3 = p3;
        result.push_back(seg);
    }
    return result;
}

} // namespace

std::vector<DrawAlgorithms::CubicBezierSegment> DrawAlgorithms::SplineCubicBeziers(
    const std::vector<GeoPoint>& points,
    SplineMode mode,
    int degree,
    const std::vector<double>& knots,
    const GeoPoint* startTangent,
    const GeoPoint* endTangent)
{
    if (mode == SplineMode::FitPoints) {
        return FitPointsToBeziers(points, startTangent, endTangent);
    }
    return ControlPointsToBeziers(points, degree, knots);
}

namespace {

bool SolveLinearSystem(std::vector<std::vector<double>> matrix, std::vector<double> rhs, std::vector<double>& unknown)
{
    const int n = static_cast<int>(rhs.size());
    if (n <= 0 || static_cast<int>(matrix.size()) != n) {
        return false;
    }
    unknown.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        int pivot = i;
        for (int row = i + 1; row < n; ++row) {
            if (std::abs(matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(i)])
                > std::abs(matrix[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(i)])) {
                pivot = row;
            }
        }
        if (std::abs(matrix[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(i)]) <= 1.0e-12) {
            return false;
        }
        if (pivot != i) {
            std::swap(matrix[static_cast<std::size_t>(i)], matrix[static_cast<std::size_t>(pivot)]);
            std::swap(rhs[static_cast<std::size_t>(i)], rhs[static_cast<std::size_t>(pivot)]);
        }
        const double diag = matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
        for (int row = i + 1; row < n; ++row) {
            const double factor = matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(i)] / diag;
            for (int col = i; col < n; ++col) {
                matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] -=
                    factor * matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(col)];
            }
            rhs[static_cast<std::size_t>(row)] -= factor * rhs[static_cast<std::size_t>(i)];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        double sum = rhs[static_cast<std::size_t>(i)];
        for (int col = i + 1; col < n; ++col) {
            sum -= matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(col)]
                * unknown[static_cast<std::size_t>(col)];
        }
        const double diag = matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
        if (std::abs(diag) <= 1.0e-12) {
            return false;
        }
        unknown[static_cast<std::size_t>(i)] = sum / diag;
    }
    return true;
}

} // namespace

std::vector<GeoPoint> DrawAlgorithms::InterpolateBSplineControls(
    const std::vector<GeoPoint>& fitPoints,
    int degree,
    std::vector<double>* knotsOut)
{
    const int nFit = static_cast<int>(fitPoints.size());
    if (nFit < 2) {
        if (knotsOut) {
            knotsOut->clear();
        }
        return fitPoints;
    }

    const auto beziers = SplineCubicBeziers(fitPoints, SplineMode::FitPoints, 3);
    if (beziers.empty()) {
        if (knotsOut) {
            knotsOut->clear();
        }
        return fitPoints;
    }

    auto assignBezierKnots = [&]() {
        if (knotsOut) {
            *knotsOut = {0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0};
        }
    };

    if (nFit == 2 || beziers.size() == 1) {
        assignBezierKnots();
        return {beziers.front().p0, beziers.front().p1, beziers.front().p2, beziers.front().p3};
    }

    constexpr int kDegree = 3;
    (void)degree;

    std::vector<double> params(static_cast<std::size_t>(nFit), 0.0);
    for (int i = 1; i < nFit; ++i) {
        params[static_cast<std::size_t>(i)] =
            params[static_cast<std::size_t>(i - 1)]
            + Distance2D(fitPoints[static_cast<std::size_t>(i - 1)], fitPoints[static_cast<std::size_t>(i)]);
    }
    if (params.back() <= Tolerance::Model) {
        if (knotsOut) {
            knotsOut->clear();
        }
        return fitPoints;
    }
    for (int i = 1; i < nFit; ++i) {
        params[static_cast<std::size_t>(i)] /= params.back();
    }

    const int nCtrl = nFit + 2;
    std::vector<double> knots(static_cast<std::size_t>(nCtrl + kDegree + 1), 0.0);
    for (int i = 0; i <= kDegree; ++i) {
        knots[static_cast<std::size_t>(i)] = 0.0;
    }
    for (int i = 1; i <= nFit - 2; ++i) {
        knots[static_cast<std::size_t>(kDegree + i)] = params[static_cast<std::size_t>(i)];
    }
    for (int i = nCtrl; i < nCtrl + kDegree + 1; ++i) {
        knots[static_cast<std::size_t>(i)] = 1.0;
    }

    std::vector<GeoPoint> controls(static_cast<std::size_t>(nCtrl));
    controls.front() = beziers.front().p0;
    controls[1] = beziers.front().p1;
    controls[static_cast<std::size_t>(nCtrl - 2)] = beziers.back().p2;
    controls.back() = beziers.back().p3;

    const int unknownCount = nFit - 2;
    if (unknownCount <= 0) {
        if (knotsOut) {
            *knotsOut = std::move(knots);
        }
        return controls;
    }

    auto basisAt = [&](int cvIndex, double t) {
        std::vector<GeoPoint> basis(static_cast<std::size_t>(nCtrl), GeoPoint(0.0, 0.0));
        basis[static_cast<std::size_t>(cvIndex)] = GeoPoint(1.0, 0.0);
        return DeBoor(std::clamp(t, 0.0, 1.0), kDegree, basis, knots).X();
    };

    std::vector<std::vector<double>> matrix(
        static_cast<std::size_t>(unknownCount),
        std::vector<double>(static_cast<std::size_t>(unknownCount), 0.0));
    std::vector<double> rhsX(static_cast<std::size_t>(unknownCount), 0.0);
    std::vector<double> rhsY(static_cast<std::size_t>(unknownCount), 0.0);
    for (int row = 0; row < unknownCount; ++row) {
        const int fitIndex = row + 1;
        const double t = params[static_cast<std::size_t>(fitIndex)];
        double knownX = 0.0;
        double knownY = 0.0;
        for (int i = 0; i < nCtrl; ++i) {
            const double ni = basisAt(i, t);
            if (i >= 2 && i <= nCtrl - 3) {
                matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(i - 2)] = ni;
            } else {
                knownX += ni * controls[static_cast<std::size_t>(i)].X();
                knownY += ni * controls[static_cast<std::size_t>(i)].Y();
            }
        }
        rhsX[static_cast<std::size_t>(row)] = fitPoints[static_cast<std::size_t>(fitIndex)].X() - knownX;
        rhsY[static_cast<std::size_t>(row)] = fitPoints[static_cast<std::size_t>(fitIndex)].Y() - knownY;
    }

    std::vector<double> cx;
    std::vector<double> cy;
    if (!SolveLinearSystem(matrix, rhsX, cx) || !SolveLinearSystem(matrix, rhsY, cy)) {
        std::vector<GeoPoint> fallback;
        fallback.push_back(beziers.front().p0);
        fallback.push_back(beziers.front().p1);
        for (const auto& bezier : beziers) {
            fallback.push_back(bezier.p2);
        }
        fallback.push_back(beziers.back().p3);
        if (knotsOut) {
            knotsOut->clear();
        }
        return fallback;
    }

    for (int i = 0; i < unknownCount; ++i) {
        controls[static_cast<std::size_t>(i + 2)] =
            GeoPoint(cx[static_cast<std::size_t>(i)], cy[static_cast<std::size_t>(i)]);
    }
    if (knotsOut) {
        *knotsOut = std::move(knots);
    }
    return controls;
}

std::vector<GeoPoint> DrawAlgorithms::SampleBSpline(
    const std::vector<GeoPoint>& controlPoints,
    int degree,
    const std::vector<double>& knots)
{
    if (controlPoints.size() < 2) {
        return controlPoints;
    }
    if (static_cast<int>(controlPoints.size()) <= degree) {
        degree = static_cast<int>(controlPoints.size()) - 1;
    }
    if (degree < 1) {
        return controlPoints;
    }
    const std::vector<double> resolved = EffectiveKnots(static_cast<int>(controlPoints.size()), degree, knots);
    const int count = static_cast<int>(controlPoints.size());
    std::vector<GeoPoint> samples;
    samples.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        double t = 0.0;
        if (static_cast<int>(resolved.size()) >= i + degree + 1) {
            for (int j = 1; j <= degree; ++j) {
                t += resolved[static_cast<std::size_t>(i + j)];
            }
            t /= static_cast<double>(degree);
        } else {
            t = (count == 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
        }
        samples.push_back(DeBoor(std::clamp(t, 0.0, 1.0), degree, controlPoints, resolved));
    }
    return samples;
}

} // namespace cad
