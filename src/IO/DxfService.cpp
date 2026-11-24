#include "Base/TextTypes.h"
#include "IO/DxfService.h"

#include "Base/IdGenerator.h"
#include "DataModel/MTextEntity.h"
#include "DataModel/CadDocument.h"
#include "DataModel/ArcEntity.h"
#include "DataModel/BlockReferenceEntity.h"
#include "DataModel/CircleEntity.h"
#include "DataModel/DimensionEntity.h"
#include "DataModel/Entity.h"
#include "DataModel/LineEntity.h"
#include "DataModel/PolylineEntity.h"
#include "DataModel/RayEntity.h"
#include "DataModel/RectangleEntity.h"
#include "DataModel/TextEntity.h"
#include "Geometry/DrawAlgorithms.h"
#include "Geometry/GeoArc.h"
#include "Geometry/GeoCircle.h"
#include "Geometry/GeoLine.h"
#include "Geometry/GeoPolyline.h"
#include "View/LineWeightUtil.h"
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

namespace {

struct DxfPair {
    int code = 0;
    std::string value;
};

double ToDouble(const std::string& value)
{
    std::istringstream stream(value);
    double result = 0.0;
    stream >> result;
    return result;
}

std::vector<DxfPair> ReadPairs(std::istream& input)
{
    std::vector<DxfPair> pairs;
    std::string codeLine;
    std::string valueLine;
    while (std::getline(input, codeLine) && std::getline(input, valueLine)) {
        std::istringstream codeStream(codeLine);
        DxfPair pair;
        codeStream >> pair.code;
        pair.value = valueLine;
        pairs.push_back(std::move(pair));
    }
    return pairs;
}

void WritePair(std::ostream& output, int code, const std::string& value)
{
    output << code << "\n" << value << "\n";
}

void WritePair(std::ostream& output, int code, double value)
{
    output << code << "\n" << value << "\n";
}

std::string ValueOrEmpty(const std::vector<DxfPair>& pairs, std::size_t& i, int code)
{
    if (i < pairs.size() && pairs[i].code == code) {
        return pairs[i++].value;
    }
    return {};
}

std::string LineTypeName(const EntityStyle& style)
{
    if (style.lineTypeSource == PropertySource::ByLayer) {
        return "ByLayer";
    }
    if (style.lineTypeSource == PropertySource::ByBlock) {
        return "ByBlock";
    }
    if (!style.lineTypeName.empty()) {
        return style.lineTypeName;
    }
    switch (style.linePattern) {
    case LinePattern::Dashed:
        return "DASHED";
    case LinePattern::Center:
        return "CENTER";
    case LinePattern::Hidden:
        return "HIDDEN";
    case LinePattern::Continuous:
        return "CONTINUOUS";
    }
    return "CONTINUOUS";
}

std::uint32_t DxfColorFromIndex(const int index)
{
    switch (std::abs(index)) {
    case 1:
        return 0xFF0000;
    case 2:
        return 0xFFFF00;
    case 3:
        return 0x00FF00;
    case 4:
        return 0x00FFFF;
    case 5:
        return 0x0000FF;
    case 6:
        return 0xFF00FF;
    case 7:
    default:
        return 0xFFFFFF;
    }
}

int DxfIndexFromColor(const std::uint32_t color)
{
    switch (color & 0xFFFFFF) {
    case 0xFF0000:
        return 1;
    case 0xFFFF00:
        return 2;
    case 0x00FF00:
        return 3;
    case 0x00FFFF:
        return 4;
    case 0x0000FF:
        return 5;
    case 0xFF00FF:
        return 6;
    default:
        return 7;
    }
}

void WriteCommonEntityPairs(std::ostream& output, const Entity& entity)
{
    WritePair(output, 8, entity.LayerName());
    WritePair(output, 6, LineTypeName(entity.Style()));
    WritePair(output, 370, entity.Style().lineWidth * 100.0);
}

void ParseTables(const std::vector<DxfPair>& pairs, std::size_t& i, CadDocument& document)
{
    std::string currentTable;
    for (; i < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "ENDSEC") {
            return;
        }
        if (pairs[i].code == 0 && pairs[i].value == "TABLE") {
            ++i;
            currentTable = ValueOrEmpty(pairs, i, 2);
            continue;
        }
        if (pairs[i].code == 0 && pairs[i].value == "ENDTAB") {
            currentTable.clear();
            continue;
        }

        if (pairs[i].code == 0 && pairs[i].value == "LAYER") {
            Layer layer;
            int flags = 0;
            int colorIndex = 7;
            for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
                if (pairs[i].code == 2) layer = Layer(pairs[i].value);
                if (pairs[i].code == 70) flags = static_cast<int>(ToDouble(pairs[i].value));
                if (pairs[i].code == 6) layer.SetLineTypeName(pairs[i].value);
                if (pairs[i].code == 62) {
                    colorIndex = static_cast<int>(ToDouble(pairs[i].value));
                    layer.SetOn(colorIndex >= 0);
                    layer.SetColor(DxfColorFromIndex(colorIndex));
                }
                if (pairs[i].code == 370) {
                    const int raw = static_cast<int>(ToDouble(pairs[i].value));
                    if (raw <= -3) {
                        layer.SetLineWeightMm(kDefaultLineWeightMm);
                    } else if (raw < 0) {
                        layer.SetLineWeightMm(kDefaultLineWeightMm);
                    } else {
                        layer.SetLineWeightMm(raw / 100.0);
                    }
                }
                if (pairs[i].code == 390) layer.SetPlotStyleName(pairs[i].value);
                if (pairs[i].code == 290) layer.SetPlottable(ToDouble(pairs[i].value) != 0.0);
                if (pairs[i].code == 420) layer.SetColor(static_cast<std::uint32_t>(ToDouble(pairs[i].value)));
                if (pairs[i].code == 440) layer.SetTransparency(static_cast<int>(ToDouble(pairs[i].value)));
                if (pairs[i].code == 999) layer.SetDescription(pairs[i].value);
            }
            --i;
            layer.SetFrozen((flags & 1) != 0);
            layer.SetLocked((flags & 4) != 0);
            layer.SetOn(colorIndex >= 0);
            Layer& target = document.EnsureLayer(layer.Name());
            target.SetLineTypeName(layer.LineTypeName());
            target.SetOn(layer.IsOn());
            target.SetFrozen(layer.IsFrozen());
            target.SetLocked(layer.IsLocked());
            target.SetPlottable(layer.Name() == "Defpoints" ? false : layer.IsPlottable());
            target.SetColor(layer.Color());
            target.SetLineWeightMm(layer.LineWeightMm());
            target.SetTransparency(layer.Transparency());
            target.SetPlotStyleName(layer.PlotStyleName());
            target.SetDescription(layer.Description());
        } else if (pairs[i].code == 0 && pairs[i].value == "LTYPE") {
            LineTypeDefinition lineType;
            for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
                if (pairs[i].code == 2) lineType.name = pairs[i].value;
                if (pairs[i].code == 3) lineType.description = pairs[i].value;
                if (pairs[i].code == 49) lineType.pattern.push_back(ToDouble(pairs[i].value));
            }
            --i;
            document.AddLineType(std::move(lineType));
        } else if (pairs[i].code == 0 && pairs[i].value == "STYLE") {
            TextStyleDefinition style;
            for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
                if (pairs[i].code == 2) style.name = pairs[i].value;
                if (pairs[i].code == 3) style.fontFamily = pairs[i].value;
                if (pairs[i].code == 40) style.fixedHeight = ToDouble(pairs[i].value);
            }
            --i;
            document.AddTextStyle(std::move(style));
        } else if (pairs[i].code == 0 && pairs[i].value == "DIMSTYLE") {
            DimensionStyle dimStyle;
            for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
                if (pairs[i].code == 2) dimStyle.name = pairs[i].value;
                if (pairs[i].code == 140) dimStyle.textHeight = ToDouble(pairs[i].value);
                if (pairs[i].code == 41) dimStyle.arrowSize = ToDouble(pairs[i].value);
                if (pairs[i].code == 42) dimStyle.dimensionLineGap = ToDouble(pairs[i].value);
            }
            --i;
            document.AddDimensionStyle(std::move(dimStyle));
        }
    }
}

