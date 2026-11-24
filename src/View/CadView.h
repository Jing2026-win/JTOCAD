#pragma once

#include "Base/CadTypes.h"
#include "Base/GripTypes.h"
#include "Base/CursorSettings.h"
#include "Base/GraphicsConfig.h"
#include "Base/Style.h"
#include "Base/Tolerance.h"
#include "Base/ViewDisplaySettings.h"
#include "Command/DrawToolEngine.h"
#include "Command/EditKernel.h"
#include "Geometry/GeoFilletChamfer.h"
#include "DataModel/BlockDefinition.h"
#include "DataModel/DimensionEntity.h"
#include "Geometry/GeoPoint.h"
#include "Geometry/GeoShape.h"
#include "View/ObjectSnapModes.h"
#include "View/ObjectSnapUtil.h"
#include "View/DimensionRenderUtil.h"
#include "View/DynamicOptionInputWidget.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QEnterEvent>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QGraphicsSimpleTextItem;
class QLabel;
class QTimer;

namespace cad {

class CadDocument;
class CadScene;
class CommandManager;
class DynamicPointInputWidget;
class DynamicPolarInputWidget;
class DynamicOptionInputWidget;
class GripHoverMenuWidget;
class Entity;

struct LinearDimensionLayout;
struct AlignedDimensionLayout;

class CadView : public QGraphicsView {
public:
    explicit CadView(QWidget* parent = nullptr);

    void SetDocument(CadDocument* document);
    void SetScene(CadScene* scene);
    void SetCommandManager(CommandManager* commandManager);
    void SetStatusCallback(std::function<void(const QString&)> callback);
    void SetToolModeChangedCallback(std::function<void(ToolMode)> callback);

    void SetToolMode(ToolMode mode);
    ToolMode CurrentToolMode() const { return toolMode_; }
    bool CancelActiveCommand();
    bool CreateRectangularArray(const RectangularArrayOptions& options);
    bool BeginPathArray(const PathArrayOptions& options);
    bool BeginPolarArray(const PolarArrayOptions& options);

    void SetGridEnabled(bool enabled);
    void SetSnapEnabled(bool enabled);
    void SetOrthoEnabled(bool enabled);
    bool IsGridEnabled() const { return gridVisible_; }
    bool IsSnapEnabled() const { return snapEnabled_; }
    bool IsOrthoEnabled() const { return orthoEnabled_; }

    const ObjectSnapModes& SnapModes() const { return objectSnapModes_; }
    void SetObjectSnapModes(const ObjectSnapModes& modes);

    void SetDrawMethod(DrawMethod method);
    DrawMethod CurrentDrawMethod() const { return drawOptions_.method; }
    void SetPolygonSides(int sides);
    void SetCornerDistance(double distance);
    double CornerDistance() const { return cornerDistance_; }
    void SetFilletRadius(double radius);
    double FilletRadius() const { return filletRadius_; }
    void SetChamferDistances(double first, double second);
    double ChamferDistance1() const { return chamferDist1_; }
    double ChamferDistance2() const { return chamferDist2_; }
    void SetDrawOptions(const DrawToolOptions& options);
    const DrawToolOptions& DrawOptions() const { return drawOptions_; }

    const CursorSettings& GetCursorSettings() const { return cursorSettings_; }
    void SetCursorSettings(const CursorSettings& settings);

    const GraphicsConfig& GetGraphicsConfig() const { return graphicsConfig_; }
    void SetGraphicsConfig(const GraphicsConfig& config);
    bool IsHardwareAccelerationActive() const;

    int ViewRes() const { return viewRes_; }
    void SetViewRes(int value);
    double PixelsPerModelUnit() const;
    CurveDisplayOptions MakeCurveDisplayOptions(bool useTrueCurves = false) const;
    void RegenerateCurveDisplay();

    void ResetInteractionState();

    void ZoomExtents();
    void ZoomAll();
    void ZoomWindow(const GeoPoint& first, const GeoPoint& second);
    void ZoomIn(double factor = 2.0);
    void ZoomOut(double factor = 2.0);
    QRectF ComputeExtentsRect() const;
    void FitModelRect(const QRectF& modelRect, double marginFactor);

    void SetMirrorEraseSourceDefault(bool eraseSource) { mirrorEraseSourceDefault_ = eraseSource; }
    bool MirrorEraseSourceDefault() const { return mirrorEraseSourceDefault_; }

