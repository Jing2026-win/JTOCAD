#include "IO/LineTypeLibrary.h"

#include "Base/Tolerance.h"
#include "DataModel/CadDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace cad {
namespace LineTypeLibrary {

namespace {

double ParsePatternValue(const QString& token)
{
    bool ok = false;
    const double value = token.trimmed().toDouble(&ok);
    return ok ? value : 0.0;
}

void FinalizeCurrent(LineTypeDefinition& current, std::vector<LineTypeDefinition>& out)
{
    if (current.name.empty()) {
        return;
    }
    if (current.pattern.empty()) {
        current.pattern.push_back(0.0);
    }
    out.push_back(std::move(current));
}

} // namespace

std::vector<LineTypeDefinition> ParseLinContent(const QString& content)
{
    std::vector<LineTypeDefinition> result;
    LineTypeDefinition current;

    const QStringList lines = content.split(QChar('\n'), Qt::KeepEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(';')) {
            continue;
        }

        if (line.startsWith('*')) {
            if (!current.name.empty()) {
                FinalizeCurrent(current, result);
                current = LineTypeDefinition{};
            }

            const int comma = line.indexOf(',');
            if (comma > 1) {
                current.name = line.mid(1, comma - 1).trimmed().toStdString();
                current.description = line.mid(comma + 1).trimmed().toStdString();
            } else {
                current.name = line.mid(1).trimmed().toStdString();
                current.description = current.name;
            }
            continue;
        }

        if (line.startsWith('A', Qt::CaseInsensitive) && !current.name.empty()) {
            const QStringList tokens = line.split(',', Qt::SkipEmptyParts);
            current.pattern.clear();
            for (int i = 1; i < tokens.size(); ++i) {
                const double value = ParsePatternValue(tokens[i]);
                if (!Tolerance::IsZero(value)) {
                    current.pattern.push_back(value);
                }
            }
        }
    }

    if (!current.name.empty()) {
        FinalizeCurrent(current, result);
    }
    return result;
}

std::vector<LineTypeDefinition> ParseLinFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return ParseLinContent(QString::fromUtf8(file.readAll()));
}

QString DefaultLinFileContent()
{
    return QStringLiteral(
        "; AutoCAD ISO linetypes (subset)\n"
        "*ACAD_ISO02W100,ISO dash (space) __    __    __\n"
        "A,12,-3\n"
        "*ACAD_ISO03W100,ISO dash (space) and line __ __ __ __ __\n"
        "A,12,-3,12,-3,12,-3,12,-3\n"
        "*ACAD_ISO04W100,ISO long dash (space) ____    ____    ____\n"
        "A,24,-3\n"
        "*ACAD_ISO05W100,ISO long dash (space) and line ____ ____ ____ ____\n"
        "A,24,-3,6,-3\n"
        "*ACAD_ISO06W100,ISO long dash (space) and double dot ____ .. ____ .. ____\n"
        "A,24,-3,3,-3,3,-3\n"
        "*ACAD_ISO07W100,ISO dot . . . . . . . . . . . . . .\n"
        "A,0,-3\n"
        "*ACAD_ISO08W100,ISO long dash (space) and triple dot ____ ... ____ ... ____\n"
        "A,24,-3,3,-3,3,-3,3,-3\n"
        "*ACAD_ISO09W100,ISO long dash (space) and double dash ____ __ ____ __ ____\n"
        "A,24,-3,6,-3\n"
        "*ACAD_ISO10W100,ISO dash (space) and dot __ . __ . __ . __ . __\n"
        "A,12,-3,3,-3\n"
        "*ACAD_ISO11W100,ISO long dash (space) and double dot ____ .. ____ .. ____\n"
        "A,24,-3,3,-3\n"
        "*ACAD_ISO12W100,ISO dash (space) and double dot __ .. __ .. __ .. __\n"
        "A,12,-3,3,-3,3,-3\n"
        "*ACAD_ISO13W100,ISO double dash (space) and dot __ __ . __ __ . __ __ .\n"
        "A,12,-3,12,-3,3,-3\n"
        "*ACAD_ISO14W100,ISO dash (space) and triple dot __ ... __ ... __ ... __\n"
        "A,12,-3,3,-3,3,-3,3,-3\n"
        "*ACAD_ISO15W100,ISO double dash (space) and triple dot __ __ ... __ __ ...\n"
        "A,12,-3,12,-3,3,-3,3,-3,3,-3\n"
        "*BORDER,Border __ __ . __ __ . __ __ . __ __ . __ __ .\n"
        "A,12,-6,12,-6,12,-6,12,-6,0\n"
        "*BORDER2,Border (.5x) __.__.__.__.__.__.__.__.__.__.__.\n"
        "A,6,-3,6,-3,6,-3,6,-3,0\n"
        "*CENTER,Center ____ _ ____ _ ____ _ ____ _ ____ _ ____\n"
        "A,12,-3,3,-3\n"
        "*CENTER2,Center (.5x) ___ _ ___ _ ___ _ ___ _ ___ _ ___\n"
        "A,6,-1.5,1.5,-1.5\n"
        "*DASHED,Dashed __ __ __ __ __ __ __ __ __ __ __ __ __ _\n"
        "A,6,-3\n"
        "*DASHED2,Dashed (.5x) _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _\n"
        "A,3,-1.5\n"
        "*DIVIDE,Divide ____ . . ____ . . ____ . . ____ . . ____\n"
        "A,12,-3,3,-3,3,-3\n"
        "*DIVIDE2,Divide (.5x) __..__..__..__..__..__..__..__.._\n"
        "A,6,-1.5,1.5,-1.5,1.5,-1.5\n"
        "*DOT,Dot . . . . . . . . . . . . . . . . . . .\n"
        "A,0,-3\n"
        "*DOT2,Dot (.5x) ........................................\n"
        "A,0,-1.5\n"
        "*HIDDEN,Hidden __ __ __ __ __ __ __ __ __ __ __ __ __ __\n"
        "A,6,-3\n"
        "*HIDDEN2,Hidden (.5x) _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _\n"
        "A,3,-1.5\n"
        "*PHANTOM,Phantom ______  __  __  ______  __  __  ______\n"
        "A,24,-6,6,-6,6,-6\n"
        "*PHANTOM2,Phantom (.5x) ___ _ _ ___ _ _ ___ _ _ ___ _ _\n"
        "A,12,-3,3,-3,3,-3\n"
        "*DASHDOT,Dash dot __ . __ . __ . __ . __ . __ . __ . __\n"
        "A,12,-3,3,-3\n"
        "*DASHDOT2,Dash dot (.5x) _._._._._._._._._._._._._._._.\n"
        "A,6,-1.5,1.5,-1.5\n"
        "*TRACKS,Tracks -|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|\n"
        "A,12,-3,12,-3,0\n"
        "*FENCE,Fence -|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|\n"
        "A,6,-3,6,-3,6,-3,0\n");
}