std::unique_ptr<Entity> ParseOneEntity(const std::vector<DxfPair>& pairs, std::size_t& i, const std::string& type)
{
    if (type == "LINE") {
        GeoPoint start;
        GeoPoint end;
        std::string layerName = "0";
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) start.Set(ToDouble(pair.value), start.Y());
            else if (pair.code == 20) start.Set(start.X(), ToDouble(pair.value));
            else if (pair.code == 11) end.Set(ToDouble(pair.value), end.Y());
            else if (pair.code == 21) end.Set(end.X(), ToDouble(pair.value));
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<LineEntity>(IdGenerator::Next(), GeoLine(start, end));
        entity->SetLayerName(layerName);
        return entity;
    }
    if (type == "CIRCLE") {
        GeoPoint center;
        double radius = 0.0;
        std::string layerName = "0";
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) center.Set(ToDouble(pair.value), center.Y());
            else if (pair.code == 20) center.Set(center.X(), ToDouble(pair.value));
            else if (pair.code == 40) radius = ToDouble(pair.value);
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<CircleEntity>(IdGenerator::Next(), GeoCircle(center, radius));
        entity->SetLayerName(layerName);
        return entity;
    }
    if (type == "RAY") {
        GeoPoint origin;
        GeoPoint direction;
        std::string layerName = "0";
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) origin.Set(ToDouble(pair.value), origin.Y());
            else if (pair.code == 20) origin.Set(origin.X(), ToDouble(pair.value));
            else if (pair.code == 11) direction.Set(ToDouble(pair.value), direction.Y());
            else if (pair.code == 21) direction.Set(direction.X(), ToDouble(pair.value));
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        const GeoPoint throughPoint(origin.X() + direction.X(), origin.Y() + direction.Y());
        auto entity = std::make_unique<RayEntity>(IdGenerator::Next(), GeoRay(origin, throughPoint));
        entity->SetLayerName(layerName);
        return entity;
    }
    if (type == "TEXT") {
        GeoPoint position;
        std::string text;
        std::string layerName = "0";
        std::string styleName = "Standard";
        double height = 12.0;
        double rotationDegrees = 0.0;
        double widthFactor = 1.0;
        double obliqueDegrees = 0.0;
        int horizontalAlign = 0;
        int verticalAlign = 0;
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) position.Set(ToDouble(pair.value), position.Y());
            else if (pair.code == 20) position.Set(position.X(), ToDouble(pair.value));
            else if (pair.code == 1) text = pair.value;
            else if (pair.code == 40) height = ToDouble(pair.value);
            else if (pair.code == 50) rotationDegrees = ToDouble(pair.value);
            else if (pair.code == 41) widthFactor = ToDouble(pair.value);
            else if (pair.code == 51) obliqueDegrees = ToDouble(pair.value);
            else if (pair.code == 7) styleName = pair.value;
            else if (pair.code == 72) horizontalAlign = static_cast<int>(ToDouble(pair.value));
            else if (pair.code == 73) verticalAlign = static_cast<int>(ToDouble(pair.value));
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<TextEntity>(
            IdGenerator::Next(), position, text, height,
            rotationDegrees * 3.14159265358979323846 / 180.0,
            static_cast<TextHorizontalAlign>(horizontalAlign),
            static_cast<TextVerticalAlign>(verticalAlign),
            widthFactor == 0.0 ? 1.0 : widthFactor,
            obliqueDegrees * 3.14159265358979323846 / 180.0);
        entity->SetLayerName(layerName);
        entity->Style().textStyle = styleName;
        return entity;
    }
    if (type == "MTEXT") {
        GeoPoint position;
        std::string content;
        std::string layerName = "0";
        std::string styleName = "Standard";
        double height = 12.0;
        double rotationDegrees = 0.0;
        double referenceWidth = 0.0;
        double lineSpacing = 1.0;
        int attachment = 1;
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) position.Set(ToDouble(pair.value), position.Y());
            else if (pair.code == 20) position.Set(position.X(), ToDouble(pair.value));
            else if (pair.code == 1 || pair.code == 3) content += pair.value;
            else if (pair.code == 40) height = ToDouble(pair.value);
            else if (pair.code == 41) referenceWidth = ToDouble(pair.value);
            else if (pair.code == 43) lineSpacing = ToDouble(pair.value);
            else if (pair.code == 50) rotationDegrees = ToDouble(pair.value);
            else if (pair.code == 71) attachment = static_cast<int>(ToDouble(pair.value));
            else if (pair.code == 7) styleName = pair.value;
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<MTextEntity>(
            IdGenerator::Next(), position, content, height,
            rotationDegrees * 3.14159265358979323846 / 180.0,
            referenceWidth, lineSpacing <= 0.0 ? 1.0 : lineSpacing,
            static_cast<MTextAttachment>(attachment));
        entity->SetLayerName(layerName);
        entity->Style().textStyle = styleName;
        return entity;
    }
    if (type == "LWPOLYLINE") {
        std::vector<GeoPoint> points;
        std::vector<double> bulges;
        std::string layerName = "0";
        double pendingX = 0.0;
        bool hasX = false;
        bool closed = false;
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) {
                pendingX = ToDouble(pair.value);
                hasX = true;
            } else if (pair.code == 20 && hasX) {
                points.emplace_back(pendingX, ToDouble(pair.value));
                bulges.push_back(0.0);
                hasX = false;
            } else if (pair.code == 42) {
                if (!bulges.empty()) {
                    bulges.back() = ToDouble(pair.value);
                }
            } else if (pair.code == 70) {
                closed = (static_cast<int>(ToDouble(pair.value)) & 1) != 0;
            } else if (pair.code == 8) {
                layerName = pair.value;
            }
        }
        --i;
        if (closed && points.size() >= 2
            && std::abs(points.front().X() - points.back().X()) <= 1.0e-9
            && std::abs(points.front().Y() - points.back().Y()) <= 1.0e-9) {
            points.pop_back();
            if (!bulges.empty()) {
                bulges.pop_back();
            }
        }
        if (points.size() >= 2) {
            auto entity = std::make_unique<PolylineEntity>(
                IdGenerator::Next(), GeoPolyline(std::move(points), std::move(bulges), closed));
            entity->SetLayerName(layerName);
            return entity;
        }
        return nullptr;
    }
    if (type == "ARC") {
        GeoPoint center;
        double radius = 0.0;
        double startDegrees = 0.0;
        double endDegrees = 0.0;
        std::string layerName = "0";
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 10) center.Set(ToDouble(pair.value), center.Y());
            else if (pair.code == 20) center.Set(center.X(), ToDouble(pair.value));
            else if (pair.code == 40) radius = ToDouble(pair.value);
            else if (pair.code == 50) startDegrees = ToDouble(pair.value);
            else if (pair.code == 51) endDegrees = ToDouble(pair.value);
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<ArcEntity>(
            IdGenerator::Next(),
            GeoArc(
                center,
                radius,
                startDegrees * 3.14159265358979323846 / 180.0,
                endDegrees * 3.14159265358979323846 / 180.0));
        entity->SetLayerName(layerName);
        return entity;
    }
    if (type == "DIMENSION") {
        GeoPoint first;
        GeoPoint second;
        GeoPoint textPosition;
        std::string layerName = "0";
        std::string dimStyle = "Standard";
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 13) first.Set(ToDouble(pair.value), first.Y());
            else if (pair.code == 23) first.Set(first.X(), ToDouble(pair.value));
            else if (pair.code == 14) second.Set(ToDouble(pair.value), second.Y());
            else if (pair.code == 24) second.Set(second.X(), ToDouble(pair.value));
            else if (pair.code == 10) textPosition.Set(ToDouble(pair.value), textPosition.Y());
            else if (pair.code == 20) textPosition.Set(textPosition.X(), ToDouble(pair.value));
            else if (pair.code == 8) layerName = pair.value;
            else if (pair.code == 3) dimStyle = pair.value;
        }
        --i;
        auto entity = std::make_unique<DimensionEntity>(IdGenerator::Next(), first, second, textPosition);
        entity->SetKind(DimensionKind::Aligned);
        entity->SetLayerName(layerName);
        entity->SetDimensionStyleName(dimStyle);
        return entity;
    }
    if (type == "INSERT") {
        GeoPoint insertion;
        std::string blockName;
        std::string layerName = "0";
        double scale = 1.0;
        double rotationDegrees = 0.0;
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            const DxfPair& pair = pairs[i];
            if (pair.code == 2) blockName = pair.value;
            else if (pair.code == 10) insertion.Set(ToDouble(pair.value), insertion.Y());
            else if (pair.code == 20) insertion.Set(insertion.X(), ToDouble(pair.value));
            else if (pair.code == 41) scale = ToDouble(pair.value);
            else if (pair.code == 50) rotationDegrees = ToDouble(pair.value);
            else if (pair.code == 8) layerName = pair.value;
        }
        --i;
        auto entity = std::make_unique<BlockReferenceEntity>(
            IdGenerator::Next(), blockName, insertion,
            scale == 0.0 ? 1.0 : scale,
            rotationDegrees * 3.14159265358979323846 / 180.0);
        entity->SetLayerName(layerName);
        return entity;
    }

    // Unsupported entity: skip attribute pairs until next group 0.
    for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
    }
    --i;
    return nullptr;
}

