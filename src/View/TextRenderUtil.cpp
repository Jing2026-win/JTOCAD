#include "View/TextRenderUtil.h"

#include "Base/Tolerance.h"
#include "DataModel/MTextEntity.h"
#include "DataModel/TextEntity.h"
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace cad {

namespace {

QString ToQ(const std::string& text) { return QString::fromUtf8(text.c_str()); }
QPointF ToScene(const GeoPoint& p) { return QPointF(p.X(), -p.Y()); }

QString ExpandSymbols(const QString& input)
{
    QString r = input;
    r.replace(QStringLiteral("%%d"), QChar(0x00B0));
    r.replace(QStringLiteral("%%D"), QChar(0x00B0));
    r.replace(QStringLiteral("%%p"), QChar(0x00B1));
    r.replace(QStringLiteral("%%P"), QChar(0x00B1));
    r.replace(QStringLiteral("%%c"), QChar(0x2205));
    r.replace(QStringLiteral("%%C"), QChar(0x2205));
    r.replace(QStringLiteral("%%%"), QStringLiteral("%"));
    return r;
}

std::string ExpandFieldsImpl(const std::string& content, const TextLayoutOptions& options)
{
    std::string result = content;
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::ostringstream dateStream;
    dateStream << std::put_time(&local, "%Y-%m-%d");
    std::ostringstream timeStream;
    timeStream << std::put_time(&local, "%H:%M:%S");
    const auto replaceAll = [&](const std::string& token, const std::string& value) {
        std::size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), value);
            pos += value.size();
        }
    };
    replaceAll("{DATE}", dateStream.str());
    replaceAll("{TIME}", timeStream.str());
    replaceAll("{FILENAME}", options.fileName.empty() ? "Drawing" : options.fileName);
    return result;
}

struct Run {
    QString text;
    double heightFactor = 1.0;
    double widthFactor = 1.0;
    double oblique = 0.0;
    bool bold = false, italic = false, underline = false, overline = false, strike = false;
    bool stacked = false;
    QString top, bottom;
    bool fraction = false;
};

struct Line { std::vector<Run> runs; double indent = 0.0; bool bullet = false; };

struct State {
    double heightFactor = 1.0, widthFactor = 1.0, oblique = 0.0;
    bool bold = false, italic = false, underline = false, overline = false, strike = false;
};

double ReadNum(const QString& s, int& i)
{
    int start = i;
    while (i < s.size() && (s.at(i).isDigit() || s.at(i) == QLatin1Char('.'))) ++i;
    return start == i ? 0.0 : s.mid(start, i - start).toDouble();
}

void SkipSemi(const QString& s, int& i)
{
    while (i < s.size() && s.at(i) != QLatin1Char(';')) ++i;
    if (i < s.size()) ++i;
}

