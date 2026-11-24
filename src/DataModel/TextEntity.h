#pragma once

#include "Base/TextTypes.h"
#include "DataModel/Entity.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <memory>
#include <string>
#include <utility>

namespace cad {

class TextEntity : public Entity {
public:
    TextEntity(
        EntityId id,
        GeoPoint position,
        std::string text,
        double height = 12.0,
        double rotationRadians = 0.0,
        TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Left,
        TextVerticalAlign verticalAlign = TextVerticalAlign::Baseline,
        double widthFactor = 1.0,
        double obliqueAngleRadians = 0.0)
        : Entity(id, EntityType::Text)
        , position_(position)
        , text_(std::move(text))
        , height_(height)
        , rotationRadians_(rotationRadians)
        , horizontalAlign_(horizontalAlign)
        , verticalAlign_(verticalAlign)
        , widthFactor_(widthFactor)
        , obliqueAngleRadians_(obliqueAngleRadians)
    {
    }

    const GeoPoint& Position() const { return position_; }
    const std::string& Text() const { return text_; }
    double Height() const { return height_; }
    double RotationRadians() const { return rotationRadians_; }
    TextHorizontalAlign HorizontalAlign() const { return horizontalAlign_; }
    TextVerticalAlign VerticalAlign() const { return verticalAlign_; }
    double WidthFactor() const { return widthFactor_; }
    double ObliqueAngleRadians() const { return obliqueAngleRadians_; }

    void SetPosition(const GeoPoint& position) { position_ = position; }
    void SetText(std::string text) { text_ = std::move(text); }
    void SetHeight(double height) { height_ = height; }
    void SetRotationRadians(double radians) { rotationRadians_ = radians; }
    void SetHorizontalAlign(TextHorizontalAlign align) { horizontalAlign_ = align; }
    void SetVerticalAlign(TextVerticalAlign align) { verticalAlign_ = align; }
    void SetWidthFactor(double factor) { widthFactor_ = factor; }
    void SetObliqueAngleRadians(double radians) { obliqueAngleRadians_ = radians; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<TextEntity>(
            Id(), position_, text_, height_, rotationRadians_,
            horizontalAlign_, verticalAlign_, widthFactor_, obliqueAngleRadians_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { position_ = position_.Translated(dx, dy); }

    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        position_ = GeoTransform::MirrorPoint(position_, axisStart, axisEnd);
        rotationRadians_ = GeoTransform::MirrorAngle(rotationRadians_, axisStart, axisEnd);
        obliqueAngleRadians_ = -obliqueAngleRadians_;
    }

    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        position_ = GeoTransform::RotatePoint(position_, center, angleRadians);
        rotationRadians_ += angleRadians;
    }

    void Scale(const GeoPoint& center, double factor) override
    {
        position_ = GeoTransform::ScalePoint(position_, center, factor);
        height_ *= factor;
    }

    std::vector<GripInfo> Grips() const override { return {{0, GripKind::Vertex, position_}}; }

    void SetGrip(int index, const GeoPoint& position) override
    {
        if (index == 0) {
            position_ = position;
        }
    }

    GeoShape Shaper() const override
    {
        GeoShape shape;
        shape.AddPoint(position_);
        return shape;
    }

private:
    GeoPoint position_;
    std::string text_;
    double height_ = 12.0;
    double rotationRadians_ = 0.0;
    TextHorizontalAlign horizontalAlign_ = TextHorizontalAlign::Left;
    TextVerticalAlign verticalAlign_ = TextVerticalAlign::Baseline;
    double widthFactor_ = 1.0;
    double obliqueAngleRadians_ = 0.0;
};

} // namespace cad
