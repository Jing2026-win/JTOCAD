#pragma once

#include "DataModel/Entity.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <cmath>
#include <memory>
#include <utility>

namespace cad {

class CircleEntity : public Entity {
public:
    CircleEntity(EntityId id, GeoCircle circle)
        : Entity(id, EntityType::Circle)
        , circle_(std::move(circle))
    {
    }

    const GeoCircle& Circle() const { return circle_; }
    GeoCircle& Circle() { return circle_; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<CircleEntity>(Id(), circle_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { circle_.MoveBy(dx, dy); }

    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        circle_.SetCenter(GeoTransform::MirrorPoint(circle_.Center(), axisStart, axisEnd));
    }

    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        circle_.SetCenter(GeoTransform::RotatePoint(circle_.Center(), center, angleRadians));
    }

    void Scale(const GeoPoint& center, double factor) override
    {
        circle_.SetCenter(GeoTransform::ScalePoint(circle_.Center(), center, factor));
        circle_.SetRadius(circle_.Radius() * factor);
    }

    std::vector<GripInfo> Grips() const override
    {
        return {
            {0, GripKind::Center, circle_.Center()},
            {1, GripKind::Radius, GeoPoint(circle_.Center().X() + circle_.Radius(), circle_.Center().Y())},
        };
    }

    void SetGrip(int index, const GeoPoint& position) override
    {
        if (index == 0) {
            circle_.SetCenter(position);
        } else if (index == 1) {
            circle_.SetRadius(Distance2D(circle_.Center(), position));
        }
    }

    GeoShape Shaper() const override
    {
        GeoShape shape;
        shape.AddCircle(circle_);
        return shape;
    }

private:
    GeoCircle circle_;
};

} // namespace cad