std::vector<Line> ParseLines(const std::string& raw, double baseHeight)
{
    State st;
    std::vector<Line> lines;
    Line cur;
    auto flush = [&]() {
        if (!cur.runs.empty() || cur.bullet) { lines.push_back(cur); cur = Line{}; }
    };
    auto append = [&](const QString& t) {
        if (t.isEmpty()) return;
        if (!cur.runs.empty() && !cur.runs.back().stacked) {
            auto& b = cur.runs.back();
            if (b.heightFactor == st.heightFactor && b.widthFactor == st.widthFactor && b.bold == st.bold
                && b.italic == st.italic && b.underline == st.underline && b.overline == st.overline
                && b.strike == st.strike && qFuzzyCompare(b.oblique, st.oblique)) {
                b.text += t;
                return;
            }
        }
        Run r;
        r.text = t;
        r.heightFactor = st.heightFactor;
        r.widthFactor = st.widthFactor;
        r.oblique = st.oblique;
        r.bold = st.bold;
        r.italic = st.italic;
        r.underline = st.underline;
        r.overline = st.overline;
        r.strike = st.strike;
        cur.runs.push_back(r);
    };

    QString input = ExpandSymbols(ToQ(raw));
    for (int i = 0; i < input.size();) {
        if (input.at(i) == QLatin1Char('\\') && i + 1 < input.size()) {
            const QChar c = input.at(i + 1);
            if (c == QLatin1Char('P')) { flush(); i += 2; continue; }
            if (c == QLatin1Char('~')) { append(QString(QChar(0x00A0))); i += 2; continue; }
            if (c == QLatin1Char('S')) {
                i += 2;
                int sep = -1;
                bool frac = false;
                int start = i;
                while (i < input.size() && input.at(i) != QLatin1Char(';')) {
                    if (input.at(i) == QLatin1Char('^')) sep = i;
                    if (input.at(i) == QLatin1Char('/')) { sep = i; frac = true; }
                    ++i;
                }
                if (sep > start) {
                    Run r;
                    r.stacked = true;
                    r.top = input.mid(start, sep - start);
                    r.bottom = input.mid(sep + 1, i - sep - 1);
                    r.fraction = frac;
                    r.heightFactor = st.heightFactor;
                    cur.runs.push_back(r);
                }
                if (i < input.size()) ++i;
                continue;
            }
            if (c == QLatin1Char('L')) { st.underline = true; i += 2; continue; }
            if (c == QLatin1Char('l')) { st.underline = false; i += 2; continue; }
            if (c == QLatin1Char('O')) { st.overline = true; i += 2; continue; }
            if (c == QLatin1Char('o')) { st.overline = false; i += 2; continue; }
            if (c == QLatin1Char('K')) { st.strike = true; i += 2; continue; }
            if (c == QLatin1Char('k')) { st.strike = false; i += 2; continue; }
            if (c == QLatin1Char('W')) { i += 2; st.widthFactor = ReadNum(input, i); SkipSemi(input, i); continue; }
            if (c == QLatin1Char('H')) {
                i += 2;
                const double v = ReadNum(input, i);
                if (i < input.size() && input.at(i) == QLatin1Char('x')) { st.heightFactor = v; ++i; }
                else if (v > 0) st.heightFactor = v / baseHeight;
                SkipSemi(input, i);
                continue;
            }
            if (c == QLatin1Char('Q')) { i += 2; st.oblique = ReadNum(input, i); SkipSemi(input, i); continue; }
            if (c == QLatin1Char('F') || c == QLatin1Char('f')) { i += 2; SkipSemi(input, i); continue; }
            if (c == QLatin1Char('p')) {
                i += 2;
                const int start = i;
                SkipSemi(input, i);
                const QString spec = input.mid(start, i - start - 1);
                if (spec.contains(QLatin1Char('t'))) cur.bullet = true;
                continue;
            }
            if (c == QLatin1Char('A')) { i += 2; ReadNum(input, i); SkipSemi(input, i); continue; }
            append(QString(c));
            i += 2;
            continue;
        }
        if (input.at(i) == QLatin1Char('\n')) { flush(); ++i; continue; }
        if (input.at(i) == QLatin1Char('\t')) { append(QStringLiteral("        ")); ++i; continue; }
        append(QString(input.at(i)));
        ++i;
    }
    flush();
    if (lines.empty()) lines.push_back({});
    return lines;
}

QFont RunFont(const EntityStyle& style, const CadDocument* doc, double h, const Run& r)
{
    QFont f = ResolveTextFont(style, doc, h * r.heightFactor);
    f.setBold(r.bold);
    f.setItalic(r.italic);
    f.setUnderline(r.underline);
    f.setStrikeOut(r.strike);
    return f;
}

double RunWidth(const Run& r, const QFont& f)
{
    if (r.stacked) {
        QFontMetricsF m(f);
        return std::max(m.horizontalAdvance(r.top), m.horizontalAdvance(r.bottom)) * r.widthFactor;
    }
    return QFontMetricsF(f).horizontalAdvance(r.text) * r.widthFactor;
}

double LineWidth(const Line& line, const EntityStyle& style, const CadDocument* doc, double h)
{
    double w = line.indent + (line.bullet ? h * 0.6 : 0.0);
    for (const Run& r : line.runs) w += RunWidth(r, RunFont(style, doc, h, r));
    return w;
}

