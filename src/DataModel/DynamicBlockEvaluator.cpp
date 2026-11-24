#include "DataModel/DynamicBlockEvaluator.h"

#include "Base/IdGenerator.h"
#include "DataModel/LineEntity.h"
#include "DataModel/RectangleEntity.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/GeoPrimitives.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cad {

namespace {

constexpr double kEps = 1.0e-12;

double Distance(const GeoPoint& a, const GeoPoint& b)
{
    return std::hypot(a.X() - b.X(), a.Y() - b.Y());
}

GeoPoint DirectionUnit(const GeoPoint& from, const GeoPoint& to)
{
    const double dx = to.X() - from.X();
    const double dy = to.Y() - from.Y();
    const double len = std::hypot(dx, dy);
    if (len <= kEps) {
        return GeoPoint(1.0, 0.0);
    }
    return GeoPoint(dx / len, dy / len);
}

bool PointInStretchFrame(const GeoPoint& p, const DynAction& action)
{
    if (!action.hasStretchFrame) {
        return true;
    }
    const double minX = std::min(action.stretchMin.X(), action.stretchMax.X());
    const double maxX = std::max(action.stretchMin.X(), action.stretchMax.X());
    const double minY = std::min(action.stretchMin.Y(), action.stretchMax.Y());
    const double maxY = std::max(action.stretchMin.Y(), action.stretchMax.Y());
    return p.X() >= minX - kEps && p.X() <= maxX + kEps
        && p.Y() >= minY - kEps && p.Y() <= maxY + kEps;
}

bool EntityTouchesStretchFrame(const Entity& entity, const DynAction& action)
{
    if (!action.hasStretchFrame) {
        return true;
    }
    const GeoBounds b = entity.Shaper().Bounds();
    if (!b.valid) {
        return false;
    }
    // Treat as stretch candidate if any corner of bounds is in frame or bounds overlap frame.
    const GeoPoint corners[4] = {
        GeoPoint(b.minX, b.minY),
        GeoPoint(b.maxX, b.minY),
        GeoPoint(b.maxX, b.maxY),
        GeoPoint(b.minX, b.maxY),
    };
    for (const GeoPoint& c : corners) {
        if (PointInStretchFrame(c, action)) {
            return true;
        }
    }
    const double minX = std::min(action.stretchMin.X(), action.stretchMax.X());
    const double maxX = std::max(action.stretchMin.X(), action.stretchMax.X());
    const double minY = std::min(action.stretchMin.Y(), action.stretchMax.Y());
    const double maxY = std::max(action.stretchMin.Y(), action.stretchMax.Y());
    return !(b.maxX < minX || b.minX > maxX || b.maxY < minY || b.minY > maxY);
}

void StretchEntityAlong(Entity& entity, const DynAction& action, double dx, double dy)
{
    if (!EntityTouchesStretchFrame(entity, action)) {
        return;
    }
    // MVP: move whole entity if it intersects the stretch set/frame.
    // (Full AutoCAD vertex-level stretch can be layered later.)
    entity.MoveBy(dx, dy);
}

int ClampIndex(double value, int count)
{
    if (count <= 0) {
        return 0;
    }
    int idx = static_cast<int>(std::lround(value));
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    return idx;
}

} // namespace

double DynamicBlockEvaluator::GetValue(
    const std::vector<DynParamValue>& values,
    const std::string& parameterId,
    double fallback)
{
    for (const DynParamValue& v : values) {
        if (v.parameterId == parameterId) {
            return v.value;
        }
    }
    return fallback;
}

