#pragma once

#include "Geometry/GeoPoint.h"
#include <string>
#include <vector>

namespace cad {

// AutoCAD-like dynamic block authoring primitives (BPARAMETER / BACTION / BVSTATE / BLOOKUPTABLE).
enum class DynParamType {
    Point,
    Linear,
    XY,
    Rotation,
    Flip,
    Alignment,
    Visibility,
    Lookup,
    Polar
};

enum class DynActionType {
    Move,
    Stretch,
    Scale,
    Rotate,
    Flip,
    Array,
    Lookup,
    Visibility
};

struct DynParameter {
    std::string id;
    std::string name;
    DynParamType type = DynParamType::Linear;

    // Definition-space geometry of the parameter (relative to block base).
    GeoPoint basePoint;
    GeoPoint endPoint; // Linear / Polar / XY (X end); Y end for XY uses endPointY
    GeoPoint endPointY;

    double defaultValue = 0.0; // distance, angle(rad), flip 0/1, visibility index, lookup row
    double minValue = 0.0;
    double maxValue = 1.0e9;
    double step = 0.0; // 0 = free
    std::vector<double> valueList; // optional discrete values

    // Visibility parameter: state names (defaultValue = active index).
    std::vector<std::string> visibilityStates;
};

struct DynAction {
    std::string id;
    std::string name;
    DynActionType type = DynActionType::Stretch;
    std::string parameterId;

    // Indices into BlockDefinition::entities that this action affects.
    std::vector<std::size_t> entityIndices;

    // Stretch frame in definition space (axis-aligned for MVP). Empty = stretch all associated entities as a rigid move along param.
    GeoPoint stretchMin;
    GeoPoint stretchMax;
    bool hasStretchFrame = false;

    // Array: column/row spacing (definition units).
    double arraySpacing = 10.0;
    int arrayMaxCount = 100;
};

struct DynVisibilityOverride {
    std::string stateName;
    // Parallel to BlockDefinition::entities: true = visible in this state.
    // Empty means "all visible".
    std::vector<bool> entityVisible;
};

struct DynLookupColumn {
    std::string parameterId;
    std::string header;
};

struct DynLookupRow {
    std::string label;
    // Values aligned with columns (parameter defaults when applied).
    std::vector<double> values;
};

struct DynLookupTable {
    std::string parameterId; // Lookup parameter that selects the row
    std::vector<DynLookupColumn> columns;
    std::vector<DynLookupRow> rows;
};

struct DynamicBlockData {
    std::vector<DynParameter> parameters;
    std::vector<DynAction> actions;
    std::vector<DynVisibilityOverride> visibilityOverrides;
    std::vector<DynLookupTable> lookupTables;

    bool IsEmpty() const
    {
        return parameters.empty() && actions.empty()
            && visibilityOverrides.empty() && lookupTables.empty();
    }
};

// Per-instance dynamic values (keyed by parameter id).
struct DynParamValue {
    std::string parameterId;
    double value = 0.0;
    std::string textValue; // optional label for lookup/visibility
};

inline const char* DynParamTypeName(DynParamType type)
{
    switch (type) {
    case DynParamType::Point: return "点";
    case DynParamType::Linear: return "线性";
    case DynParamType::XY: return "XY";
    case DynParamType::Rotation: return "旋转";
    case DynParamType::Flip: return "翻转";
    case DynParamType::Alignment: return "对齐";
    case DynParamType::Visibility: return "可见性";
    case DynParamType::Lookup: return "查询";
    case DynParamType::Polar: return "极轴";
    }
    return "参数";
}

inline const char* DynActionTypeName(DynActionType type)
{
    switch (type) {
    case DynActionType::Move: return "移动";
    case DynActionType::Stretch: return "拉伸";
    case DynActionType::Scale: return "缩放";
    case DynActionType::Rotate: return "旋转";
    case DynActionType::Flip: return "翻转";
    case DynActionType::Array: return "阵列";
    case DynActionType::Lookup: return "查询";
    case DynActionType::Visibility: return "可见性";
    }
    return "动作";
}

} // namespace cad