double LineHeight(const Line& line, double h)
{
    double maxH = h;
    for (const Run& r : line.runs) maxH = std::max(maxH, r.stacked ? h * r.heightFactor * 1.35 : h * r.heightFactor);
    return maxH;
}

double LineAscent(const Line& line, const EntityStyle& style, const CadDocument* doc, double h)
{
    double maxAscent = QFontMetricsF(ResolveTextFont(style, doc, h)).ascent();
    for (const Run& r : line.runs) {
        maxAscent = std::max(maxAscent, QFontMetricsF(RunFont(style, doc, h, r)).ascent());
    }
    return maxAscent;
}

struct StackedLayout {
    double textWidth = 0.0;
    double topBaseline = 0.0;
    double bottomBaseline = 0.0;
    double fractionLineY = 0.0;
    double topX = 0.0;
    double bottomX = 0.0;
};

StackedLayout ComputeStackedLayout(const Run& r, const QFont& f, double x, double y, double h)
{
    QFont small = f;
    small.setPixelSize(qMax(1, qRound(std::max(1.0, h * r.heightFactor * 0.65))));
    QFontMetricsF metrics(small);

    StackedLayout layout;
    layout.textWidth = std::max(metrics.horizontalAdvance(r.top), metrics.horizontalAdvance(r.bottom));
    layout.topX = x + (layout.textWidth - metrics.horizontalAdvance(r.top)) * 0.5;
    layout.bottomX = x + (layout.textWidth - metrics.horizontalAdvance(r.bottom)) * 0.5;

    if (r.fraction) {
        layout.topBaseline = y - h * 0.55;
        layout.bottomBaseline = y + h * 0.55;
        const double topBottom = layout.topBaseline + metrics.descent();
        const double bottomTop = layout.bottomBaseline - metrics.ascent();
        layout.fractionLineY = (topBottom + bottomTop) * 0.5;
    } else {
        layout.topBaseline = y - h * 0.75;
        layout.bottomBaseline = y + h * 0.15;
    }
    return layout;
}

void AppendRunToPath(QPainterPath& path, const Run& r, const QFont& f, double x, double y, double h)
{
    if (r.stacked) {
        QFont small = f;
        small.setPixelSize(qMax(1, qRound(std::max(1.0, h * r.heightFactor * 0.65))));
        const StackedLayout layout = ComputeStackedLayout(r, f, x, y, h);
        path.addText(layout.topX, layout.topBaseline, small, r.top);
        if (r.fraction) {
            path.moveTo(x, layout.fractionLineY);
            path.lineTo(x + layout.textWidth, layout.fractionLineY);
        }
        path.addText(layout.bottomX, layout.bottomBaseline, small, r.bottom);
        return;
    }

    if (!qFuzzyIsNull(r.oblique)) {
        QTransform shear;
        shear.shear(std::tan(qDegreesToRadians(r.oblique)), 0.0);
        QPainterPath runPath;
        runPath.addText(x, y, f, r.text);
        path.addPath(shear.map(runPath));
        return;
    }

    path.addText(x, y, f, r.text);
}

void PaintRun(QPainter* p, const Run& r, const QFont& f, double x, double y, double h)
{
    p->save();
    p->setFont(f);
    if (r.stacked) {
        QFont small = f;
        small.setPixelSize(qMax(1, qRound(std::max(1.0, h * r.heightFactor * 0.65))));
        p->setFont(small);
        const StackedLayout layout = ComputeStackedLayout(r, f, x, y, h);
        p->drawText(QPointF(layout.topX, layout.topBaseline), r.top);
        if (r.fraction) {
            p->drawLine(
                QPointF(x, layout.fractionLineY),
                QPointF(x + layout.textWidth, layout.fractionLineY));
        }
        p->drawText(QPointF(layout.bottomX, layout.bottomBaseline), r.bottom);
        p->restore();
        return;
    }
    if (!qFuzzyIsNull(r.oblique)) {
        QTransform tr;
        tr.shear(std::tan(qDegreesToRadians(r.oblique)), 0.0);
        p->setTransform(tr, true);
    }
    p->drawText(QPointF(x, y), r.text);
    if (r.overline) {
        QFontMetricsF m(f);
        p->drawLine(QPointF(x, y - m.ascent()), QPointF(x + m.horizontalAdvance(r.text), y - m.ascent()));
    }
    p->restore();
}

