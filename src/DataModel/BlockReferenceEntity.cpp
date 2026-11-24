#include "DataModel/BlockReferenceEntity.h"

#include "DataModel/DynamicBlockEvaluator.h"
#include "Geometry/GeoPoint.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace cad {

namespace {

constexpr double kEpsilon = 1.0e-12;

} // namespace

void BlockReferenceEntity::SetAttributeValue(const std::string& tag, const std::string& value)
{
    for (BlockAttributeValue& attr : attributeValues_) {
        if (attr.tag == tag) {
            attr.value = value;
            return;
        }
    }
    attributeValues_.push_back({tag, value});
}

void BlockReferenceEntity::SetDynamicValue(const std::string& parameterId, double value)
{
    for (DynParamValue& v : dynamicValues_) {
        if (v.parameterId == parameterId) {
            v.value = value;
            InvalidateCache();
            return;
        }
    }
    dynamicValues_.push_back({parameterId, value, {}});
    InvalidateCache();
}

double BlockReferenceEntity::GetDynamicValue(const std::string& parameterId, double fallback) const
{
    for (const DynParamValue& v : dynamicValues_) {
        if (v.parameterId == parameterId) {
            return v.value;
        }
    }
    return fallback;
}

GeoPoint BlockReferenceEntity::LocalToWorld(const GeoPoint& local) const
{
    double x = local.X() * scaleX_;
    double y = local.Y() * scaleY_;
    const double c = std::cos(rotationRadians_);
    const double s = std::sin(rotationRadians_);
    const double rx = x * c - y * s;
    const double ry = x * s + y * c;
    return GeoPoint(insertionPoint_.X() + rx, insertionPoint_.Y() + ry);
}

GeoPoint BlockReferenceEntity::WorldToLocal(const GeoPoint& world) const
{
    const double dx = world.X() - insertionPoint_.X();
    const double dy = world.Y() - insertionPoint_.Y();
    const double c = std::cos(-rotationRadians_);
    const double s = std::sin(-rotationRadians_);
    const double rx = dx * c - dy * s;
    const double ry = dx * s + dy * c;
    const double invSx = std::abs(scaleX_) > kEpsilon ? 1.0 / scaleX_ : 1.0;
    const double invSy = std::abs(scaleY_) > kEpsilon ? 1.0 / scaleY_ : 1.0;
    return GeoPoint(rx * invSx, ry * invSy);
}

void BlockReferenceEntity::RebuildCache(const BlockDefinition* definition)
{
    cachedShape_.Clear();
    cacheValid_ = false;
    if (!definition || definition->entities.empty()) {
        cachedShape_ = PlaceholderShape();
        cacheValid_ = true;
        return;
    }

    const std::vector<DynParamValue> resolved =
        DynamicBlockEvaluator::ResolveValues(*definition, dynamicValues_);
    std::vector<std::unique_ptr<Entity>> evaluated =
        DynamicBlockEvaluator::Evaluate(*definition, resolved);

    const double sx = scaleX_;
    const double sy = scaleY_;
    const bool uniform = std::abs(sx - sy) <= kEpsilon;
    const GeoPoint origin(0.0, 0.0);

    for (auto& clone : evaluated) {
        if (!clone) {
            continue;
        }
        if (uniform) {
            if (std::abs(sx - 1.0) > kEpsilon) {
                clone->Scale(origin, sx);
            }
        } else {
            const double avg = 0.5 * (sx + sy);
            if (std::abs(avg - 1.0) > kEpsilon) {
                clone->Scale(origin, avg);
            }
        }
        if (std::abs(rotationRadians_) > kEpsilon) {
            clone->Rotate(origin, rotationRadians_);
        }
        clone->MoveBy(insertionPoint_.X(), insertionPoint_.Y());
        cachedShape_.Append(clone->Shaper());
    }

    if (!cachedShape_.Bounds().valid) {
        cachedShape_ = PlaceholderShape();
    }
    cacheValid_ = true;
}

