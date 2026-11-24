#pragma once

#include "Base/TextTypes.h"
#include "DataModel/Entity.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <memory>
#include <string>
#include <utility>

namespace cad {

class MTextEntity : public Entity {
public:
    MTextEntity(
        EntityId id,
        GeoPoint position,
        std::string content,
        double height = 12.0,
        double rotationRadians = 0.0,
        double referenceWidth = 0.0,
        double lineSpacingFactor = 1.0,
        MTextAttachment attachment = MTextAttachment::TopLeft,
        MTextParagraphAlign paragraphAlign = MTextParagraphAlign::Left,
        bool backgroundMask = false)
        : Entity(id, EntityType::MText)
        , position_(position)
        , content_(std::move(content))
        , height_(height)
        , rotationRadians_(rotationRadians)
        , referenceWidth_(referenceWidth)
        , lineSpacingFactor_(lineSpacingFactor)
        , attachment_(attachment)
        , paragraphAlign_(paragraphAlign)
        , backgroundMask_(backgroundMask)
    {
    }

    const GeoPoint& Position() const { return position_; }
    const std::string& Content() const { return content_; }
    double Height() const { return height_; }
    double RotationRadians() const { return rotationRadians_; }
    double ReferenceWidth() const { return referenceWidth_; }
    double LineSpacingFactor() const { return lineSpacingFactor_; }
    MTextAttachment Attachment() const { return attachment_; }
    MTextParagraphAlign ParagraphAlign() const { return paragraphAlign_; }
    bool BackgroundMask() const { return backgroundMask_; }

    void SetPosition(const GeoPoint& position) { position_ = position; }
    void SetContent(std::string content) { content_ = std::move(content); }
    void SetHeight(double height) { height_ = height; }
    void SetRotationRadians(double radians) { rotationRadians_ = radians; }
    void SetReferenceWidth(double width) { referenceWidth_ = width; }
    void SetLineSpacingFactor(double factor) { lineSpacingFactor_ = factor; }
    void SetAttachment(MTextAttachment attachment) { attachment_ = attachment; }
    void SetParagraphAlign(MTextParagraphAlign align) { paragraphAlign_ = align; }
    void SetBackgroundMask(bool enabled) { backgroundMask_ = enabled; }

    std::unique_ptr<Entity> Clone() const override
    {
        auto clone = std::make_unique<MTextEntity>(
            Id(), position_, content_, height_, rotationRadians_,
            referenceWidth_, lineSpacingFactor_, attachment_, paragraphAlign_, backgroundMask_);
        CopyBasePropertiesTo(*clone);
        return clone;
    }

    void MoveBy(double dx, double dy) override { position_ = position_.Translated(dx, dy); }

    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        position_ = GeoTransform::MirrorPoint(position_, axisStart, axisEnd);
        rotationRadians_ = GeoTransform::MirrorAngle(rotationRadians_, axisStart, axisEnd);
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
        referenceWidth_ *= factor;
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
    std::string content_;
    double height_ = 12.0;
    double rotationRadians_ = 0.0;
    double referenceWidth_ = 0.0;
    double lineSpacingFactor_ = 1.0;
    MTextAttachment attachment_ = MTextAttachment::TopLeft;
    MTextParagraphAlign paragraphAlign_ = MTextParagraphAlign::Left;
    bool backgroundMask_ = false;
};

} // namespace cad