struct Layout { std::vector<Line> lines; double width = 0, height = 0; };

Layout BuildLayout(const MTextEntity& text, const EntityStyle& style, const TextLayoutOptions& opt)
{
    Layout layout;
    layout.lines = ParseLines(ExpandFieldsImpl(text.Content(), opt), text.Height());
    for (Line& line : layout.lines) {
        layout.width = std::max(layout.width, LineWidth(line, style, opt.document, text.Height()));
        layout.height += LineHeight(line, text.Height()) * text.LineSpacingFactor();
    }
    return layout;
}

} // namespace

std::string ExpandTextContent(const std::string& content, const TextLayoutOptions& options)
{
    return ExpandFieldsImpl(content, options);
}

std::string PlainTextFromMText(const std::string& content)
{
    QString plain = ExpandSymbols(ToQ(content));
    plain.replace(QStringLiteral("\\P"), QStringLiteral("\n"));
    plain.replace(QStringLiteral("\\~"), QString(QChar(0x00A0)));
    return plain.toUtf8().constData();
}

QFont ResolveTextFont(const EntityStyle& style, const CadDocument* document, double height)
{
    QString family = QStringLiteral("Arial");
    if (document) {
        for (const TextStyleDefinition& ts : document->TextStyles()) {
            if (ts.name == style.textStyle && !ts.fontFamily.empty()) {
                family = QString::fromUtf8(ts.fontFamily.c_str());
                break;
            }
        }
    }
    QFont font(family);
    font.setPixelSize(qMax(1, qRound(std::max(1.0, height))));
    return font;
}

QPointF TextAlignmentOffset(const QRectF& bounds, TextHorizontalAlign h, TextVerticalAlign v)
{
    double dx = 0, dy = 0;
    if (h == TextHorizontalAlign::Center || h == TextHorizontalAlign::Middle) dx = -bounds.width() * 0.5;
    else if (h == TextHorizontalAlign::Right) dx = -bounds.width();
    if (v == TextVerticalAlign::Top) dy = bounds.height();
    else if (v == TextVerticalAlign::Middle) dy = bounds.height() * 0.5;
    else if (v == TextVerticalAlign::Baseline) dy = -bounds.height() * 0.2;
    return QPointF(dx, dy);
}

QPointF MTextAttachmentOffset(const QRectF& bounds, MTextAttachment attachment)
{
    const double width = bounds.width();
    const double height = bounds.height();
    switch (attachment) {
    case MTextAttachment::TopCenter: return QPointF(-width * 0.5, 0);
    case MTextAttachment::TopRight: return QPointF(-width, 0);
    case MTextAttachment::MiddleLeft: return QPointF(0, -height * 0.5);
    case MTextAttachment::MiddleCenter: return QPointF(-width * 0.5, -height * 0.5);
    case MTextAttachment::MiddleRight: return QPointF(-width, -height * 0.5);
    case MTextAttachment::BottomLeft: return QPointF(0, -height);
    case MTextAttachment::BottomCenter: return QPointF(-width * 0.5, -height);
    case MTextAttachment::BottomRight: return QPointF(-width, -height);
    default: return QPointF(0, 0);
    }
}