bool IsAnonymousOrSpaceBlock(const std::string& name)
{
    return name.empty() || name[0] == '*';
}

void ParseBlocks(const std::vector<DxfPair>& pairs, std::size_t& i, CadDocument& document)
{
    for (; i < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "ENDSEC") {
            return;
        }
        if (pairs[i].code != 0 || pairs[i].value != "BLOCK") {
            continue;
        }

        BlockDefinition block;
        for (++i; i < pairs.size(); ++i) {
            if (pairs[i].code == 0) {
                if (pairs[i].value == "ENDBLK") {
                    break;
                }
                const std::string type = pairs[i].value;
                std::unique_ptr<Entity> entity = ParseOneEntity(pairs, i, type);
                if (entity) {
                    document.EnsureLayer(entity->LayerName());
                    block.entities.push_back(std::shared_ptr<Entity>(std::move(entity)));
                }
                continue;
            }
            if (pairs[i].code == 2 || pairs[i].code == 3) {
                if (block.name.empty() || pairs[i].code == 2) {
                    block.name = pairs[i].value;
                }
            } else if (pairs[i].code == 10) {
                block.baseX = ToDouble(pairs[i].value);
            } else if (pairs[i].code == 20) {
                block.baseY = ToDouble(pairs[i].value);
            }
        }
        if (!IsAnonymousOrSpaceBlock(block.name)) {
            document.AddBlockDefinition(std::move(block));
        }
    }
}

