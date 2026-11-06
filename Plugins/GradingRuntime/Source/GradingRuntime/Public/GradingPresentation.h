#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GradingPresentation.generated.h"

class UUserWidget;
class SGradingPaperView;

/** Keeps the existing bound paper image, with a clipped, draggable zoom viewport. */
UCLASS()
class GRADINGRUNTIME_API UGradingPaperViewport : public UContentWidget
{
    GENERATED_BODY()
public:
    void SetPaperSize(FVector2D Size);
    void SetZoom(float Value);
    float GetZoom() const { return Zoom; }
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void SynchronizeProperties() override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void OnSlotAdded(UPanelSlot* Slot) override;
    virtual void OnSlotRemoved(UPanelSlot* Slot) override;

private:
    FVector2D PaperSize = FVector2D(874.f, 1078.f);
    float Zoom = 1.f;
    TSharedPtr<SGradingPaperView> PaperView;
};

UCLASS()
class GRADINGRUNTIME_API UGradingOptionsSettingsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Grading|Options")
    static void ApplyWidgetSettings(UUserWidget* Widget);
    static void RefreshWorldWidgets(UWorld* World, float TextScale, float ExamZoom);
    static void ApplyWidgetValues(UUserWidget* Widget, float TextScale, float ExamZoom);
};
