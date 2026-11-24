#pragma once

#include "DataModel/BlockDefinition.h"
#include "DataModel/DynamicBlock.h"
#include "DataModel/Entity.h"
#include "Geometry/GeoShape.h"
#include "Geometry/GeoTransform.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cad {

class BlockReferenceEntity : public Entity {
public:
    static constexpr int kDynamicGripBase = 1000;

    BlockReferenceEntity(
        EntityId id,
        std::string blockName,
        GeoPoint insertionPoint,
        double scale = 1.0,
        double rotationRadians = 0.0)
        : Entity(id, EntityType::BlockReference)
        , blockName_(std::move(blockName))
        , insertionPoint_(insertionPoint)
        , scaleX_(scale)
        , scaleY_(scale)
        , rotationRadians_(rotationRadians)
    {
    }

    BlockReferenceEntity(
        EntityId id,
        std::string blockName,
        GeoPoint insertionPoint,
        double scaleX,
        double scaleY,
        double rotationRadians)
        : Entity(id, EntityType::BlockReference)
        , blockName_(std::move(blockName))
        , insertionPoint_(insertionPoint)
        , scaleX_(scaleX)
        , scaleY_(scaleY)
        , rotationRadians_(rotationRadians)
    {
    }

    const std::string& BlockName() const { return blockName_; }
    void SetBlockName(std::string name) { blockName_ = std::move(name); }
    const GeoPoint& InsertionPoint() const { return insertionPoint_; }
    void SetInsertionPoint(const GeoPoint& point) { insertionPoint_ = point; InvalidateCache(); }

    double ScaleFactor() const { return scaleX_; }
    double ScaleX() const { return scaleX_; }
    double ScaleY() const { return scaleY_; }
    void SetScale(double scale)
    {
        scaleX_ = scale;
        scaleY_ = scale;
        InvalidateCache();
    }
    void SetScaleXY(double scaleX, double scaleY)
    {
        scaleX_ = scaleX;
        scaleY_ = scaleY;
        InvalidateCache();
    }

    double RotationRadians() const { return rotationRadians_; }
    void SetRotationRadians(double radians)
    {
        rotationRadians_ = radians;
        InvalidateCache();
    }

    void SetAttributeValue(const std::string& tag, const std::string& value);
    const std::vector<BlockAttributeValue>& AttributeValues() const { return attributeValues_; }
    std::vector<BlockAttributeValue>& AttributeValues() { return attributeValues_; }
    void SetAttributeValues(std::vector<BlockAttributeValue> values)
    {
        attributeValues_ = std::move(values);
    }

    const std::vector<DynParamValue>& DynamicValues() const { return dynamicValues_; }
    std::vector<DynParamValue>& DynamicValues() { return dynamicValues_; }
    void SetDynamicValues(std::vector<DynParamValue> values)
    {
        dynamicValues_ = std::move(values);
        InvalidateCache();
    }
    void SetDynamicValue(const std::string& parameterId, double value);
    double GetDynamicValue(const std::string& parameterId, double fallback) const;

    void RebuildCache(const BlockDefinition* definition);
    void InvalidateCache() { cacheValid_ = false; }
    bool IsCacheValid() const { return cacheValid_; }
    const GeoShape& CachedShape() const { return cachedShape_; }

    std::vector<GripInfo> DynamicGrips(const BlockDefinition* definition) const;
    bool ApplyDynamicGrip(
        const BlockDefinition* definition,
        int gripIndex,
        const GeoPoint& worldPosition);

    std::unique_ptr<Entity> Clone() const override;

    void MoveBy(double dx, double dy) override
    {
        insertionPoint_ = insertionPoint_.Translated(dx, dy);
        InvalidateCache();
    }

    void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) override
    {
        insertionPoint_ = GeoTransform::MirrorPoint(insertionPoint_, axisStart, axisEnd);
        rotationRadians_ = GeoTransform::MirrorAngle(rotationRadians_, axisStart, axisEnd);
        InvalidateCache();
    }

    void Rotate(const GeoPoint& center, double angleRadians) override
    {
        insertionPoint_ = GeoTransform::RotatePoint(insertionPoint_, center, angleRadians);
        rotationRadians_ += angleRadians;
        InvalidateCache();
    }

    void Scale(const GeoPoint& center, double factor) override
    {
        insertionPoint_ = GeoTransform::ScalePoint(insertionPoint_, center, factor);
        scaleX_ *= factor;
        scaleY_ *= factor;
        InvalidateCache();
    }

    std::vector<GripInfo> Grips() const override
    {
        return {{0, GripKind::Vertex, insertionPoint_}};
    }

    void SetGrip(int index, const GeoPoint& position) override
    {
        if (index == 0) {
            insertionPoint_ = position;
            InvalidateCache();
        }
    }

    GeoShape Shaper() const override;

private:
    GeoShape PlaceholderShape() const;
    GeoPoint LocalToWorld(const GeoPoint& local) const;
    GeoPoint WorldToLocal(const GeoPoint& world) const;

    std::string blockName_;
    GeoPoint insertionPoint_;
    double scaleX_ = 1.0;
    double scaleY_ = 1.0;
    double rotationRadians_ = 0.0;
    std::vector<BlockAttributeValue> attributeValues_;
    std::vector<DynParamValue> dynamicValues_;
    mutable GeoShape cachedShape_;
    mutable bool cacheValid_ = false;
};

} // namespace cad
