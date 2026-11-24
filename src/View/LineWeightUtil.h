#pragma once

#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

constexpr double kDefaultLineWeightMm = -1.0;

inline bool IsDefaultLineWeight(const double mm)
{
    return mm < 0.0;
}

inline QString LineWeightTableLabel(const double mm)
{
    if (IsDefaultLineWeight(mm)) {
        return QStringLiteral("默认");
    }
    return QString::number(mm, 'f', 2);
}

inline QString LineWeightDialogLabel(const double mm)
{
    if (IsDefaultLineWeight(mm)) {
        return QStringLiteral("默认");
    }
    return QStringLiteral("%1 mm").arg(QString::number(mm, 'f', 2));
}

inline QString LineWeightMenuLabel(const double mm)
{
    if (IsDefaultLineWeight(mm)) {
        return QStringLiteral("默认");
    }
    return QStringLiteral("%1 毫米").arg(QString::number(mm, 'f', 2));
}

inline double LineWeightPreviewPenWidth(const double mm)
{
    if (IsDefaultLineWeight(mm) || mm <= 0.0) {
        return 1.0;
    }
    return std::clamp(1.0 + mm * 22.0, 1.0, 9.0);
}

inline double EffectiveLineWeightMm(const double mm)
{
    return IsDefaultLineWeight(mm) ? 0.0 : mm;
}

inline const std::vector<double>& StandardLineWeightsMm()
{
    static const std::vector<double> weights = {
        kDefaultLineWeightMm,
        0.00,
        0.05,
        0.09,
        0.13,
        0.15,
        0.18,
        0.20,
        0.25,
        0.30,
        0.35,
        0.40,
        0.50,
        0.53,
        0.60,
        0.70,
        0.80,
        0.90,
        1.00,
        1.06,
        1.20,
        1.40,
        1.58,
        2.00,
        2.11,
    };
    return weights;
}

inline bool LineWeightNearlyEqual(const double a, const double b)
{
    if (IsDefaultLineWeight(a) && IsDefaultLineWeight(b)) {
        return true;
    }
    return std::abs(a - b) < 0.001;
}

inline int IndexOfLineWeight(const double mm)
{
    const auto& weights = StandardLineWeightsMm();
    for (int i = 0; i < static_cast<int>(weights.size()); ++i) {
        if (LineWeightNearlyEqual(weights[static_cast<std::size_t>(i)], mm)) {
            return i;
        }
    }
    return 0;
}

} // namespace cad
