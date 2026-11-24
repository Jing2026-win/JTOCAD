#include "View/AcadColorPalette.h"

#include <QColor>
#include <cmath>
#include <limits>

namespace cad {

namespace {

constexpr int kAciMin = 1;
constexpr int kAciMax = 255;

std::uint32_t PackRgb(int red, int green, int blue)
{
    return (static_cast<std::uint32_t>(red & 0xFF) << 16)
        | (static_cast<std::uint32_t>(green & 0xFF) << 8)
        | static_cast<std::uint32_t>(blue & 0xFF);
}

std::uint32_t StandardAciColor(int index)
{
    switch (index) {
    case 1:
        return PackRgb(255, 0, 0);
    case 2:
        return PackRgb(255, 255, 0);
    case 3:
        return PackRgb(0, 255, 0);
    case 4:
        return PackRgb(0, 255, 255);
    case 5:
        return PackRgb(0, 0, 255);
    case 6:
        return PackRgb(255, 0, 255);
    case 7:
        return PackRgb(255, 255, 255);
    case 8:
        return PackRgb(128, 128, 128);
    case 9:
        return PackRgb(192, 192, 192);
    case 250:
        return PackRgb(51, 51, 51);
    case 251:
        return PackRgb(91, 91, 91);
    case 252:
        return PackRgb(131, 131, 131);
    case 253:
        return PackRgb(171, 171, 171);
    case 254:
        return PackRgb(211, 211, 211);
    case 255:
        return PackRgb(255, 255, 255);
    default:
        break;
    }

    if (index >= 10 && index <= 249) {
        const int offset = index - 10;
        const int row = offset / 10;
        const int column = offset % 10;
        const int hue = row * 15;
        const int saturation = column == 0 ? 0 : 240 - column * 20;
        const int value = column == 0 ? 255 - row * 6 : 255;
        const QColor color = QColor::fromHsv(
            hue % 360,
            std::clamp(saturation, 0, 255),
            std::clamp(value, 40, 255));
        return PackRgb(color.red(), color.green(), color.blue());
    }

    return PackRgb(255, 255, 255);
}

} // namespace

std::uint32_t AcadColorIndexToRgb(int index)
{
    if (index < kAciMin || index > kAciMax) {
        return PackRgb(255, 255, 255);
    }
    return StandardAciColor(index);
}

int RgbToAcadColorIndex(const std::uint32_t rgb)
{
    int bestIndex = 7;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = kAciMin; index <= kAciMax; ++index) {
        const std::uint32_t candidate = AcadColorIndexToRgb(index);
        const int dr = static_cast<int>((rgb >> 16) & 0xFF) - static_cast<int>((candidate >> 16) & 0xFF);
        const int dg = static_cast<int>((rgb >> 8) & 0xFF) - static_cast<int>((candidate >> 8) & 0xFF);
        const int db = static_cast<int>(rgb & 0xFF) - static_cast<int>(candidate & 0xFF);
        const double distance = static_cast<double>(dr * dr + dg * dg + db * db);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }
    return bestIndex;
}

QString AcadColorLabel(const int index, const std::uint32_t rgb)
{
    const std::uint32_t indexedRgb = AcadColorIndexToRgb(index);
    if (indexedRgb != (rgb & 0xFFFFFFU)) {
        return QStringLiteral("真彩色");
    }

    switch (index) {
    case 1:
        return QStringLiteral("红");
    case 2:
        return QStringLiteral("黄");
    case 3:
        return QStringLiteral("绿");
    case 4:
        return QStringLiteral("青");
    case 5:
        return QStringLiteral("蓝");
    case 6:
        return QStringLiteral("洋红");
    case 7:
        return QStringLiteral("白");
    case 8:
        return QStringLiteral("深灰");
    case 9:
        return QStringLiteral("浅灰");
    case 250:
        return QStringLiteral("黑灰");
    case 251:
        return QStringLiteral("深灰");
    case 252:
        return QStringLiteral("中灰");
    case 253:
        return QStringLiteral("浅灰");
    case 254:
        return QStringLiteral("淡灰");
    case 255:
        return QStringLiteral("白");
    default:
        break;
    }

    if (index >= 10 && index <= 249) {
        static const QStringList hueNames = {
            QStringLiteral("红"),
            QStringLiteral("橙"),
            QStringLiteral("橙黄"),
            QStringLiteral("黄"),
            QStringLiteral("黄绿"),
            QStringLiteral("绿"),
            QStringLiteral("绿青"),
            QStringLiteral("青"),
            QStringLiteral("青蓝"),
            QStringLiteral("蓝"),
            QStringLiteral("蓝紫"),
            QStringLiteral("紫"),
            QStringLiteral("紫红"),
            QStringLiteral("洋红"),
            QStringLiteral("红"),
            QStringLiteral("橙"),
            QStringLiteral("橙黄"),
            QStringLiteral("黄"),
            QStringLiteral("黄绿"),
            QStringLiteral("绿"),
            QStringLiteral("绿青"),
            QStringLiteral("青"),
            QStringLiteral("青蓝"),
            QStringLiteral("蓝"),
        };
        const int offset = index - 10;
        const int row = offset / 10;
        const int column = offset % 10;
        const QString hue = hueNames.value(row % hueNames.size());
        if (column == 0) {
            return QStringLiteral("暗%1").arg(hue);
        }
        return hue;
    }

    return QStringLiteral("白");
}

} // namespace cad
