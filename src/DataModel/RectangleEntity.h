#pragma once

#include "DataModel/Entity.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoPrimitives.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <memory>

namespace cad {

class RectangleEntity : public Entity {
public:
    RectangleEntity(EntityId id, GeoRectangle rectangle)
        : Entity(id, EntityType::Rectangle)
        , rectangle_(std::move(rectangle))
    {
    }

    const GeoRectangle& Rectangle() const { return rectangle_; }
    GeoRectangle& Rectangle() { return rectangle_; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<RectangleEntity>(Id(), rectangle_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { rectangle_.MoveBy(dx, dy); }
    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        rectangle_.SetCorner1(GeoTransform::MirrorPoint(rectangle_.Corner1(), axisStart, axisEnd));
        rectangle_.SetCorner2(GeoTransform::MirrorPoint(rectangle_.Corner2(), axisStart, axisEnd));
    }
    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        rectangle_.SetCorner1(GeoTransform::RotatePoint(rectangle_.Corner1(), center, angleRadians));
        rectangle_.SetCorner2(GeoTransform::RotatePoint(rectangle_.Corner2(), center, angleRadians));
    }
    void Scale(const GeoPoint& center, double factor) override
    {
        rectangle_.SetCorner1(GeoTransform::ScalePoint(rectangle_.Corner1(), center, factor));
        rectangle_.SetCorner2(GeoTransform::ScalePoint(rectangle_.Corner2(), center, factor));
    }

    std::vector<GripInfo> Grips() const override
    {
        const auto points = DrawAlgorithms::RectanglePoints(rectangle_.Corner1(), rectangle_.Corner2());
        std::vector<GripInfo> grips;
        for (std::size_t i = 0; i < points.size(); ++i) {
            grips.push_back({static_cast<int>(i), GripKind::Vertex, points[i]});
        }
        return grips;
    }

    void SetGrip(int index, const GeoPoint& position) override
    {
        // GeoRectangle is an axis-aligned box defined by two opposite corners.
        // Map the four displayed corner grips onto those two corners so the
        // rectangle stays a single closed shape (AutoCAD RECTANG behavior).
        const GeoPoint c1 = rectangle_.Corner1();
        const GeoPoint c2 = rectangle_.Corner2();
        switch (index) {
        case 0: // Corner1
            rectangle_.SetCorner1(position);
            break;
        case 1: // (c2.x, c1.y)
            rectangle_.SetCorner1(GeoPoint(c1.X(), position.Y()));
            rectangle_.SetCorner2(GeoPoint(position.X(), c2.Y()));
            break;
        case 2: // Corner2
            rectangle_.SetCorner2(position);
            break;
        case 3: // (c1.x, c2.y)
            rectangle_.SetCorner1(GeoPoint(position.X(), c1.Y()));
            rectangle_.SetCorner2(GeoPoint(c2.X(), position.Y()));
            break;
        default:
            break;
        }
    }

    GeoShape Shaper() const override
    {
        GeoShape shape;
        shape.AddPolyline(DrawAlgorithms::RectanglePoints(rectangle_.Corner1(), rectangle_.Corner2()), true);
        return shape;
    }

private:
    GeoRectangle rectangle_;
};

class PolygonEntity : public Entity {
public:
    PolygonEntity(EntityId id, GeoPolygon polygon)
        : Entity(id, EntityType::Polygon)
        , polygon_(std::move(polygon))
    {
    }

    const GeoPolygon& Polygon() const { return polygon_; }
    GeoPolygon& Polygon() { return polygon_; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<PolygonEntity>(Id(), polygon_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { polygon_.MoveBy(dx, dy); }
    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        polygon_.SetCenter(GeoTransform::MirrorPoint(polygon_.Center(), axisStart, axisEnd));
        polygon_.SetRotationRadians(-polygon_.RotationRadians());
    }
    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        polygon_.SetCenter(GeoTransform::RotatePoint(polygon_.Center(), center, angleRadians));
        polygon_.SetRotationRadians(polygon_.RotationRadians() + angleRadians);
    }
    void Scale(const GeoPoint& center, double factor) override
    {
        polygon_.SetCenter(GeoTransform::ScalePoint(polygon_.Center(), center, factor));
        polygon_.SetRadius(polygon_.Radius() * factor);
    }

    std::vector<GripInfo> Grips() const override
    {
        return {
            {0, GripKind::Center, polygon_.Center()},
            {1, GripKind::Radius, GeoPoint(
                polygon_.Center().X() + std::cos(polygon_.RotationRadians()) * polygon_.Radius(),
                polygon_.Center().Y() + std::sin(polygon_.RotationRadians()) * polygon_.Radius())},
        };
    }

    void SetGrip(int index, const GeoPoint& position) override
    {
        if (index == 0) {
            polygon_.SetCenter(position);
        } else if (index == 1) {
            polygon_.SetRadius(Distance2D(polygon_.Center(), position));
            polygon_.SetRotationRadians(std::atan2(position.Y() - polygon_.Center().Y(), position.X() - polygon_.Center().X()));
        }
    }

    GeoShape Shaper() const override
    {
        GeoShape shape;
        shape.AddPolyline(
            DrawAlgorithms::RegularPolygonPoints(
                polygon_.Center(),
                polygon_.Radius(),
                polygon_.Sides(),
                polygon_.RotationRadians()),
            true);
        return shape;
    }

private:
    GeoPolygon polygon_;
};

} // namespace cad