QString DefaultLinFilePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString bundled = appDir + QStringLiteral("/Support/acadiso.lin");
    if (QFile::exists(bundled)) {
        return bundled;
    }

    const QString source = appDir + QStringLiteral("/../../Support/acadiso.lin");
    if (QFile::exists(source)) {
        return source;
    }
    return bundled;
}

void ApplyPatternToPen(QPen& pen, const LineTypeDefinition& lineType, double patternScale,
                       double pixelsPerModelUnit)
{
    if (lineType.pattern.empty()) {
        pen.setStyle(Qt::SolidLine);
        return;
    }

    bool hasPositive = false;
    for (double value : lineType.pattern) {
        if (value > Tolerance::Model) {
            hasPositive = true;
            break;
        }
    }
    if (!hasPositive) {
        pen.setStyle(Qt::DotLine);
        return;
    }

    QVector<qreal> dashes;
    dashes.reserve(static_cast<int>(lineType.pattern.size()));
    const double scale = std::max(patternScale, Tolerance::Model);
    // Qt dash entries are multiples of pen width. Cosmetic pens stay constant on
    // screen, so convert AutoCAD model-unit pattern × LTSCALE into device pixels
    // then divide by pen width. When pixelsPerModelUnit <= 0 (UI previews), keep
    // a convenient fixed-pixel preview scale.
    const double penWidth = std::max(1.0, pen.widthF() > 0.0 ? static_cast<double>(pen.widthF()) : 1.0);
    const double modelToDashUnit = (pixelsPerModelUnit > Tolerance::Model)
        ? (pixelsPerModelUnit / penWidth)
        : (1.0 / penWidth);
    for (double value : lineType.pattern) {
        const double modelLength = std::abs(value) * scale;
        const double dashUnit = modelLength * modelToDashUnit;
        if (dashUnit > Tolerance::Model) {
            dashes.push_back(dashUnit);
        }
    }
    if (dashes.isEmpty()) {
        pen.setStyle(Qt::SolidLine);
        return;
    }
    // CustomDashLine needs an even count.
    if (dashes.size() % 2 != 0) {
        dashes.push_back(dashes.back());
    }

    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern(dashes);
    pen.setDashOffset(0.0);
}

const LineTypeDefinition* FindDefinition(const CadDocument& document, const std::string& name)
{
    for (const LineTypeDefinition& lineType : document.LineTypes()) {
        if (lineType.name == name) {
            return &lineType;
        }
    }
    return nullptr;
}

bool HasDefinition(const CadDocument& document, const std::string& name)
{
    return FindDefinition(document, name) != nullptr;
}

std::vector<LineTypeDefinition> StandardDefinitions()
{
    return ParseLinContent(DefaultLinFileContent());
}

} // namespace LineTypeLibrary
} // namespace cad
