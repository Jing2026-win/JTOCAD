#include "Geometry/GeoSpline.h"

#include "Geometry/DrawAlgorithms.h"
#include <cstddef>

namespace cad {

GeoSpline::GeoSpline(std::vector<GeoPoint> points, SplineMode mode, int degree)
    : definitionMode_(mode)
    , displayMode_(mode)
    , degree_(degree)
{
    if (mode == SplineMode::FitPoints) {
        fitPoints_ = std::move(points);
    } else {
        controlPoints_ = std::move(points);
    }
}

void GeoSpline::ToggleDisplayMode()
{
    displayMode_ = (displayMode_ == SplineMode::FitPoints)
        ? SplineMode::ControlPoints
        : SplineMode::FitPoints;
}

void GeoSpline::SetFitPoint(std::size_t index, const GeoPoint& point)
{
    if (index >= fitPoints_.size()) {
        return;
    }
    fitPoints_[index] = point;
    definitionMode_ = SplineMode::FitPoints;
    RebuildFromFit();
}

void GeoSpline::SetControlPoint(std::size_t index, const GeoPoint& point)
{
    if (index >= controlPoints_.size()) {
        return;
    }
    controlPoints_[index] = point;
    definitionMode_ = SplineMode::ControlPoints;
    RebuildFromControls();
}

void GeoSpline::InsertFitPoint(std::size_t index, const GeoPoint& point)
{
    EnsureFitPoints();
    if (index > fitPoints_.size()) {
        index = fitPoints_.size();
    }
    fitPoints_.insert(fitPoints_.begin() + static_cast<std::ptrdiff_t>(index), point);
    definitionMode_ = SplineMode::FitPoints;
    RebuildFromFit();
}

void GeoSpline::RemoveFitPoint(std::size_t index)
{
    EnsureFitPoints();
    if (index >= fitPoints_.size() || fitPoints_.size() <= 2) {
        return;
    }
    fitPoints_.erase(fitPoints_.begin() + static_cast<std::ptrdiff_t>(index));
    definitionMode_ = SplineMode::FitPoints;
    RebuildFromFit();
}

void GeoSpline::InsertControlPoint(std::size_t index, const GeoPoint& point)
{
    EnsureControlPoints();
    if (index > controlPoints_.size()) {
        index = controlPoints_.size();
    }
    controlPoints_.insert(controlPoints_.begin() + static_cast<std::ptrdiff_t>(index), point);
    definitionMode_ = SplineMode::ControlPoints;
    knots_.clear();
    RebuildFromControls();
}

void GeoSpline::RemoveControlPoint(std::size_t index)
{
    EnsureControlPoints();
    if (index >= controlPoints_.size() || controlPoints_.size() <= 2) {
        return;
    }
    controlPoints_.erase(controlPoints_.begin() + static_cast<std::ptrdiff_t>(index));
    definitionMode_ = SplineMode::ControlPoints;
    knots_.clear();
    RebuildFromControls();
}

void GeoSpline::SetStartTangent(const std::optional<GeoPoint>& tangent)
{
    startTangent_ = tangent;
    if (tangent) {
        definitionMode_ = SplineMode::FitPoints;
        RebuildFromFit();
    }
}

void GeoSpline::SetEndTangent(const std::optional<GeoPoint>& tangent)
{
    endTangent_ = tangent;
    if (tangent) {
        definitionMode_ = SplineMode::FitPoints;
        RebuildFromFit();
    }
}

void GeoSpline::MoveBy(double dx, double dy)
{
    TransformPoints([&](const GeoPoint& point) { return point.Translated(dx, dy); });
}

void GeoSpline::EnsureFitPoints() const
{
    if (fitPoints_.empty() && !controlPoints_.empty()) {
        RebuildFromControls();
    }
}

void GeoSpline::EnsureControlPoints() const
{
    if (controlPoints_.empty() && !fitPoints_.empty()) {
        RebuildFromFit();
    }
}

void GeoSpline::RebuildFromFit() const
{
    controlPoints_ = DrawAlgorithms::InterpolateBSplineControls(fitPoints_, degree_, &knots_);
}

void GeoSpline::RebuildFromControls() const
{
    fitPoints_ = DrawAlgorithms::SampleBSpline(controlPoints_, degree_, knots_);
}

} // namespace cad