    struct PendingBlockInsert {
        std::string blockName;
        double scale = 1.0;
        double rotationRadians = 0.0;
        bool explodeOnInsert = false;
        std::vector<BlockAttributeValue> attributes;
    };
    void BeginInsertBlock(PendingBlockInsert params);
    bool HasPendingBlockInsert() const { return pendingBlockInsert_.has_value(); }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    bool IsDimensionTool(ToolMode mode) const;
    bool IsModifyTool(ToolMode mode) const;
    bool IsDrawingCommand(ToolMode mode) const;
    bool UsesObjectSnapAperture() const;
    CadCursorDisplay ResolveCursorDisplay() const;
    void UpdateCursorOverlay(const QPoint& viewportPos);
    void RepaintViewport();
    void ScheduleViewportRepaint(const QRectF& sceneDirtyRect = QRectF());
    void PaintCadCursorForeground(QPainter* painter) const;
    void ApplyCrosshairCursorShape(CadCursorDisplay display);
    void ApplyCursorSettingsToPickSnap();
    void ApplyGraphicsConfigToViewport();
    void EnsureViewportInputReady();
    void BeginSuppressViewportLeave(int holdMs = 80);
    bool ShouldSuppressViewportLeave() const;
    void RestorePointerInteractionFromGlobalCursor();
    void RefreshDrawInteractionAfterViewChange(const QPoint& viewportPos);
    void ZoomByFactorAtViewportPos(double factor, const QPoint& viewportPos);
    GeoPoint MapToModel(const QPoint& viewPoint) const;
    QPointF ToScenePoint(const GeoPoint& point) const;
    double ViewScale() const;
    double ModelToleranceFromPixels(double pixels) const;
    QRectF VisibleModelRect(double marginPixels = 0.0) const;
    void EnsureSceneCoversViewport();
    bool EntityIntersectsVisibleRect(const Entity& entity, const QRectF& visibleModelRect, double tolerance) const;
    GeoPoint ApplySnap(const GeoPoint& point, bool forDrag = false) const;
    ObjectSnapModes EffectiveObjectSnapModes() const;
    std::optional<GeoPoint> FindObjectSnapPoint(const GeoPoint& point) const;
    std::optional<ObjectSnapHit> FindObjectSnapHit(const GeoPoint& point) const;
    bool IsGridSnapActive(bool forDrag = false) const;
    GeoPoint SnapToGrid(const GeoPoint& point) const;
    std::vector<GeoShape> BuildTransientSnapShapes() const;
    void UpdateSnapMarker(const GeoPoint& point, bool visible, ObjectSnapKind kind = ObjectSnapKind::Unknown);
    void ClearSnapMarker();
    GeoPoint ApplyOrtho(const GeoPoint& point) const;
    GeoPoint ResolveInputPoint(const GeoPoint& rawPoint, bool forDrag = false) const;
    EntityId HitTest(const GeoPoint& point) const;
    EntityId HitTestTextAtPoint(const GeoPoint& point) const;
    EntityId ResolveTextEditHit(const GeoPoint& rawPoint) const;
    bool IsSelected(EntityId id) const;

    void HandleLeftPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers);
    void HandleTextToolPress(const GeoPoint& point);
    void HandleMTextToolPress(const GeoPoint& point);
    void UpdateDimensionSnapPreview(const GeoPoint& rawPoint);
    void ClearDimensionSnapPreview();
    void SetDimensionSnapGuidePaths(const QPainterPath& endpointPath, const QPainterPath& guidePath);
    bool IsDimensionDefPointPickActive() const;
    std::optional<GeoPoint> DimensionSnapPointAt(const GeoPoint& point) const;