namespace {

GeoPoint ToModel(const QPointF& point)
{
    return GeoPoint(point.x(), -point.y());
}

QRectF BoundsFromSceneCorners(const std::array<QPointF, 4>& corners, double padding = 2.0)
{
    QRectF bounds;
    for (const QPointF& corner : corners) {
        bounds = bounds.isValid() ? bounds.united(QRectF(corner, corner)) : QRectF(corner, corner);
    }
    if (!bounds.isValid()) {
        return {};
    }
    return bounds.normalized().adjusted(-padding, -padding, padding, padding);
}

double TextBoundsPadding(double textHeight)
{
    return std::max(8.0, textHeight * 0.5);
}

QRectF TextEntityLocalBox(const TextEntity& text, const EntityStyle& style, const TextLayoutOptions& options)
{
    QFont font = ResolveTextFont(style, options.document, text.Height());
    font.setStretch(static_cast<int>(std::clamp(text.WidthFactor(), 0.1, 10.0) * 100.0));
    const QRectF metrics = QFontMetricsF(font).boundingRect(ExpandSymbols(ToQ(text.Text())));
    const QPointF offset = TextAlignmentOffset(metrics, text.HorizontalAlign(), text.VerticalAlign());
    return QRectF(offset.x(), offset.y() - metrics.height(), metrics.width(), metrics.height()).normalized();
}

QRectF MTextEntityLocalBox(const MTextEntity& text, const EntityStyle& style, const TextLayoutOptions& options)
{
    const Layout layout = BuildLayout(text, style, options);
    const double width = text.ReferenceWidth() > 0 ? text.ReferenceWidth() : layout.width;
    const QRectF box(0.0, 0.0, width, layout.height);
    const QPointF attachment = MTextAttachmentOffset(box, text.Attachment());
    return QRectF(
        attachment.x(),
        attachment.y(),
        box.width(),
        box.height()).normalized();
}

std::array<QPointF, 4> CornersFromLocalBox(const QRectF& localBox, const GeoPoint& position, double rotationRadians)
{
    QTransform transform;
    transform.translate(ToScene(position).x(), ToScene(position).y());
    transform.rotate(-qRadiansToDegrees(rotationRadians));

    return {
        transform.map(localBox.topLeft()),
        transform.map(localBox.topRight()),
        transform.map(localBox.bottomRight()),
        transform.map(localBox.bottomLeft()),
    };
}

bool PointInsideModelRect(const GeoPoint& point, const QRectF& rect, double tolerance)
{
    return point.X() >= rect.left() - tolerance
        && point.X() <= rect.right() + tolerance
        && point.Y() >= rect.top() - tolerance
        && point.Y() <= rect.bottom() + tolerance;
}

bool SegmentIntersectsModelRect(
    const GeoPoint& start,
    const GeoPoint& end,
    const QRectF& rect,
    double tolerance)
{
    const double xmin = rect.left() - tolerance;
    const double xmax = rect.right() + tolerance;
    const double ymin = rect.top() - tolerance;
    const double ymax = rect.bottom() + tolerance;

    const double x0 = start.X();
    const double y0 = start.Y();
    const double dx = end.X() - x0;
    const double dy = end.Y() - y0;

    double t0 = 0.0;
    double t1 = 1.0;

    auto clip = [&](double p, double q) {
        if (std::abs(p) <= Tolerance::Model) {
            return q >= -Tolerance::Model;
        }
        const double r = q / p;
        if (p < 0.0) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
        return true;
    };

    if (!clip(-dx, x0 - xmin)) {
        return false;
    }
    if (!clip(dx, xmax - x0)) {
        return false;
    }
    if (!clip(-dy, y0 - ymin)) {
        return false;
    }
    if (!clip(dy, ymax - y0)) {
        return false;
    }

    return t0 <= t1 + Tolerance::Model;
}

std::array<GeoPoint, 4> ModelCornersFromSceneCorners(const std::array<QPointF, 4>& sceneCorners)
{
    return {
        ToModel(sceneCorners[0]),
        ToModel(sceneCorners[1]),
        ToModel(sceneCorners[2]),
        ToModel(sceneCorners[3]),
    };
}

double DistancePointToSegment(const GeoPoint& point, const GeoPoint& start, const GeoPoint& end)
{
    const double vx = end.X() - start.X();
    const double vy = end.Y() - start.Y();
    const double wx = point.X() - start.X();
    const double wy = point.Y() - start.Y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= Tolerance::Model * Tolerance::Model) {
        return std::hypot(wx, wy);
    }
    const double t = std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    return std::hypot(point.X() - (start.X() + t * vx), point.Y() - (start.Y() + t * vy));
}

} // namespace

