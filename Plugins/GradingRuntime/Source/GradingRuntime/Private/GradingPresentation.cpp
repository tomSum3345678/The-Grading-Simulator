#include "GradingPresentation.h"
#include "GradingOptionsSettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"

class SGradingPaperView : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGradingPaperView) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        SetClipping(EWidgetClipping::ClipToBoundsAlways);
        ChildSlot[Args._Content.Widget];
    }

    void SetPaper(FVector2D Size, float Value)
    {
        if (!FMath::IsNearlyEqual(Zoom, Value)) Pan = FVector2D::ZeroVector;
        BaseSize = Size;
        Zoom = Value;
        Invalidate(EInvalidateWidgetReason::Layout);
    }

    void SetContent(TSharedRef<SWidget> Content) { ChildSlot[Content]; }

    virtual FVector2D ComputeDesiredSize(float) const override { return BaseSize; }

    virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override
    {
        if (Children.Accepts(ChildSlot.GetWidget()->GetVisibility()))
        {
            Children.AddWidget(Geometry.MakeChild(ChildSlot.GetWidget(), BaseSize,
                FSlateLayoutTransform(Zoom, Offset(Geometry.GetLocalSize()))));
        }
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Zoom > 1.f && (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::MiddleMouseButton))
        {
            LastPointer = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
            return FReply::Handled().CaptureMouse(AsShared());
        }
        return FReply::Unhandled();
    }

    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!HasMouseCapture()) return FReply::Unhandled();
        const FVector2D Pointer = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
        Pan += Pointer - LastPointer;
        LastPointer = Pointer;
        ClampPan(Geometry.GetLocalSize());
        Invalidate(EInvalidateWidgetReason::Layout);
        return FReply::Handled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (HasMouseCapture() && (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::MiddleMouseButton))
            return FReply::Handled().ReleaseMouseCapture();
        return FReply::Unhandled();
    }

    virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Zoom <= 1.f) return FReply::Unhandled();
        if (Event.IsShiftDown()) Pan.X += Event.GetWheelDelta() * 90.f;
        else Pan.Y += Event.GetWheelDelta() * 90.f;
        ClampPan(Geometry.GetLocalSize());
        Invalidate(EInvalidateWidgetReason::Layout);
        return FReply::Handled();
    }

    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return Zoom > 1.f ? FCursorReply::Cursor(HasMouseCapture() ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand) : FCursorReply::Unhandled();
    }

private:
    FVector2D BaseSize = FVector2D(874.f, 1078.f);
    FVector2D Pan = FVector2D::ZeroVector;
    FVector2D LastPointer = FVector2D::ZeroVector;
    float Zoom = 1.f;

    FVector2D Offset(FVector2D ViewSize) const
    {
        const FVector2D Space = ViewSize - BaseSize * Zoom;
        return FVector2D(Space.X >= 0.f ? Space.X * .5f : FMath::Clamp(Space.X * .5f + Pan.X, Space.X, 0.f),
            Space.Y >= 0.f ? Space.Y * .5f : FMath::Clamp(Space.Y * .5f + Pan.Y, Space.Y, 0.f));
    }

    void ClampPan(FVector2D ViewSize)
    {
        const FVector2D Space = ViewSize - BaseSize * Zoom;
        const FVector2D Position = Offset(ViewSize);
        Pan = FVector2D(Space.X >= 0.f ? 0.f : Position.X - Space.X * .5f,
            Space.Y >= 0.f ? 0.f : Position.Y - Space.Y * .5f);
    }
};

void UGradingPaperViewport::SetPaperSize(FVector2D Size)
{
    PaperSize = FVector2D(FMath::Max(Size.X, 1.f), FMath::Max(Size.Y, 1.f));
    if (PaperView.IsValid()) PaperView->SetPaper(PaperSize, Zoom);
}

void UGradingPaperViewport::SetZoom(float Value)
{
    const float NewZoom = FMath::Clamp(FMath::IsFinite(Value) ? Value : 1.f, .75f, 2.f);
    if (FMath::IsNearlyEqual(Zoom, NewZoom)) return;
    Zoom = NewZoom;
    if (PaperView.IsValid()) PaperView->SetPaper(PaperSize, Zoom);
    SetToolTipText(Zoom > 1.f ? FText::FromString(TEXT("Drag to pan. Mouse wheel to scroll; Shift + wheel to scroll sideways.")) : FText::GetEmpty());
}

TSharedRef<SWidget> UGradingPaperViewport::RebuildWidget()
{
    SAssignNew(PaperView, SGradingPaperView)[GetContent() ? GetContent()->TakeWidget() : SNullWidget::NullWidget];
    PaperView->SetPaper(PaperSize, Zoom);
    return PaperView.ToSharedRef();
}

void UGradingPaperViewport::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (PaperView.IsValid()) PaperView->SetPaper(PaperSize, Zoom);
}

void UGradingPaperViewport::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    PaperView.Reset();
}

void UGradingPaperViewport::OnSlotAdded(UPanelSlot*)
{
    if (PaperView.IsValid()) PaperView->SetContent(GetContent() ? GetContent()->TakeWidget() : SNullWidget::NullWidget);
}

void UGradingPaperViewport::OnSlotRemoved(UPanelSlot*)
{
    if (PaperView.IsValid()) PaperView->SetContent(SNullWidget::NullWidget);
}

namespace
{
    TMap<TWeakObjectPtr<UWidget>, int32> BaseFontSizes;
    TMap<TWeakObjectPtr<UCanvasPanelSlot>, FAnchorData> BaseButtonLayouts;