void ParseObjects(const std::vector<DxfPair>& pairs, std::size_t& i, CadDocument& document)
{
    for (; i < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "ENDSEC") {
            return;
        }
        if (pairs[i].code != 0 || pairs[i].value != "LAYOUT") {
            continue;
        }

        PageLayout layout;
        for (++i; i < pairs.size() && pairs[i].code != 0; ++i) {
            if (pairs[i].code == 1) layout.name = pairs[i].value;
            if (pairs[i].code == 40) layout.widthMm = ToDouble(pairs[i].value);
            if (pairs[i].code == 41) layout.heightMm = ToDouble(pairs[i].value);
            if (pairs[i].code == 140) layout.marginMm = ToDouble(pairs[i].value);
            if (pairs[i].code == 141) layout.scale = ToDouble(pairs[i].value);
            if (pairs[i].code == 70) layout.landscape = ToDouble(pairs[i].value) != 0.0;
        }
        --i;
        document.AddLayout(std::move(layout));
    }
}

void ParseEntities(const std::vector<DxfPair>& pairs, std::size_t& i, CadDocument& document)
{
    for (; i < pairs.size(); ++i) {
        if (pairs[i].code != 0) {
            continue;
        }
        if (pairs[i].value == "ENDSEC") {
            return;
        }

        const std::string type = pairs[i].value;
        std::unique_ptr<Entity> entity = ParseOneEntity(pairs, i, type);
        if (!entity) {
            continue;
        }
        document.EnsureLayer(entity->LayerName());
        document.AddEntity(std::move(entity));
    }
}


} // namespace

