#pragma once

#include "DataModel/Entity.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <cmath>
#include <memory>
#include <utility>

namespace cad {

class LineEntity : public Entity {
public:
    LineEntity(EntityId id, GeoLine line)
        : Entity(id, EntityType::Line)
        , line_(std::move(line))
    {
    }

    const GeoLine& Line() const { return line_; }
    GeoLine& Line() { return line_; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<LineEntity>(Id(), line_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { line_.MoveBy(dx, dy); }

    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        line_.SetStart(GeoTransform::MirrorPoint(line_.Start(), axisStart, axisEnd));
        line_.SetEnd(GeoTransform::MirrorPoint(line_.End(), axisStart, axisEnd));
    }

    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        line_.SetStart(GeoTransform::RotatePoint(line_.Start(), center, angleRadians));
        line_.SetEnd(GeoTransform::RotatePoint(line_.End(), center, angleRadians));
    }

    void Scale(const GeoPoint& center, double factor) override
    {
        line_.SetStart(GeoTransform::ScalePoint(line_.Start(), center, factor));
        line_.SetEnd(GeoTransform::ScalePoint(line_.End(), center, factor));
    }

    std::vector<GripInfo> Grips() const override
    {
        return {
            {0, GripKind::Vertex, line_.Start()},
            {1, GripKind::Vertex, line_.End()},
            {2, GripKind::MidPoint, GeoPoint((line_.Start().X() + line_.End().X()) * 0.5, (line_.Start().Y() + line_.End().Y()) * 0.5)},
        };
    }

    void SetGrip(int index, const GeoPoint& position) override
    {
        if (index == 0) {
            line_.SetStart(position);
        } else if (index == 1) {
            line_.SetEnd(position);
        } else if (index == 2) {
            const GeoPoint mid((line_.Start().X() + line_.End().X()) * 0.5, (line_.Start().Y() + line_.End().Y()) * 0.5);
            const double dx = position.X() - mid.X();
            const double dy = position.Y() - mid.Y();
            line_.SetStart(line_.Start().Translated(dx, dy));
            line_.SetEnd(line_.End().Translated(dx, dy));
        }
    }

    GeoShape Shaper() const override
    {
        GeoShape shape;
        shape.AddSegment(line_.Start(), line_.End());
        return shape;
    }

private:
    GeoLine line_;
};

} // namespace cad