std::array<QPointF, 4> TextEntitySceneCorners(const TextEntity& text, const TextLayoutOptions& options)
{
    EntityStyle style;
    if (options.style) {
        style = *options.style;
    }
    const QRectF localBox = TextEntityLocalBox(text, style, options);
    return CornersFromLocalBox(localBox, text.Position(), text.RotationRadians());
}

std::array<QPointF, 4> MTextEntitySceneCorners(const MTextEntity& text, const TextLayoutOptions& options)
{
    EntityStyle style;
    if (options.style) {
        style = *options.style;
    }
    const QRectF localBox = MTextEntityLocalBox(text, style, options);
    return CornersFromLocalBox(localBox, text.Position(), text.RotationRadians());
}

std::array<GeoPoint, 4> TextLikeEntityModelCorners(const Entity& entity, const TextLayoutOptions& options)
{
    if (entity.Type() == EntityType::Text) {
        return ModelCornersFromSceneCorners(
            TextEntitySceneCorners(static_cast<const TextEntity&>(entity), options));
    }
    if (entity.Type() == EntityType::MText) {
        return ModelCornersFromSceneCorners(
            MTextEntitySceneCorners(static_cast<const MTextEntity&>(entity), options));
    }
    return {};
}

bool TextLikeEntityCrossesModelRect(
    const Entity& entity,
    const QRectF& modelRect,
    double tolerance,
    const TextLayoutOptions& options)
{
    if (entity.Type() != EntityType::Text && entity.Type() != EntityType::MText) {
        return false;
    }

    const std::array<GeoPoint, 4> corners = TextLikeEntityModelCorners(entity, options);
    const QRectF rect = modelRect.normalized();
    for (const GeoPoint& corner : corners) {
        if (PointInsideModelRect(corner, rect, tolerance)) {
            return true;
        }
    }

    for (std::size_t i = 0; i < corners.size(); ++i) {
        const GeoPoint& start = corners[i];
        const GeoPoint& end = corners[(i + 1) % corners.size()];
        if (SegmentIntersectsModelRect(start, end, rect, tolerance)) {
            return true;
        }
    }

    return false;
}

double TextLikeEntityDistanceToModelPoint(
    const Entity& entity,
    const GeoPoint& point,
    const TextLayoutOptions& options)
{
    if (entity.Type() != EntityType::Text && entity.Type() != EntityType::MText) {
        return std::numeric_limits<double>::max();
    }
    if (TextLikeEntityContainsModelPoint(entity, point, options)) {
        return 0.0;
    }

    const std::array<GeoPoint, 4> corners = TextLikeEntityModelCorners(entity, options);
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const GeoPoint& start = corners[i];
        const GeoPoint& end = corners[(i + 1) % corners.size()];
        bestDistance = std::min(bestDistance, DistancePointToSegment(point, start, end));
    }
    return bestDistance;
}

bool TextLikeEntityFullyInsideModelRect(
    const Entity& entity,
    const QRectF& modelRect,
    double tolerance,
    const TextLayoutOptions& options)
{
    if (entity.Type() != EntityType::Text && entity.Type() != EntityType::MText) {
        return false;
    }

    const std::array<GeoPoint, 4> corners = TextLikeEntityModelCorners(entity, options);
    const QRectF rect = modelRect.normalized();
    for (const GeoPoint& corner : corners) {
        if (!PointInsideModelRect(corner, rect, tolerance)) {
            return false;
        }
    }
    return true;
}