    int32 ScaledSize(UWidget* Widget, int32 Current, float Scale)
    {
        const TWeakObjectPtr<UWidget> Key(Widget);
        const int32* Existing = BaseFontSizes.Find(Key);
        if (!Existing) Existing = &BaseFontSizes.Add(Key, Current);
        return FMath::Max(8, FMath::RoundToInt(*Existing * Scale));
    }

    void ScaleFont(UWidget* Widget, float Scale)
    {
        if (UTextBlock* Text = Cast<UTextBlock>(Widget))
        {
            FSlateFontInfo Font = Text->Font;
            const int32 Size = ScaledSize(Widget, Font.Size, Scale);
            if (Font.Size != Size) { Font.Size = Size; Text->SetFont(Font); }
            // Canvas menu buttons need room for the enlarged text without shifting
            // their centre or overlapping the neighbouring menu choices.
            UButton* Button = Cast<UButton>(Text->GetParent());
            UCanvasPanelSlot* Slot = Button ? Cast<UCanvasPanelSlot>(Button->Slot) : nullptr;
            if (Slot && Slot->GetSize().X >= 150.f)
            {
                const TWeakObjectPtr<UCanvasPanelSlot> Key(Slot);
                const FAnchorData* Base = BaseButtonLayouts.Find(Key);
                if (!Base) Base = &BaseButtonLayouts.Add(Key, Slot->GetLayout());
                if (Base->Anchors.Minimum == Base->Anchors.Maximum)
                {
                    FAnchorData Layout = *Base;
                    const float Extra = Base->Offsets.Right * (FMath::Max(1.f, Scale) - 1.f);
                    Layout.Offsets.Right += Extra;
                    Layout.Offsets.Left -= Extra * (.5f - Layout.Alignment.X);
                    if (Slot->GetPosition() != FVector2D(Layout.Offsets.Left, Layout.Offsets.Top)
                        || Slot->GetSize() != FVector2D(Layout.Offsets.Right, Layout.Offsets.Bottom))
                        Slot->SetLayout(Layout);
                }
            }
        }
        else if (UEditableTextBox* InputBox = Cast<UEditableTextBox>(Widget))
        {
            const int32 Size = ScaledSize(Widget, InputBox->WidgetStyle.Font.Size, Scale);
            if (InputBox->WidgetStyle.Font.Size != Size)
            {
                InputBox->WidgetStyle.Font.Size = Size;
                if (InputBox->GetCachedWidget().IsValid()) InputBox->SynchronizeProperties();
            }
        }
        else if (UEditableText* InputText = Cast<UEditableText>(Widget))
        {
            const int32 Size = ScaledSize(Widget, InputText->WidgetStyle.Font.Size, Scale);
            if (InputText->WidgetStyle.Font.Size != Size)
            {
                InputText->WidgetStyle.Font.Size = Size;
                if (InputText->GetCachedWidget().IsValid()) InputText->SynchronizeProperties();
            }
        }
    }

    void PreparePaper(UUserWidget* Widget, float Zoom)
    {
        UImage* Image = Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("Img_PaperDisplay")));
        if (!Image) return;
        UGradingPaperViewport* Viewport = Cast<UGradingPaperViewport>(Image->GetParent());
        if (!Viewport)
        {
            UCanvasPanel* Canvas = Cast<UCanvasPanel>(Image->GetParent());
            UCanvasPanelSlot* OldSlot = Cast<UCanvasPanelSlot>(Image->Slot);
            if (!Canvas || !OldSlot) return;
            const FAnchorData Layout = OldSlot->GetLayout();
            const int32 ZOrder = OldSlot->GetZOrder();
            const FVector2D Size = OldSlot->GetSize();
            Viewport = Widget->WidgetTree->ConstructWidget<UGradingPaperViewport>(UGradingPaperViewport::StaticClass(), TEXT("OptionsPaperViewport"));
            Viewport->SetPaperSize(Size);
            Image->RemoveFromParent();
            Viewport->SetContent(Image);
            UCanvasPanelSlot* NewSlot = Canvas->AddChildToCanvas(Viewport);
            NewSlot->SetLayout(Layout);
            // The old image was the first child, beneath the page controls. Adding
            // the wrapper last at equal Z would cover those existing buttons.
            NewSlot->SetZOrder(ZOrder - 1);
            Viewport->SetVisibility(ESlateVisibility::Visible);
            Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        Viewport->SetZoom(Zoom);
    }
}

void UGradingOptionsSettingsLibrary::ApplyWidgetValues(UUserWidget* Widget, float TextScale, float ExamZoom)
{
    if (!IsValid(Widget) || Widget->IsDesignTime() || !Widget->WidgetTree) return;
    const float Scale = FMath::Clamp(FMath::IsFinite(TextScale) ? TextScale : 1.f, .8f, 1.4f);
    Widget->WidgetTree->ForEachWidget([Scale](UWidget* Child) { ScaleFont(Child, Scale); });
    PreparePaper(Widget, ExamZoom);
}

void UGradingOptionsSettingsLibrary::ApplyWidgetSettings(UUserWidget* Widget)
{
    const UGradingUserSettings* Settings = UGradingUserSettings::GetGradingSettings();
    const FGradingOptions Values = Settings ? Settings->GetOptions() : FGradingOptions();
    ApplyWidgetValues(Widget, Values.TextScale, Values.ExamZoom);
}

void UGradingOptionsSettingsLibrary::RefreshWorldWidgets(UWorld* World, float TextScale, float ExamZoom)
{
    if (!World || !World->IsGameWorld()) return;
    TArray<UUserWidget*> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(), false);
    for (UUserWidget* Widget : Widgets) ApplyWidgetValues(Widget, TextScale, ExamZoom);
    for (auto It = BaseFontSizes.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    for (auto It = BaseButtonLayouts.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}