const DynParameter* DynamicBlockEvaluator::FindParameter(
    const DynamicBlockData& data,
    const std::string& id)
{
    for (const DynParameter& p : data.parameters) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string DynamicBlockEvaluator::MakeParameterId(DynParamType type, int serial)
{
    std::ostringstream ss;
    ss << "P_" << static_cast<int>(type) << "_" << serial;
    return ss.str();
}

std::string DynamicBlockEvaluator::MakeActionId(DynActionType type, int serial)
{
    std::ostringstream ss;
    ss << "A_" << static_cast<int>(type) << "_" << serial;
    return ss.str();
}

std::vector<DynParamValue> DynamicBlockEvaluator::ResolveValues(
    const BlockDefinition& definition,
    const std::vector<DynParamValue>& instanceValues)
{
    const DynamicBlockData& data = definition.dynamic;
    std::unordered_map<std::string, DynParamValue> map;
    for (const DynParameter& p : data.parameters) {
        DynParamValue v;
        v.parameterId = p.id;
        v.value = p.defaultValue;
        if (p.type == DynParamType::Linear || p.type == DynParamType::Polar) {
            const double dist = Distance(p.basePoint, p.endPoint);
            if (std::abs(v.value) <= kEps) {
                v.value = dist;
            }
        } else if (p.type == DynParamType::Point) {
            // Point params store offset distance along X for default; actual XY kept via two values later.
            // For point we encode X in value and Y is unused in MVP single-value; use endPoint as default position.
            v.value = p.endPoint.X() - p.basePoint.X(); // dx default; dy via text unused
        }
        map[p.id] = v;
    }

    for (const DynParamValue& iv : instanceValues) {
        auto it = map.find(iv.parameterId);
        if (it != map.end()) {
            it->second = iv;
        }
    }

    // Apply lookup tables: lookup param selects a row that writes column parameter values.
    for (const DynLookupTable& table : data.lookupTables) {
        const double rowValue = map.count(table.parameterId)
            ? map[table.parameterId].value
            : 0.0;
        const int rowIndex = ClampIndex(rowValue, static_cast<int>(table.rows.size()));
        if (rowIndex < 0 || rowIndex >= static_cast<int>(table.rows.size())) {
            continue;
        }
        const DynLookupRow& row = table.rows[static_cast<std::size_t>(rowIndex)];
        map[table.parameterId].textValue = row.label;
        for (std::size_t c = 0; c < table.columns.size() && c < row.values.size(); ++c) {
            const std::string& pid = table.columns[c].parameterId;
            if (map.count(pid)) {
                map[pid].value = row.values[c];
            }
        }
    }

    // Clamp / snap discrete lists.
    for (const DynParameter& p : data.parameters) {
        auto it = map.find(p.id);
        if (it == map.end()) {
            continue;
        }
        double& v = it->second.value;
        if (!p.valueList.empty()) {
            double best = p.valueList.front();
            double bestDist = std::abs(v - best);
            for (double candidate : p.valueList) {
                const double d = std::abs(v - candidate);
                if (d < bestDist) {
                    bestDist = d;
                    best = candidate;
                }
            }
            v = best;
        } else {
            if (p.step > kEps) {
                const double base = p.minValue;
                v = base + p.step * std::round((v - base) / p.step);
            }
            v = std::clamp(v, p.minValue, p.maxValue);
        }
        if (p.type == DynParamType::Visibility && !p.visibilityStates.empty()) {
            v = static_cast<double>(ClampIndex(v, static_cast<int>(p.visibilityStates.size())));
        }
        if (p.type == DynParamType::Flip) {
            v = v >= 0.5 ? 1.0 : 0.0;
        }
    }

    std::vector<DynParamValue> out;
    out.reserve(map.size());
    for (auto& kv : map) {
        out.push_back(kv.second);
    }
    return out;
}

std::vector<std::unique_ptr<Entity>> DynamicBlockEvaluator::Evaluate(
    const BlockDefinition& definition,
    const std::vector<DynParamValue>& resolvedValues)
{
    std::vector<std::unique_ptr<Entity>> result;
    result.reserve(definition.entities.size());

    // Clone all entities first.
    for (const std::shared_ptr<Entity>& source : definition.entities) {
        if (!source) {
            result.push_back(nullptr);
            continue;
        }
        std::unique_ptr<Entity> clone = source->Clone();
        if (clone) {
            clone->SetId(IdGenerator::Next());
            clone->SetSelected(false);
        }
        result.push_back(std::move(clone));
    }

    const DynamicBlockData& data = definition.dynamic;
    if (data.IsEmpty()) {
        // Drop nulls.
        std::vector<std::unique_ptr<Entity>> cleaned;
        for (auto& e : result) {
            if (e) {
                cleaned.push_back(std::move(e));
            }
        }
        return cleaned;
    }

    // Visibility: determine which entities are shown.
    std::vector<bool> visible(result.size(), true);
    for (const DynParameter& p : data.parameters) {
        if (p.type != DynParamType::Visibility || p.visibilityStates.empty()) {
            continue;
        }
        const int stateIndex = ClampIndex(
            GetValue(resolvedValues, p.id, p.defaultValue),
            static_cast<int>(p.visibilityStates.size()));
        const std::string& stateName = p.visibilityStates[static_cast<std::size_t>(stateIndex)];
        for (const DynVisibilityOverride& ov : data.visibilityOverrides) {
            if (ov.stateName != stateName) {
                continue;
            }
            for (std::size_t i = 0; i < visible.size() && i < ov.entityVisible.size(); ++i) {
                visible[i] = ov.entityVisible[i];
            }
            break;
        }
    }

    // Apply geometric actions in authoring order.
    for (const DynAction& action : data.actions) {
        const DynParameter* param = FindParameter(data, action.parameterId);
        if (!param) {
            continue;
        }
        const double value = GetValue(resolvedValues, param->id, param->defaultValue);

        std::unordered_set<std::size_t> targets;
        if (action.entityIndices.empty()) {
            for (std::size_t i = 0; i < result.size(); ++i) {
                targets.insert(i);
            }
        } else {
            for (std::size_t idx : action.entityIndices) {
                targets.insert(idx);
            }
        }

        if (action.type == DynActionType::Move && param->type == DynParamType::Point) {
            const double defDx = param->endPoint.X() - param->basePoint.X();
            const double defDy = param->endPoint.Y() - param->basePoint.Y();
            // Instance value stores absolute X offset from base; Y offset from a second packing:
            // value = dx, and we keep dy = defDy + (optional) — for MVP use value as distance along base→end.
            const GeoPoint unit = DirectionUnit(param->basePoint, param->endPoint);
            const double defLen = Distance(param->basePoint, param->endPoint);
            const double curLen = value;
            const double delta = curLen - (defLen > kEps ? defLen : 0.0);
            const double dx = unit.X() * delta;
            const double dy = unit.Y() * delta;
            // Also support free XY: if end==base, interpret value as dx only.
            const double moveDx = (defLen > kEps) ? dx : (value - defDx);
            const double moveDy = (defLen > kEps) ? dy : 0.0;
            for (std::size_t idx : targets) {
                if (idx < result.size() && result[idx]) {
                    result[idx]->MoveBy(moveDx, moveDy);
                }
            }
        } else if ((action.type == DynActionType::Stretch || action.type == DynActionType::Move)
            && (param->type == DynParamType::Linear || param->type == DynParamType::Polar)) {
            const double defLen = Distance(param->basePoint, param->endPoint);
            const double curLen = value;
            const double delta = curLen - (defLen > kEps ? defLen : 0.0);
            const GeoPoint unit = DirectionUnit(param->basePoint, param->endPoint);
            const double dx = unit.X() * delta;
            const double dy = unit.Y() * delta;
            for (std::size_t idx : targets) {
                if (idx >= result.size() || !result[idx]) {
                    continue;
                }
                if (action.type == DynActionType::Stretch) {
                    StretchEntityAlong(*result[idx], action, dx, dy);
                } else {
                    result[idx]->MoveBy(dx, dy);
                }
            }
        } else if (action.type == DynActionType::Scale
            && (param->type == DynParamType::Linear || param->type == DynParamType::Polar)) {
            const double defLen = Distance(param->basePoint, param->endPoint);
            if (defLen > kEps && value > kEps) {
                const double factor = value / defLen;
                for (std::size_t idx : targets) {
                    if (idx < result.size() && result[idx]) {
                        result[idx]->Scale(param->basePoint, factor);
                    }
                }
            }
        } else if (action.type == DynActionType::Rotate && param->type == DynParamType::Rotation) {
            const double delta = value - param->defaultValue;
            if (std::abs(delta) > kEps) {
                for (std::size_t idx : targets) {
                    if (idx < result.size() && result[idx]) {
                        result[idx]->Rotate(param->basePoint, delta);
                    }
                }
            }
        } else if (action.type == DynActionType::Flip && param->type == DynParamType::Flip) {
            if (value >= 0.5) {
                // Mirror across the flip line base→end.
                for (std::size_t idx : targets) {
                    if (idx < result.size() && result[idx]) {
                        result[idx]->Mirror(param->basePoint, param->endPoint);
                    }
                }
            }
        } else if (action.type == DynActionType::Array
            && (param->type == DynParamType::Linear || param->type == DynParamType::Polar)) {
            const double spacing = action.arraySpacing > kEps ? action.arraySpacing : 10.0;
            int count = static_cast<int>(std::floor(value / spacing)) + 1;
            count = std::clamp(count, 1, action.arrayMaxCount > 0 ? action.arrayMaxCount : 100);
            const GeoPoint unit = DirectionUnit(param->basePoint, param->endPoint);
            // Collect originals in targets and duplicate.
            std::vector<std::unique_ptr<Entity>> extras;
            for (std::size_t idx : targets) {
                if (idx >= result.size() || !result[idx]) {
                    continue;
                }
                for (int n = 1; n < count; ++n) {
                    std::unique_ptr<Entity> copy = result[idx]->Clone();
                    if (!copy) {
                        continue;
                    }
                    copy->SetId(IdGenerator::Next());
                    copy->MoveBy(unit.X() * spacing * n, unit.Y() * spacing * n);
                    extras.push_back(std::move(copy));
                }
            }
            for (auto& e : extras) {
                result.push_back(std::move(e));
                visible.push_back(true);
            }
        }
    }

    std::vector<std::unique_ptr<Entity>> cleaned;
    cleaned.reserve(result.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (!result[i]) {
            continue;
        }
        if (i < visible.size() && !visible[i]) {
            continue;
        }
        cleaned.push_back(std::move(result[i]));
    }
    return cleaned;
}

bool DynamicBlockEvaluator::RunSelfTest(std::string& report)
{
    std::ostringstream out;
    int passed = 0;
    int failed = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) {
            ++passed;
            out << "[PASS] " << name << "\n";
        } else {
            ++failed;
            out << "[FAIL] " << name << "\n";
        }
    };

    BlockDefinition def;
    def.name = "__dyn_self_test__";
    // Fixed left rectangle + movable right vertical line (stretch target).
    def.entities.push_back(std::make_shared<RectangleEntity>(
        IdGenerator::Next(), GeoRectangle(GeoPoint(0.0, 0.0), GeoPoint(40.0, 30.0))));
    def.entities.push_back(std::make_shared<LineEntity>(
        IdGenerator::Next(), GeoLine(GeoPoint(100.0, 0.0), GeoPoint(100.0, 30.0))));

    DynParameter linear;
    linear.id = "P_linear";
    linear.name = "宽度";
    linear.type = DynParamType::Linear;
    linear.basePoint = GeoPoint(0.0, 15.0);
    linear.endPoint = GeoPoint(100.0, 15.0);
    linear.defaultValue = 100.0;
    linear.minValue = 10.0;
    linear.maxValue = 500.0;
    def.dynamic.parameters.push_back(linear);

    DynAction stretch;
    stretch.id = "A_stretch";
    stretch.name = "拉伸右边";
    stretch.type = DynActionType::Stretch;
    stretch.parameterId = linear.id;
    stretch.entityIndices = {1}; // only the right line
    stretch.hasStretchFrame = true;
    stretch.stretchMin = GeoPoint(50.0, -1.0);
    stretch.stretchMax = GeoPoint(120.0, 40.0);
    def.dynamic.actions.push_back(stretch);

    // Default evaluation: line should stay near x=100.
    {
        const auto values = ResolveValues(def, {});
        auto ents = Evaluate(def, values);
        check(ents.size() == 2, "default entity count == 2");
        if (ents.size() == 2 && ents[1]) {
            const GeoBounds b = ents[1]->Shaper().Bounds();
            check(b.valid && std::abs(b.minX - 100.0) < 1.0, "default right line ~ x=100");
        } else {
            check(false, "default right line ~ x=100");
        }
    }

    // Stretch to 150: right line should move +50 in X.
    {
        std::vector<DynParamValue> inst = {{linear.id, 150.0, {}}};
        const auto values = ResolveValues(def, inst);
        check(std::abs(GetValue(values, linear.id, 0.0) - 150.0) < 1.0e-6, "resolved width == 150");
        auto ents = Evaluate(def, values);
        check(ents.size() == 2, "stretched entity count == 2");
        if (ents.size() == 2 && ents[1]) {
            const GeoBounds b = ents[1]->Shaper().Bounds();
            check(b.valid && std::abs(b.minX - 150.0) < 1.0, "stretched right line ~ x=150");
        } else {
            check(false, "stretched right line ~ x=150");
        }
        if (ents.size() == 2 && ents[0]) {
            const GeoBounds b = ents[0]->Shaper().Bounds();
            check(b.valid && std::abs(b.minX) < 1.0 && std::abs(b.maxX - 40.0) < 1.0,
                "left rectangle stays put");
        } else {
            check(false, "left rectangle stays put");
        }
    }

    // Visibility: hide entity 1 in state2.
    {
        DynParameter vis;
        vis.id = "P_vis";
        vis.name = "可见性";
        vis.type = DynParamType::Visibility;
        vis.visibilityStates = {"状态1", "状态2"};
        vis.defaultValue = 0.0;
        def.dynamic.parameters.push_back(vis);
        DynVisibilityOverride s1;
        s1.stateName = "状态1";
        s1.entityVisible = {true, true};
        DynVisibilityOverride s2;
        s2.stateName = "状态2";
        s2.entityVisible = {true, false};
        def.dynamic.visibilityOverrides = {s1, s2};

        auto ents1 = Evaluate(def, ResolveValues(def, {{vis.id, 0.0, {}}}));
        auto ents2 = Evaluate(def, ResolveValues(def, {{vis.id, 1.0, {}}}));
        check(ents1.size() == 2, "visibility state1 shows 2 entities");
        check(ents2.size() == 1, "visibility state2 shows 1 entity");
    }

    out << "Dynamic block self-test: " << passed << " passed, " << failed << " failed.\n";
    report = out.str();
    return failed == 0;
}

} // namespace cad