void PaintTextEntity(QPainter* painter, const TextEntity& text, const EntityStyle& style, const TextLayoutOptions& options)
{
    QFont font = ResolveTextFont(style, options.document, text.Height());
    font.setStretch(static_cast<int>(std::clamp(text.WidthFactor(), 0.1, 10.0) * 100.0));
    const QRectF bounds = QFontMetricsF(font).boundingRect(ExpandSymbols(ToQ(text.Text())));
    const QPointF offset = TextAlignmentOffset(bounds, text.HorizontalAlign(), text.VerticalAlign());

    QPainterPath localPath;
    localPath.addText(offset.x(), offset.y(), font, ExpandSymbols(ToQ(text.Text())));

    painter->save();
    painter->setClipping(false);
    painter->translate(ToScene(text.Position()));
    painter->rotate(-qRadiansToDegrees(text.RotationRadians()));
    painter->setPen(Qt::NoPen);
    const QColor color = options.overrideColor.value_or(QColor(style.red, style.green, style.blue));
    painter->setBrush(color);
    painter->fillPath(localPath, painter->brush());
    painter->restore();
}

QPainterPath TextEntityScenePath(const TextEntity& text, const TextLayoutOptions& options)
{
    EntityStyle style;
    if (options.style) {
        style = *options.style;
    }

    QFont font = ResolveTextFont(style, options.document, text.Height());
    font.setStretch(static_cast<int>(std::clamp(text.WidthFactor(), 0.1, 10.0) * 100.0));
    const QRectF bounds = QFontMetricsF(font).boundingRect(ExpandSymbols(ToQ(text.Text())));
    const QPointF offset = TextAlignmentOffset(bounds, text.HorizontalAlign(), text.VerticalAlign());

    QPainterPath localPath;
    localPath.addText(offset.x(), offset.y(), font, ExpandSymbols(ToQ(text.Text())));

    QTransform transform;
    transform.translate(ToScene(text.Position()).x(), ToScene(text.Position()).y());
    transform.rotate(-qRadiansToDegrees(text.RotationRadians()));
    return transform.map(localPath);
}

QPainterPath MTextEntityScenePath(const MTextEntity& text, const TextLayoutOptions& options)
{
    EntityStyle style;
    if (options.style) {
        style = *options.style;
    }

    const Layout layout = BuildLayout(text, style, options);
    const double boxW = text.ReferenceWidth() > 0 ? text.ReferenceWidth() : layout.width;
    const QRectF box(0, 0, boxW, layout.height);

    QPainterPath localPath;
    double y = 0;
    for (const Line& line : layout.lines) {
        const double lineHeight = LineHeight(line, text.Height()) * text.LineSpacingFactor();
        const double baseline = y + LineAscent(line, style, options.document, text.Height());
        double x = line.indent;
        const double lineWidth = LineWidth(line, style, options.document, text.Height());
        if (text.ParagraphAlign() == MTextParagraphAlign::Center) {
            x += (boxW - lineWidth) * 0.5;
        } else if (text.ParagraphAlign() == MTextParagraphAlign::Right) {
            x += boxW - lineWidth;
        }
        if (line.bullet) {
            QFont bulletFont = ResolveTextFont(style, options.document, text.Height());
            localPath.addText(x, baseline, bulletFont, QStringLiteral("\xE2\x80\xA2"));
            x += text.Height() * 0.6;
        }
        for (const Run& run : line.runs) {
            const QFont font = RunFont(style, options.document, text.Height(), run);
            AppendRunToPath(localPath, run, font, x, baseline, text.Height());
            x += RunWidth(run, font);
        }
        y += lineHeight;
    }

    const QPointF attachment = MTextAttachmentOffset(box, text.Attachment());
    QTransform transform;
    transform.translate(ToScene(text.Position()).x(), ToScene(text.Position()).y());
    transform.rotate(-qRadiansToDegrees(text.RotationRadians()));
    transform.translate(attachment.x(), attachment.y());
    return transform.map(localPath);
}

QRectF TextEntityBounds(const TextEntity& text, const TextLayoutOptions& options)
{
    const QPainterPath path = TextEntityScenePath(text, options);
    if (path.isEmpty()) {
        return {};
    }
    const double pad = TextBoundsPadding(text.Height());
    return path.boundingRect().adjusted(-pad, -pad, pad, pad);
}

