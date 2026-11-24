#pragma once

#include "Base/Style.h"
#include <QString>
#include <QPen>
#include <vector>

namespace cad {

class CadDocument;

namespace LineTypeLibrary {

std::vector<LineTypeDefinition> ParseLinContent(const QString& content);
std::vector<LineTypeDefinition> ParseLinFile(const QString& path);

// AutoCAD acadiso.lin subset shipped with the app (used as default load source).
QString DefaultLinFileContent();
QString DefaultLinFilePath();

void ApplyPatternToPen(QPen& pen, const LineTypeDefinition& lineType, double patternScale = 1.0,
                       double pixelsPerModelUnit = 0.0);
const LineTypeDefinition* FindDefinition(const CadDocument& document, const std::string& name);
bool HasDefinition(const CadDocument& document, const std::string& name);

std::vector<LineTypeDefinition> StandardDefinitions();

} // namespace LineTypeLibrary

} // namespace cad