std::vector<GripInfo> BlockReferenceEntity::DynamicGrips(const BlockDefinition* definition) const
{
    std::vector<GripInfo> grips;
    if (!definition || definition->dynamic.IsEmpty()) {
        return grips;
    }

    const std::vector<DynParamValue> resolved =
        DynamicBlockEvaluator::ResolveValues(*definition, dynamicValues_);

    for (std::size_t i = 0; i < definition->dynamic.parameters.size(); ++i) {
        const DynParameter& p = definition->dynamic.parameters[i];
        if (p.type == DynParamType::Visibility || p.type == DynParamType::Lookup
            || p.type == DynParamType::Alignment) {
            // These use property UI / dropdown rather than a stretch grip.
            continue;
        }

        GeoPoint local = p.endPoint;
        const double value = DynamicBlockEvaluator::GetValue(resolved, p.id, p.defaultValue);

        if (p.type == DynParamType::Linear || p.type == DynParamType::Polar) {
            const double dx = p.endPoint.X() - p.basePoint.X();
            const double dy = p.endPoint.Y() - p.basePoint.Y();
            const double defLen = std::hypot(dx, dy);
            if (defLen > kEpsilon) {
                const double ux = dx / defLen;
                const double uy = dy / defLen;
                local = GeoPoint(p.basePoint.X() + ux * value, p.basePoint.Y() + uy * value);
            }
        } else if (p.type == DynParamType::Point) {
            const double dx = p.endPoint.X() - p.basePoint.X();
            const double dy = p.endPoint.Y() - p.basePoint.Y();
            const double defLen = std::hypot(dx, dy);
            if (defLen > kEpsilon) {
                local = GeoPoint(
                    p.basePoint.X() + (dx / defLen) * value,
                    p.basePoint.Y() + (dy / defLen) * value);
            } else {
                local = GeoPoint(p.basePoint.X() + value, p.basePoint.Y());
            }
        } else if (p.type == DynParamType::Rotation) {
            const double radius = std::max(10.0, std::hypot(p.endPoint.X() - p.basePoint.X(), p.endPoint.Y() - p.basePoint.Y()));
            local = GeoPoint(
                p.basePoint.X() + std::cos(value) * radius,
                p.basePoint.Y() + std::sin(value) * radius);
        } else if (p.type == DynParamType::Flip) {
            local = p.endPoint;
        } else if (p.type == DynParamType::XY) {
            local = GeoPoint(
                p.basePoint.X() + DynamicBlockEvaluator::GetValue(resolved, p.id, p.defaultValue),
                p.endPointY.Y());
        }

        GripInfo g;
        g.index = kDynamicGripBase + static_cast<int>(i);
        g.kind = GripKind::Dynamic;
        g.position = LocalToWorld(local);
        grips.push_back(g);
    }
    return grips;
}

bool BlockReferenceEntity::ApplyDynamicGrip(
    const BlockDefinition* definition,
    int gripIndex,
    const GeoPoint& worldPosition)
{
    if (!definition || gripIndex < kDynamicGripBase) {
        return false;
    }
    const std::size_t paramIndex = static_cast<std::size_t>(gripIndex - kDynamicGripBase);
    if (paramIndex >= definition->dynamic.parameters.size()) {
        return false;
    }

    const DynParameter& p = definition->dynamic.parameters[paramIndex];
    const GeoPoint local = WorldToLocal(worldPosition);
    double newValue = p.defaultValue;

    if (p.type == DynParamType::Linear || p.type == DynParamType::Polar || p.type == DynParamType::Point) {
        const double dx = p.endPoint.X() - p.basePoint.X();
        const double dy = p.endPoint.Y() - p.basePoint.Y();
        const double defLen = std::hypot(dx, dy);
        if (defLen > kEpsilon) {
            const double ux = dx / defLen;
            const double uy = dy / defLen;
            newValue = (local.X() - p.basePoint.X()) * ux + (local.Y() - p.basePoint.Y()) * uy;
        } else {
            newValue = local.X() - p.basePoint.X();
        }
        newValue = std::max(0.0, newValue);
    } else if (p.type == DynParamType::Rotation) {
        newValue = std::atan2(local.Y() - p.basePoint.Y(), local.X() - p.basePoint.X());
    } else if (p.type == DynParamType::Flip) {
        // Toggle on grip click/drag past midline.
        const double cur = GetDynamicValue(p.id, p.defaultValue);
        newValue = cur >= 0.5 ? 0.0 : 1.0;
    } else if (p.type == DynParamType::XY) {
        newValue = local.X() - p.basePoint.X();
    } else {
        return false;
    }

    SetDynamicValue(p.id, newValue);
    RebuildCache(definition);
    return true;
}

std::unique_ptr<Entity> BlockReferenceEntity::Clone() const
{
    auto clone = std::make_unique<BlockReferenceEntity>(
        Id(), blockName_, insertionPoint_, scaleX_, scaleY_, rotationRadians_);
    clone->attributeValues_ = attributeValues_;
    clone->dynamicValues_ = dynamicValues_;
    CopyBasePropertiesTo(*clone);
    if (cacheValid_) {
        clone->cachedShape_ = cachedShape_;
        clone->cacheValid_ = true;
    }
    return clone;
}

GeoShape BlockReferenceEntity::Shaper() const
{
    if (cacheValid_) {
        return cachedShape_;
    }
    return PlaceholderShape();
}

GeoShape BlockReferenceEntity::PlaceholderShape() const
{
    GeoShape shape;
    const double size = 12.0 * (std::abs(scaleX_) > kEpsilon ? std::abs(scaleX_) : 1.0);
    shape.AddPolyline(
        {
            insertionPoint_.Translated(-size * 0.5, -size * 0.5),
            insertionPoint_.Translated(size * 0.5, -size * 0.5),
            insertionPoint_.Translated(size * 0.5, size * 0.5),
            insertionPoint_.Translated(-size * 0.5, size * 0.5),
        },
        true);
    return shape;
}

} // namespace cad
