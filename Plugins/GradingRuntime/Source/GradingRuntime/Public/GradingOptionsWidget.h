#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GradingOptionsSettings.h"
#include "GradingOptionsWidget.generated.h"

class USlider;
class UCheckBox;
class UComboBoxString;
class UTextBlock;
class UVerticalBox;

/** A focused options overlay; opening and closing it never changes game pause state. */
UCLASS()
class GRADINGRUNTIME_API UGradingOptionsPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    UGradingOptionsPanel(const FObjectInitializer& ObjectInitializer);
    void Configure(UUserWidget* InOriginMenu);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
    UPROPERTY() UUserWidget* OriginMenu = nullptr;
    UPROPERTY() UGradingUserSettings* Settings = nullptr;
    UPROPERTY() USlider* MasterSlider = nullptr;
    UPROPERTY() USlider* SFXSlider = nullptr;
    UPROPERTY() USlider* MusicSlider = nullptr;
    UPROPERTY() USlider* SensitivitySlider = nullptr;
    UPROPERTY() USlider* TextSlider = nullptr;
    UPROPERTY() USlider* ZoomSlider = nullptr;
    UPROPERTY() UCheckBox* MusicEnabledBox = nullptr;
    UPROPERTY() UComboBoxString* WindowModeBox = nullptr;
    UPROPERTY() UComboBoxString* QualityBox = nullptr;
    UPROPERTY() UTextBlock* MasterValue = nullptr;
    UPROPERTY() UTextBlock* SFXValue = nullptr;
    UPROPERTY() UTextBlock* MusicValue = nullptr;
    UPROPERTY() UTextBlock* SensitivityValue = nullptr;
    UPROPERTY() UTextBlock* TextValue = nullptr;
    UPROPERTY() UTextBlock* ZoomValue = nullptr;
    UPROPERTY() UTextBlock* StatusLabel = nullptr;
    FDelegateHandle SettingsChangedHandle;
    bool bRefreshing = false;
    bool bOriginWasEnabled = true;
    bool bOriginRestored = false;
    void RefreshSettings(const FGradingOptions& Options);
    void RestoreOrigin();
    bool CanEdit() const;
    UFUNCTION() void MasterChanged(float Value);
    UFUNCTION() void SFXChanged(float Value);
    UFUNCTION() void MusicChanged(float Value);
    UFUNCTION() void SensitivityChanged(float Value);
    UFUNCTION() void TextChanged(float Value);
    UFUNCTION() void ZoomChanged(float Value);
    UFUNCTION() void MusicEnabledChanged(bool bEnabled);
    UFUNCTION() void WindowModeChanged(FString Selected, ESelectInfo::Type SelectionType);
    UFUNCTION() void QualityChanged(FString Selected, ESelectInfo::Type SelectionType);
    UFUNCTION() void RestoreDefaults();
    UFUNCTION() void CloseOptions();
};

UCLASS()
class GRADINGRUNTIME_API UGradingOptionsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Grading|Options")
    static UGradingOptionsPanel* OpenOptions(UUserWidget* OriginMenu);
};