bool DxfService::ImportFile(const std::string& filePath, CadDocument& document)
{
    std::ifstream input(filePath);
    if (!input) {
        return false;
    }

    document.Clear();
    const std::vector<DxfPair> pairs = ReadPairs(input);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].code == 0 && pairs[i].value == "SECTION") {
            ++i;
            const std::string sectionName = ValueOrEmpty(pairs, i, 2);
            if (sectionName == "TABLES") {
                ParseTables(pairs, i, document);
            } else if (sectionName == "BLOCKS") {
                ParseBlocks(pairs, i, document);
            } else if (sectionName == "OBJECTS") {
                ParseObjects(pairs, i, document);
            } else if (sectionName == "ENTITIES") {
                ParseEntities(pairs, i, document);
            } else {
                for (; i < pairs.size(); ++i) {
                    if (pairs[i].code == 0 && pairs[i].value == "ENDSEC") {
                        break;
                    }
                }
            }
            continue;
        }
    }

    // Block definitions are parsed before ENTITIES; rebuild INSERT geometry caches.
    for (const auto& entity : document.Entities()) {
        if (!entity || entity->Type() != EntityType::BlockReference) {
            continue;
        }
        auto& ref = static_cast<BlockReferenceEntity&>(*entity);
        const BlockDefinition* def = document.FindBlockDefinition(ref.BlockName());
        if (def) {
            ref.RebuildCache(def);
        }
    }
    return true;
}