QRectF MTextEntityBounds(const MTextEntity& text, const TextLayoutOptions& options)
{
    const QPainterPath path = MTextEntityScenePath(text, options);
    if (path.isEmpty()) {
        return {};
    }
    const double pad = TextBoundsPadding(text.Height());
    return path.boundingRect().adjusted(-pad, -pad, pad, pad);
}

QRectF TextLikeEntityBounds(const Entity& entity, const TextLayoutOptions& options)
{
    if (entity.Type() == EntityType::Text) return TextEntityBounds(static_cast<const TextEntity&>(entity), options);
    if (entity.Type() == EntityType::MText) return MTextEntityBounds(static_cast<const MTextEntity&>(entity), options);
    return {};
}

QPainterPath TextLikeEntityScenePath(const Entity& entity, const TextLayoutOptions& options)
{
    if (entity.Type() == EntityType::Text) {
        return TextEntityScenePath(static_cast<const TextEntity&>(entity), options);
    }
    if (entity.Type() == EntityType::MText) {
        return MTextEntityScenePath(static_cast<const MTextEntity&>(entity), options);
    }
    return {};
}

bool TextLikeEntityContainsModelPoint(
    const Entity& entity,
    const GeoPoint& point,
    const TextLayoutOptions& options,
    const double hitToleranceModel)
{
    const QPainterPath path = TextLikeEntityScenePath(entity, options);
    if (path.isEmpty()) {
        return false;
    }

    const QPointF scenePoint(point.X(), -point.Y());
    if (path.contains(scenePoint)) {
        return true;
    }
    if (hitToleranceModel <= Tolerance::Model) {
        return false;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(hitToleranceModel * 2.0);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(path).contains(scenePoint);
}

void PaintTextGlyphHoverGlow(QPainter* painter, const QPainterPath& path)
{
    if (path.isEmpty()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    static constexpr QColor glowColor(96, 196, 255);
    static constexpr std::array<int, 5> glowWidths = {10, 7, 5, 3, 1};
    static constexpr std::array<int, 5> glowAlphas = {25, 40, 65, 100, 160};
    for (std::size_t i = 0; i < glowWidths.size(); ++i) {
        QPen pen(QColor(glowColor.red(), glowColor.green(), glowColor.blue(), glowAlphas[i]));
        pen.setWidth(glowWidths[i]);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->drawPath(path);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(180, 228, 255, 90));
    painter->drawPath(path);
    painter->restore();
}

void PaintTextGlyphSelectionOutline(QPainter* painter, const QPainterPath& path)
{
    if (path.isEmpty()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    QPen selectPen(QColor(255, 210, 64));
    selectPen.setCosmetic(true);
    selectPen.setWidth(2);
    selectPen.setJoinStyle(Qt::RoundJoin);
    selectPen.setCapStyle(Qt::RoundCap);
    painter->setPen(selectPen);
    painter->drawPath(path);
    painter->restore();
}

void PaintMTextEntity(QPainter* painter, const MTextEntity& text, const EntityStyle& style, const TextLayoutOptions& options)
{
    if (text.BackgroundMask()) {
        const Layout layout = BuildLayout(text, style, options);
        const double boxW = text.ReferenceWidth() > 0 ? text.ReferenceWidth() : layout.width;
        const QRectF box(0, 0, boxW, layout.height);

        painter->save();
        painter->setClipping(false);
        painter->translate(ToScene(text.Position()));
        painter->rotate(-qRadiansToDegrees(text.RotationRadians()));
        painter->translate(MTextAttachmentOffset(box, text.Attachment()));
        painter->fillRect(box.adjusted(-2, -2, 2, 2), QColor(0, 0, 0));
        painter->restore();
    }

    const QPainterPath path = MTextEntityScenePath(text, options);
    if (path.isEmpty()) {
        return;
    }

    painter->save();
    painter->setClipping(false);
    painter->setPen(Qt::NoPen);
    const QColor color = options.overrideColor.value_or(QColor(style.red, style.green, style.blue));
    painter->setBrush(color);
    painter->fillPath(path, painter->brush());
    painter->restore();
}

} // namespace cad