    void HandleDimLinearPress(const GeoPoint& point);
    void HandleDimAlignedPress(const GeoPoint& point);
    void HandleDimRadiusPress(const GeoPoint& point);
    void HandleDimJoggedRadiusPress(const GeoPoint& point);
    void HandleDimDiameterPress(const GeoPoint& point);
    void HandleDimAngularPress(const GeoPoint& point);
    void HandleDimOrdinatePress(const GeoPoint& point);
    void BeginAngularPlacementReady(
        const GeoPoint& vertex,
        const GeoPoint& arm1,
        const GeoPoint& arm2,
        bool hasArcSpan = false,
        double arcStartRadians = 0.0,
        double arcEndRadians = 0.0,
        double sourceArcRadius = 0.0);
    bool TryBeginArcDimensionPick(const GeoPoint& point);
    DimensionStyle ActiveDimensionStyle() const;
    void ApplyDimensionStyleName(DimensionEntity& entity) const;
    GeoPoint ResolveDimensionDefPoint(const GeoPoint& point) const;
    void ResetDimLinearOptions();
    void ResetDimAlignedOptions();
    void ResetDimRadiusOptions();
    void ResetDimAngularOptions();
    void ResetDimOrdinateOptions();
    void PromptDimJoggedRadiusTextEdit(bool multiline);
    void PromptDimAngularTextEdit(bool multiline);
    void PromptDimAngularTextAngle();
    void CycleDimAngularQuadrantLock();
    void PromptDimJoggedRadiusTextAngle();
    void PromptDimDiameterTextEdit(bool multiline);
    void PromptDimDiameterTextAngle();
    void PromptDimLinearTextEdit(bool multiline);
    void PromptDimLinearTextAngle();
    void PromptDimAlignedTextEdit(bool multiline);
    void PromptDimAlignedTextAngle();
    void PromptDimRadiusTextEdit(bool multiline);
    void PromptDimRadiusTextAngle();
    void PromptDimOrdinateTextEdit(bool multiline);
    void PromptDimOrdinateTextAngle();
    void ToggleDimOrdinateOrientation();
    void ToggleDimLinearOrientation();
    void EditTextEntity(EntityId id);
    void EditMTextEntity(EntityId id);
    void HandleSelectPress(const GeoPoint& rawPoint, Qt::KeyboardModifiers modifiers);
    void HandleLeftRelease(const GeoPoint& point);
    void UpdatePreview(const GeoPoint& point);
    void SyncArcContinueOptions();
    void SyncSeaIncludedAngleOptions();
    void SyncSclChordLengthOptions();
    void NoteArcContinuation(const Entity& entity);
    void ClearPreview();
    void ClearPreviewDimLabel();
    void SetPreviewDimLabel(const LinearDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewAlignedDimLabel(const AlignedDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewRadiusDimLabel(const RadiusDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewJoggedRadiusDimLabel(const JoggedRadiusDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewDiameterDimLabel(const DiameterDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewAngularDimLabel(const AngularDimensionLayout& layout, const DimensionStyle& style);
    void SetPreviewOrdinateDimLabel(const OrdinateDimensionLayout& layout, const DimensionStyle& style);
    void AcquirePreviewItem();
    void ApplyPreviewPen();
    void SetPreviewPath(const QPainterPath& path);
    void UpdateBoxSelectPreview(const QPoint& currentViewPoint);
    void ClearBoxSelectPreview();
    void FinalizeBoxSelection(const QPoint& endViewPoint, Qt::KeyboardModifiers modifiers);
    void ApplyBoxSelection(const QRectF& modelRect, bool windowMode, bool addToSelection);
    bool EntityFullyContainedInRect(const Entity& entity, const QRectF& modelRect, double tolerance) const;
    bool EntityBoundsIntersectsRect(const Entity& entity, const GeoShape& shape, const QRectF& modelRect) const;
    bool EntityCrossesRect(const Entity& entity, const QRectF& modelRect, double tolerance) const;
    void ResetTransientState();
    void CancelDrag();
    void CancelGripDrag();
    enum class SplineGripEditMode {
        Stretch,
        AddPoint,
        RemovePoint,
        Tangent
    };
    bool IsSplineGripEditing() const { return splineGripEditing_; }
    bool IsSplineFitGripEdit() const;
    QVector<SplineGripEditMode> CurrentSplineGripEditModes() const;
    QString SplineGripEditPrompt() const;
    QVector<QString> SplineGripEditModeLabels() const;
    void CycleSplineGripEditMode();
    void SetSplineGripEditMode(SplineGripEditMode mode);
    void ApplySplineGripEditKeyword(const QString& keyword);
    void UpdateSplineGripEditHud(const QPoint& viewPos, const GeoPoint& cursorPoint);
    void UpdateSplineGripEditPreview(const GeoPoint& cursorPoint);
    GeoSpline MakeSplineGripPreview(const GeoSpline& source, const GeoPoint& cursorPoint) const;
    std::size_t SplineInsertIndex(const GeoSpline& spline, const GeoPoint& point) const;
    bool CommitSplineGripEdit(const GeoPoint& point);
    void CancelSplineGripEdit();
    void UpdateSplineGripHover(const QPoint& viewPos, const GeoPoint& rawPoint);
    void HideSplineGripHoverMenu();
    void EnsureSplineGripHoverMenu();
    QVector<DynCommandOption> SplineGripHoverOptions(
        const Entity& entity, int gripIndex, GripKind kind) const;
    void BeginSplineGripEdit(EntityId entityId, int gripIndex, GripKind kind, SplineGripEditMode mode, bool startDrag);
    void EmitStatus(const QString& text);
    bool IsDrawTool(ToolMode mode) const;
    void FinishMultiPointDraw();
    void FinishDrawCommand();
    bool IsDrawInProgress() const;
    bool IsContinuousLineDraw() const;
    bool IsContinuousPolylineDraw() const;
    bool IsSplineDraw() const;
    bool IsFilletChamferCommand() const;
    bool IsCommandOptionHudActive() const;
    QString PolylineOptionPrompt() const;
    void ResetPolylineDrawOptions();
    void CommitLastPolylineSegment();
    GeoPoint PolylineDrawEndTangent() const;
    bool UndoPolylineVertex();
    bool ClosePolyline();
    void HandlePolylineOptionKey(int key);
    void ApplyPolylineOptionKeyword(const QString& keyword);
    QVector<DynCommandOption> CurrentPolylineOptions() const;
    QString PolylineDynPromptText() const;
    QVector<DynCommandOption> CurrentSplineOptions() const;
    QString SplineDynPromptText() const;
    QString SplineOptionPrompt() const;
    void ApplySplineOptionKeyword(const QString& keyword);
    bool UndoSplineVertex();
    bool CloseSpline();
    void EnsureOptionInput();
    void HideOptionInput();
    void UpdatePolylineOptionInput(const QPoint& viewPos);
    void UpdateSplineOptionInput(const QPoint& viewPos);
    bool IsPolylineOptionEditing() const;
    enum class FilletChamferPhase {
        First,
        Second,
        Polyline,
        Radius,
        Dist1,
        Dist2,
        AngleLen,
        AngleVal,
        Trim,
        Method
    };
    enum class PolygonCommandPhase {
        Center,
        Option,
        Radius,
        Edge
    };
    bool IsPolygonCommand() const;
    void ResetPolygonSession();
    QString PolygonPromptText() const;
    QVector<DynCommandOption> CurrentPolygonHudOptions() const;
    void UpdatePolygonHud();
    void ApplyPolygonKeyword(const QString& keyword);
    void ResetFilletChamferSession();
    FilletOptions ActiveFilletOptions(bool shiftCorner) const;
    ChamferOptions ActiveChamferOptions(bool shiftCorner) const;
    QString FilletChamferSettingsStatus() const;
    QString FilletChamferPromptText() const;
    QString FilletChamferStatusBarText() const;
    QString FilletChamferNumericDefaultText() const;
    bool IsFilletChamferNumericPhase() const;
    QVector<DynCommandOption> CurrentFilletChamferHudOptions() const;
    void UpdateFilletChamferHud();
    void ApplyFilletChamferKeyword(const QString& keyword);
    void HandleFilletChamferPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers);
    void UpdateFilletChamferPreview(const GeoPoint& cursor);
    void RestoreFilletPreviewSources();
    void HideFilletPreviewSources(const std::vector<EntityId>& ids);
    void ShowFilletFirstEdgeHighlight();
    bool FinishFilletChamferPair(EntityId secondId, const GeoPoint& pick2, bool shiftCorner);
    enum class ArrayCommandPhase {
        Center,
        Edit,
        BasePoint,
        ItemCount,
        AngleBetween,
        FillAngle,
        RotateItems
    };
    bool IsArrayCommand() const;
    ArrayCommandPhase ArrayHomePhase() const;
    double PolarAngleBetweenDegrees() const;
    QString ArrayPromptText() const;
    QVector<DynCommandOption> CurrentArrayHudOptions() const;
    bool IsArrayNumericPhase() const;
    QString ArrayNumericDefaultText() const;
    void UpdateArrayHud();
    void ApplyArrayKeyword(const QString& keyword);
    void ReturnToArrayPrompt();
    bool CommitPolarArray();
    void BeginPolylineKeywordTyping(const QString& text);
    void EndPolylineKeywordEditUi();
    void KeepPolylineKeywordEditPointer(const QPoint& viewportPos);
    bool SupportsPolarTracking() const;
    bool TryGetPolarBasePoint(GeoPoint* baseOut) const;
    bool IsPolarAngleHintOnlyGuide() const;
    bool IsArcStartCenterEndEndPick() const;
    bool IsArcIncludedAnglePick() const;
    bool IsArcChordLengthPick() const;
    GeoPoint ArcChordLengthStartPoint() const;
    GeoPoint ArcChordLengthCenterPoint() const;
    bool IsArcStartEndRadiusPick() const;
    bool IsArcStartEndDirectionPick() const;
    bool IsArcStartEndAnglePick() const;
    bool IsArcCenterStartEndStartPick() const;
    bool IsArcCenterStartEndEndPick() const;
    bool IsArcContinuePick() const;
    bool IsCircleCenterRadiusPick() const;
    bool IsCircleCenterDiameterPick() const;
    bool IsCircleCenterPolarPick() const;
    bool IsCircleTanTanRadiusObjectPick() const;
    bool IsCircleTanTanRadiusValuePick() const;
    bool IsCircleTangentObjectPick() const;
    GeoPoint ResolveTtrObjectPickPoint(const GeoPoint& rawPoint) const;
    bool IsCircleTwoPointsPick() const;
    bool IsCircleThreePointsSecondPick() const;
    bool IsCircleThreePointsThirdPick() const;
    bool IsCirclePolarSegmentPick() const;
    GeoPoint ArcIncludedAngleStartPoint() const;
    GeoPoint ArcIncludedAngleCenterPoint() const;
    void ClearLineChain();
    bool UndoLineSegment();
    bool CloseLineChain();
    void EnsurePolarInput();
    void HidePolarInput();
    void UpdatePolarInput(const QPoint& viewPos, const GeoPoint& cursorPoint);
    void EnsureDynPromptLabel();
    void ShowDynPrompt(const QString& text, const QPoint& viewPos);
    void HideDynPrompt();
    GeoPoint ResolveDynamicLineEndpoint(
        const GeoPoint& base,
        const GeoPoint& cursorPoint,
        bool* trackingSnapped = nullptr) const;
    double ApplyPolarTracking(double angleRadians, bool* snapped) const;
    void UpdatePolarGuidePreview(
        const GeoPoint& base,
        const GeoPoint& end,
        double angleRadians,
        bool trackingSnapped,
        const GeoPoint* rawCursor = nullptr);
    void ClearPolarGuidePreview();
    void CommitDynamicLinePoint();
    void InvalidateGridBackground();
    void RefreshViewport();

    enum class MirrorPhase {
        Inactive,
        SelectObjects,
        PickAxisFirst,
        PickAxisSecond,
        ConfirmEraseSource
    };

    void BeginMirrorWorkflow();
    void ResetMirrorState();
    void FinishMirrorSelection();
    void HandleMirrorPress(const GeoPoint& point, Qt::KeyboardModifiers modifiers);
    void HandleMirrorRightClick();
    void ConfirmMirror(bool eraseSource);
    void SelectEntities(const std::vector<EntityId>& ids);
    void UpdateMirrorPreview(const GeoPoint& point);
    void UpdateArrayPreview(const GeoPoint& point);
    void ClearArraySession();

    void CopySelectedTextEntities();
    void BeginPasteText();
    void FinishPasteTextAt(const GeoPoint& point);
    void CancelPasteText();
    void EndTextPasteSession(bool notify);
    void UpdatePasteTextPreview(const GeoPoint& point);
    void EnsurePointInput();
    void ShowPastePointInput(const QPoint& viewPos, const GeoPoint& modelPoint);
    void HidePointInput();
    void UpdatePointInput(const QPoint& viewPos, const GeoPoint& modelPoint);
    bool ShouldShowDynPointInput() const;
    QString DynPointPrompt() const;
    void CommitDynPointInput();
    void UpdateTextHover(const GeoPoint& point);
    void ClearTextHover();

    bool IsAwaitingDimLinearPlacement() const;
    bool IsAwaitingDimAlignedPlacement() const;
    bool IsAwaitingDimRadiusPlacement() const;
    bool IsAwaitingDimJoggedRadiusPlacement() const;
    bool IsAwaitingDimDiameterPlacement() const;
    bool IsAwaitingDimAngularPlacement() const;
    bool IsAwaitingDimOrdinatePlacement() const;
    bool IsDimAngularOptionKey(int key) const;
    void HandleDimAngularOptionKey(int key);
    bool IsDimLinearOptionKey(int key) const;
    bool IsDimAlignedOptionKey(int key) const;
    bool IsDimRadiusOptionKey(int key) const;
    bool IsDimJoggedRadiusOptionKey(int key) const;
    bool IsDimDiameterOptionKey(int key) const;
    bool IsDimOrdinateOptionKey(int key) const;
    void HandleDimLinearOptionKey(int key);
    void HandleDimAlignedOptionKey(int key);
    void HandleDimRadiusOptionKey(int key);
    void HandleDimJoggedRadiusOptionKey(int key);
    void HandleDimDiameterOptionKey(int key);
    void HandleDimOrdinateOptionKey(int key);
    bool IsAwaitingDimPlacement() const;
    bool IsDimOptionKey(int key) const;
    void HandleDimOptionKey(int key);

    CadDocument* document_ = nullptr;
    CadScene* cadScene_ = nullptr;
    CommandManager* commandManager_ = nullptr;
    std::function<void(const QString&)> statusCallback_;
    std::function<void(ToolMode)> toolModeChangedCallback_;

    ToolMode toolMode_ = ToolMode::Select;
    bool gridVisible_ = true;
    bool snapEnabled_ = true;
    ObjectSnapModes objectSnapModes_ = ObjectSnapModes::Default();
    bool orthoEnabled_ = false;
    double gridSize_ = 20.0;
    double snapTolerancePixels_ = Tolerance::PickTolerancePixels;
    double snapAperturePixels_ = Tolerance::OsnapAperturePixels;
    CursorSettings cursorSettings_;
    GraphicsConfig graphicsConfig_;
    int viewRes_ = ViewDisplaySettings::kDefaultViewRes;
    bool cursorInsideView_ = false;
    bool crosshairPosValid_ = false;
    CadCursorDisplay cursorDisplay_ = CadCursorDisplay::CrosshairWithPickbox;
    QPoint lastCursorViewportPos_;

    bool panning_ = false;
    bool boxSelecting_ = false;
    bool draggingSelection_ = false;
    bool draggingGrip_ = false;
    bool dragMoved_ = false;
    EntityId gripEntityId_ = 0;
    int gripIndex_ = 0;
    EntityId editHelperEntityId_ = 0;
    QPoint lastPanPoint_;
    QPoint boxSelectStartView_;
    GeoPoint dragStartPoint_;
    GeoPoint dragCurrentPoint_;
    GeoPoint gripStartPoint_;
    // Coincident vertex grips dragged together (rectangle made of lines, etc.).
    std::vector<std::pair<EntityId, int>> gripDragGroup_;
    bool splineGripEditing_ = false;
    bool splineGripAwaitingClick_ = false;
    SplineGripEditMode splineGripEditMode_ = SplineGripEditMode::Stretch;
    GripKind splineGripKind_ = GripKind::Vertex;
    std::vector<GeoPoint> inputPoints_;
    // AutoCAD LINE chain vertices / entity ids for Undo / Close during continuous drawing.
    std::vector<GeoPoint> lineChainPoints_;
    std::vector<EntityId> lineChainEntityIds_;
    DrawToolOptions drawOptions_;
    PolygonCommandPhase polygonPhase_ = PolygonCommandPhase::Center;
    ArcContinueInfo lastArcContinue_;
    double cornerDistance_ = 10.0;
    double filletRadius_ = 10.0;
    double chamferDist1_ = 10.0;
    double chamferDist2_ = 10.0;
    double chamferLength_ = 10.0;
    double chamferAngleDeg_ = 45.0;
    ChamferMethod chamferMethod_ = ChamferMethod::Distance;
    bool filletTrim_ = true;
    bool filletMultiple_ = false;
    FilletChamferPhase filletPhase_ = FilletChamferPhase::First;
    QString filletChamferHudNotice_;
    int filletUndoDepth_ = 0;
    std::vector<EntityId> filletPreviewHiddenIds_;
    std::optional<GeoLine> filletFirstEdge_;
    double lastTextHeight_ = 12.0;
    QGraphicsPathItem* previewItem_ = nullptr;
    QGraphicsPathItem* polarGuideItem_ = nullptr;
    QGraphicsPathItem* polarRayItem_ = nullptr;
    QGraphicsPathItem* polarTrackingItem_ = nullptr;
    QGraphicsSimpleTextItem* previewDimLabelItem_ = nullptr;
    QGraphicsRectItem* boxSelectItem_ = nullptr;
    QGraphicsPathItem* snapMarkerItem_ = nullptr;
    QGraphicsPathItem* dimSnapEndpointItem_ = nullptr;
    QGraphicsPathItem* dimSnapGuideItem_ = nullptr;
    QRectF previewDirtyRect_;
    bool drawSegmentFinished_ = false;
    bool ignoreViewportLeave_ = false;
    int viewportLeaveSuppressDepth_ = 0;

    MirrorPhase mirrorPhase_ = MirrorPhase::Inactive;
    std::vector<EntityId> mirrorTargetIds_;
    GeoPoint mirrorAxisStart_;
    GeoPoint mirrorAxisEnd_;
    bool mirrorEraseSourceDefault_ = false;

    std::vector<EntityId> arraySourceIds_;
    PathArrayOptions pathArrayOptions_;
    PolarArrayOptions polarArrayOptions_;
    ArrayCommandPhase arrayPhase_ = ArrayCommandPhase::Center;
    std::optional<GeoPoint> arrayCenter_;

    std::vector<std::unique_ptr<Entity>> textClipboard_;
    GeoPoint textClipboardBase_;
    bool pasteTextPending_ = false;
    std::optional<PendingBlockInsert> pendingBlockInsert_;
    DynamicPointInputWidget* pointInput_ = nullptr;
    DynamicPolarInputWidget* polarInput_ = nullptr;
    DynamicOptionInputWidget* optionInput_ = nullptr;
    GripHoverMenuWidget* gripHoverMenu_ = nullptr;
    QTimer* gripHoverHideTimer_ = nullptr;
    EntityId hoverGripEntityId_ = 0;
    int hoverGripIndex_ = -1;
    bool polylineKeywordEditActive_ = false;
    QLabel* dynPromptLabel_ = nullptr;
    EntityId hoveredTextEntityId_ = 0;

    std::optional<LinearDimensionOrientation> dimLinearOrientationOverride_;
    std::string dimLinearOverrideText_;
    double dimLinearTextRotationRadians_ = 0.0;
    std::string dimAlignedOverrideText_;
    double dimAlignedTextRotationRadians_ = 0.0;
    double dimRadiusValue_ = 0.0;
    bool dimRadiusHasArcSpan_ = false;
    double dimRadiusArcStartRadians_ = 0.0;
    double dimRadiusArcEndRadians_ = 0.0;
    std::string dimRadiusOverrideText_;
    double dimRadiusTextRotationRadians_ = 0.0;

    bool dimAngularPlacementReady_ = false;
    bool dimAngularThreePointMode_ = false;
    bool dimAngularAwaitCircleArm2_ = false;
    GeoPoint dimAngularVertex_;
    GeoPoint dimAngularArm1_;
    GeoPoint dimAngularArm2_;
    bool dimAngularHasArcSpan_ = false;
    double dimAngularArcStartRadians_ = 0.0;
    double dimAngularArcEndRadians_ = 0.0;
    double dimAngularSourceArcRadius_ = 0.0;
    double dimAngularCircleRadius_ = 0.0;
    EntityId dimAngularLine1Id_ = 0;
    GeoPoint dimAngularLine1Pick_;
    int dimAngularQuadrantLock_ = -1;
    std::string dimAngularOverrideText_;
    double dimAngularTextRotationRadians_ = 0.0;

    std::optional<LinearDimensionOrientation> dimOrdinateOrientationOverride_;
    std::string dimOrdinateOverrideText_;
    double dimOrdinateTextRotationRadians_ = 0.0;
};

} // namespace cad
