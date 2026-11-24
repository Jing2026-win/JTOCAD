#pragma once

#include "Base/CadTypes.h"
#include "Base/GripTypes.h"
#include "Base/Style.h"
#include "Geometry/GeoShape.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cad {

class GeoPoint;

class Entity {
public:
    Entity(EntityId id, EntityType type);
    virtual ~Entity() = default;

    EntityId Id() const { return id_; }
    void SetId(EntityId id) { id_ = id; }
    EntityType Type() const { return type_; }

    const std::string& LayerName() const { return layerName_; }
    void SetLayerName(std::string layerName) { layerName_ = std::move(layerName); }

    const EntityStyle& Style() const { return style_; }
    EntityStyle& Style() { return style_; }
    void SetStyle(const EntityStyle& style) { style_ = style; }

    bool IsSelected() const { return selected_; }
    void SetSelected(bool selected) { selected_ = selected; }

    virtual std::unique_ptr<Entity> Clone() const = 0;
    virtual void MoveBy(double dx, double dy) = 0;
    virtual void Mirror(const GeoPoint& axisStart, const GeoPoint& axisEnd) = 0;
    virtual void Rotate(const GeoPoint& center, double angleRadians) = 0;
    virtual void Scale(const GeoPoint& center, double factor) = 0;
    virtual std::vector<GripInfo> Grips() const = 0;
    virtual void SetGrip(int index, const GeoPoint& position) = 0;
    virtual GeoShape Shaper() const = 0;

protected:
    void CopyBasePropertiesTo(Entity& target) const
    {
        target.SetLayerName(layerName_);
        target.SetStyle(style_);
        target.SetSelected(selected_);
    }

private:
    EntityId id_;
    EntityType type_;
    std::string layerName_ = "0";
    EntityStyle style_;
    bool selected_ = false;
};

} // namespace cad