void WriteLwPolyline(
    std::ostream& output,
    const Entity& entity,
    const std::vector<GeoPoint>& points,
    bool closed,
    const std::vector<double>* bulges = nullptr)
{
    if (points.size() < 2) {
        return;
    }
    WritePair(output, 0, "LWPOLYLINE");
    WriteCommonEntityPairs(output, entity);
    WritePair(output, 90, static_cast<double>(points.size()));
    WritePair(output, 70, closed ? 1.0 : 0.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        WritePair(output, 10, points[i].X());
        WritePair(output, 20, points[i].Y());
        const double bulge = (bulges && i < bulges->size()) ? (*bulges)[i] : 0.0;
        if (std::abs(bulge) > 1.0e-12) {
            WritePair(output, 42, bulge);
        }
    }
}

void WriteEntityShapeFallback(std::ostream& output, const Entity& entity)
{
    const GeoShape shape = entity.Shaper();
    for (const GeoLine& segment : shape.Segments()) {
        WritePair(output, 0, "LINE");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, segment.Start().X());
        WritePair(output, 20, segment.Start().Y());
        WritePair(output, 11, segment.End().X());
        WritePair(output, 21, segment.End().Y());
    }
    for (const GeoCircle& circle : shape.Circles()) {
        WritePair(output, 0, "CIRCLE");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, circle.Center().X());
        WritePair(output, 20, circle.Center().Y());
        WritePair(output, 40, circle.Radius());
    }
    for (const GeoArc& arc : shape.Arcs()) {
        WritePair(output, 0, "ARC");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, arc.Center().X());
        WritePair(output, 20, arc.Center().Y());
        WritePair(output, 40, arc.Radius());
        WritePair(output, 50, arc.StartAngleRadians() * 180.0 / 3.14159265358979323846);
        WritePair(output, 51, arc.EndAngleRadians() * 180.0 / 3.14159265358979323846);
    }
    const auto& polylines = shape.Polylines();
    const auto& closedFlags = shape.PolylineClosed();
    for (std::size_t i = 0; i < polylines.size(); ++i) {
        const bool closed = i < closedFlags.size() && closedFlags[i];
        WriteLwPolyline(output, entity, polylines[i], closed);
    }
    for (const GeoSpline& spline : shape.Splines()) {
        const std::vector<GeoPoint> samples = DrawAlgorithms::SplinePreviewPoints(
            spline.Points(), spline.Mode(), spline.Degree(), spline.Knots());
        if (samples.size() >= 2) {
            WriteLwPolyline(output, entity, samples, false);
        }
    }
}

void WriteOneEntity(std::ostream& output, const Entity& entity)
{
    if (entity.Type() == EntityType::Line) {
        const auto& line = static_cast<const LineEntity&>(entity).Line();
        WritePair(output, 0, "LINE");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, line.Start().X());
        WritePair(output, 20, line.Start().Y());
        WritePair(output, 11, line.End().X());
        WritePair(output, 21, line.End().Y());
    } else if (entity.Type() == EntityType::Circle) {
        const auto& circle = static_cast<const CircleEntity&>(entity).Circle();
        WritePair(output, 0, "CIRCLE");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, circle.Center().X());
        WritePair(output, 20, circle.Center().Y());
        WritePair(output, 40, circle.Radius());
    } else if (entity.Type() == EntityType::Arc) {
        const auto& arc = static_cast<const ArcEntity&>(entity).Arc();
        WritePair(output, 0, "ARC");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, arc.Center().X());
        WritePair(output, 20, arc.Center().Y());
        WritePair(output, 40, arc.Radius());
        WritePair(output, 50, arc.StartAngleRadians() * 180.0 / 3.14159265358979323846);
        WritePair(output, 51, arc.EndAngleRadians() * 180.0 / 3.14159265358979323846);
    } else if (entity.Type() == EntityType::Ray) {
        const GeoRay& ray = static_cast<const RayEntity&>(entity).Ray();
        const double dx = ray.ThroughPoint().X() - ray.Origin().X();
        const double dy = ray.ThroughPoint().Y() - ray.Origin().Y();
        const double len = std::hypot(dx, dy);
        const double unitX = len > 1.0e-12 ? dx / len : 1.0;
        const double unitY = len > 1.0e-12 ? dy / len : 0.0;
        WritePair(output, 0, "RAY");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, ray.Origin().X());
        WritePair(output, 20, ray.Origin().Y());
        WritePair(output, 11, unitX);
        WritePair(output, 21, unitY);
    } else if (entity.Type() == EntityType::Rectangle) {
        const auto& rect = static_cast<const RectangleEntity&>(entity).Rectangle();
        WriteLwPolyline(
            output,
            entity,
            DrawAlgorithms::RectanglePoints(rect.Corner1(), rect.Corner2()),
            true);
    } else if (entity.Type() == EntityType::Polygon) {
        const auto& polygon = static_cast<const PolygonEntity&>(entity).Polygon();
        WriteLwPolyline(
            output,
            entity,
            DrawAlgorithms::RegularPolygonPoints(
                polygon.Center(),
                polygon.Radius(),
                polygon.Sides(),
                polygon.RotationRadians()),
            true);
    } else if (entity.Type() == EntityType::Polyline) {
        const auto& polyline = static_cast<const PolylineEntity&>(entity).Polyline();
        WriteLwPolyline(output, entity, polyline.Points(), polyline.Closed(), &polyline.Bulges());
    } else if (entity.Type() == EntityType::Text) {
        const auto& text = static_cast<const TextEntity&>(entity);
        WritePair(output, 0, "TEXT");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, text.Position().X());
        WritePair(output, 20, text.Position().Y());
        WritePair(output, 40, text.Height());
        WritePair(output, 1, text.Text());
        WritePair(output, 50, text.RotationRadians() * 180.0 / 3.14159265358979323846);
        WritePair(output, 41, text.WidthFactor());
        WritePair(output, 51, text.ObliqueAngleRadians() * 180.0 / 3.14159265358979323846);
        WritePair(output, 7, entity.Style().textStyle);
        WritePair(output, 72, static_cast<double>(static_cast<int>(text.HorizontalAlign())));
        WritePair(output, 73, static_cast<double>(static_cast<int>(text.VerticalAlign())));
    } else if (entity.Type() == EntityType::MText) {
        const auto& text = static_cast<const MTextEntity&>(entity);
        WritePair(output, 0, "MTEXT");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 10, text.Position().X());
        WritePair(output, 20, text.Position().Y());
        WritePair(output, 40, text.Height());
        WritePair(output, 41, text.ReferenceWidth());
        WritePair(output, 43, text.LineSpacingFactor());
        WritePair(output, 71, static_cast<double>(static_cast<int>(text.Attachment())));
        WritePair(output, 50, text.RotationRadians() * 180.0 / 3.14159265358979323846);
        WritePair(output, 7, entity.Style().textStyle);
        const std::string& content = text.Content();
        if (content.size() <= 250) WritePair(output, 1, content);
        else {
            WritePair(output, 1, content.substr(0, 250));
            for (std::size_t off = 250; off < content.size(); off += 250)
                WritePair(output, 3, content.substr(off, 250));
        }
    } else if (entity.Type() == EntityType::Dimension) {
        const auto& dim = static_cast<const DimensionEntity&>(entity);
        WritePair(output, 0, "DIMENSION");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 3, dim.DimensionStyleName());
        WritePair(output, 10, dim.TextPosition().X());
        WritePair(output, 20, dim.TextPosition().Y());
        WritePair(output, 13, dim.First().X());
        WritePair(output, 23, dim.First().Y());
        WritePair(output, 14, dim.Second().X());
        WritePair(output, 24, dim.Second().Y());
    } else if (entity.Type() == EntityType::BlockReference) {
        const auto& block = static_cast<const BlockReferenceEntity&>(entity);
        WritePair(output, 0, "INSERT");
        WriteCommonEntityPairs(output, entity);
        WritePair(output, 2, block.BlockName());
        WritePair(output, 10, block.InsertionPoint().X());
        WritePair(output, 20, block.InsertionPoint().Y());
        WritePair(output, 41, block.ScaleFactor());
        WritePair(output, 42, block.ScaleFactor());
        WritePair(output, 50, block.RotationRadians() * 180.0 / 3.14159265358979323846);
    } else {
        // Rectangle/Polygon/Arc covered above; remaining types (ellipse, spline, …)
        // fall back to discretized shape geometry so WBLOCK never writes empty blocks.
        WriteEntityShapeFallback(output, entity);
    }
}


bool DxfService::ExportFile(const std::string& filePath, const CadDocument& document)
{
    std::ofstream output(filePath);
    if (!output) {
        return false;
    }

    // TABLES section: layers, line types, text styles, dimension styles.
    WritePair(output, 0, "SECTION");
    WritePair(output, 2, "TABLES");

    WritePair(output, 0, "TABLE");
    WritePair(output, 2, "LTYPE");
    for (const LineTypeDefinition& lineType : document.LineTypes()) {
        WritePair(output, 0, "LTYPE");
        WritePair(output, 2, lineType.name);
        WritePair(output, 3, lineType.description);
        WritePair(output, 73, static_cast<double>(lineType.pattern.size()));
        for (double dash : lineType.pattern) {
            WritePair(output, 49, dash);
        }
    }
    WritePair(output, 0, "ENDTAB");

    WritePair(output, 0, "TABLE");
    WritePair(output, 2, "LAYER");
    for (const Layer& layer : document.Layers()) {
        WritePair(output, 0, "LAYER");
        WritePair(output, 2, layer.Name());
        int flags = 0;
        if (layer.IsFrozen()) flags |= 1;
        if (layer.IsLocked()) flags |= 4;
        WritePair(output, 70, static_cast<double>(flags));
        const int colorIndex = DxfIndexFromColor(layer.Color());
        WritePair(output, 62, static_cast<double>(layer.IsOn() ? colorIndex : -colorIndex));
        WritePair(output, 420, static_cast<double>(layer.Color()));
        WritePair(output, 6, layer.LineTypeName());
        if (IsDefaultLineWeight(layer.LineWeightMm())) {
            WritePair(output, 370, -3.0);
        } else {
            WritePair(output, 370, static_cast<double>(std::lround(layer.LineWeightMm() * 100.0)));
        }
        WritePair(output, 290, static_cast<double>(layer.IsPlottable() ? 1 : 0));
        WritePair(output, 440, static_cast<double>(layer.Transparency()));
        WritePair(output, 999, layer.Description());
    }
    WritePair(output, 0, "ENDTAB");

    WritePair(output, 0, "TABLE");
    WritePair(output, 2, "STYLE");
    for (const TextStyleDefinition& style : document.TextStyles()) {
        WritePair(output, 0, "STYLE");
        WritePair(output, 2, style.name);
        WritePair(output, 3, style.fontFamily);
        WritePair(output, 40, style.fixedHeight);
    }
    WritePair(output, 0, "ENDTAB");

    WritePair(output, 0, "TABLE");
    WritePair(output, 2, "DIMSTYLE");
    for (const DimensionStyle& dimStyle : document.DimensionStyles()) {
        WritePair(output, 0, "DIMSTYLE");
        WritePair(output, 2, dimStyle.name);
        WritePair(output, 140, dimStyle.textHeight);
        WritePair(output, 41, dimStyle.arrowSize);
        WritePair(output, 42, dimStyle.dimensionLineGap);
    }
    WritePair(output, 0, "ENDTAB");
    WritePair(output, 0, "ENDSEC");

    // BLOCKS section (include block geometry between BLOCK / ENDBLK).
    WritePair(output, 0, "SECTION");
    WritePair(output, 2, "BLOCKS");
    for (const BlockDefinition& block : document.BlockDefinitions()) {
        WritePair(output, 0, "BLOCK");
        WritePair(output, 2, block.name);
        WritePair(output, 10, block.baseX);
        WritePair(output, 20, block.baseY);
        WritePair(output, 3, block.name);
        for (const std::shared_ptr<Entity>& be : block.entities) {
            if (be) {
                WriteOneEntity(output, *be);
            }
        }
        WritePair(output, 0, "ENDBLK");
    }
    WritePair(output, 0, "ENDSEC");

    // ENTITIES section.
    WritePair(output, 0, "SECTION");
    WritePair(output, 2, "ENTITIES");
    for (const auto& entity : document.Entities()) {
        WriteOneEntity(output, *entity);
    }
    WritePair(output, 0, "ENDSEC");

    // OBJECTS section: layouts.
    WritePair(output, 0, "SECTION");
    WritePair(output, 2, "OBJECTS");
    for (const PageLayout& layout : document.Layouts()) {
        WritePair(output, 0, "LAYOUT");
        WritePair(output, 1, layout.name);
        WritePair(output, 40, layout.widthMm);
        WritePair(output, 41, layout.heightMm);
        WritePair(output, 140, layout.marginMm);
        WritePair(output, 141, layout.scale);
        WritePair(output, 70, static_cast<double>(layout.landscape ? 1 : 0));
    }
    WritePair(output, 0, "ENDSEC");

    WritePair(output, 0, "EOF");
    return true;
}

} // namespace cad
