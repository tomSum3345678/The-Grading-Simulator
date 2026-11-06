#include "GradingOptionsWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"

namespace GradingOptionsUI
{
    const FLinearColor Ink(0.98f, 0.94f, 0.84f, 1.f);
    const FLinearColor Muted(0.68f, 0.78f, 0.82f, 1.f);
    const FLinearColor Accent(1.f, 0.035f, 0.46f, 1.f);

    FSlateChildSize Fill(float Weight)
    {
        FSlateChildSize Size(ESlateSizeRule::Fill);
        Size.Value = Weight;
        return Size;
    }

    UTextBlock* Text(UWidgetTree* Tree, const FString& Value, int32 FontSize, FLinearColor Color = Ink)
    {
        UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Value));
        FSlateFontInfo Font = Label->Font;
        Font.Size = FontSize;
        Label->SetFont(Font);
        Label->SetColorAndOpacity(FSlateColor(Color));
        return Label;
    }

    UHorizontalBox* Row(UWidgetTree* Tree, UVerticalBox* Body, const FString& Caption)
    {
        UHorizontalBox* Result = Tree->ConstructWidget<UHorizontalBox>();
        UVerticalBoxSlot* BodySlot = Body->AddChildToVerticalBox(Result);
        BodySlot->SetPadding(FMargin(0.f, 10.f, 0.f, 10.f));
        UTextBlock* Label = Text(Tree, Caption, 23);
        Label->SetAutoWrapText(true);
        UHorizontalBoxSlot* LabelSlot = Result->AddChildToHorizontalBox(Label);
        LabelSlot->SetSize(Fill(0.4f));
        LabelSlot->SetVerticalAlignment(VAlign_Center);
        LabelSlot->SetPadding(FMargin(0, 0, 24, 0));
        return Result;
    }

    USlider* Slider(UWidgetTree* Tree, UVerticalBox* Body, const FString& Caption, float Min, float Max, float Step, UTextBlock*& ValueLabel)
    {
        UHorizontalBox* Line = Row(Tree, Body, Caption);
        USlider* Control = Tree->ConstructWidget<USlider>();
        Control->SetMinValue(Min);
        Control->SetMaxValue(Max);
        Control->SetStepSize(Step);
        Control->SetSliderHandleColor(Accent);
        Control->SetSliderBarColor(FLinearColor(.65f, .72f, .77f, 1.f));
        UHorizontalBoxSlot* ControlSlot = Line->AddChildToHorizontalBox(Control);
        ControlSlot->SetSize(Fill(0.5f));
        ControlSlot->SetVerticalAlignment(VAlign_Center);
        ValueLabel = Text(Tree, TEXT(""), 22, Muted);
        ValueLabel->SetJustification(ETextJustify::Right);
        USizeBox* ValueBox = Tree->ConstructWidget<USizeBox>();
        ValueBox->SetWidthOverride(110);
        ValueBox->SetMinDesiredHeight(44);
        ValueBox->AddChild(ValueLabel);
        UHorizontalBoxSlot* ValueSlot = Line->AddChildToHorizontalBox(ValueBox);
        ValueSlot->SetPadding(FMargin(18, 0, 0, 0));
        ValueSlot->SetVerticalAlignment(VAlign_Center);
        return Control;
    }

    UComboBoxString* Combo(UWidgetTree* Tree, UVerticalBox* Body, const FString& Caption, const TArray<FString>& Choices)
    {
        UHorizontalBox* Line = Row(Tree, Body, Caption);
        UComboBoxString* Control = Tree->ConstructWidget<UComboBoxString>();
        Control->Font.Size = 22;
        for (const FString& Choice : Choices) Control->AddOption(Choice);
        UHorizontalBoxSlot* Slot = Line->AddChildToHorizontalBox(Control);
        Slot->SetSize(Fill(0.6f));
        Slot->SetVerticalAlignment(VAlign_Center);
        return Control;
    }

    UButton* Button(UWidgetTree* Tree, UHorizontalBox* Line, const FString& Caption)
    {
        UButton* Control = Tree->ConstructWidget<UButton>();
        Control->AddChild(Text(Tree, Caption, 27, Accent));
        USizeBox* Box = Tree->ConstructWidget<USizeBox>();
        Box->SetWidthOverride(210);
        Box->SetHeightOverride(60);
        Box->AddChild(Control);
        Line->AddChildToHorizontalBox(Box)->SetPadding(FMargin(0, 0, 18, 0));
        return Control;
    }

    void FullScreen(UCanvasPanel* Root, UWidget* Child, int32 ZOrder)
    {
        UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Child);
        Slot->SetAnchors(FAnchors(0, 0, 1, 1));
        Slot->SetOffsets(FMargin(0));
        Slot->SetZOrder(ZOrder);
    }

    FText Percent(float Value)
    {
        return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.f)));
    }
}

UGradingOptionsPanel::UGradingOptionsPanel(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bIsFocusable = true;
}

void UGradingOptionsPanel::Configure(UUserWidget* InOriginMenu)
{
    OriginMenu = InOriginMenu;
    Settings = UGradingUserSettings::GetGradingSettings();
    bOriginWasEnabled = OriginMenu ? OriginMenu->GetIsEnabled() : true;
    bOriginRestored = false;
    if (OriginMenu) OriginMenu->SetIsEnabled(false);
}

TSharedRef<SWidget> UGradingOptionsPanel::RebuildWidget()
{
    using namespace GradingOptionsUI;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
        WidgetTree->RootWidget = Root;
        UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
        Blur->SetBlurStrength(12.f);
        Blur->SetApplyAlphaToBlur(false);
        FullScreen(Root, Blur, 0);
        UBorder* Shade = WidgetTree->ConstructWidget<UBorder>();
        Shade->SetBrushColor(FLinearColor(.015f, .022f, .035f, .55f));
        FullScreen(Root, Shade, 1);
        UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
        Frame->SetBrushColor(FLinearColor(.02f, .035f, .055f, .83f));
        Frame->SetPadding(FMargin(42, 28));
        UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
        FrameSlot->SetAnchors(FAnchors(.16f, .055f, .84f, .945f));
        FrameSlot->SetOffsets(FMargin(0));
        FrameSlot->SetZOrder(2);
        UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>();
        Frame->AddChild(Layout);
        Layout->AddChildToVerticalBox(Text(WidgetTree, TEXT("OPTIONS"), 38))->SetPadding(FMargin(0, 0, 0, 5));
        StatusLabel = Text(WidgetTree, TEXT("Changes are saved automatically."), 18, Muted);
        StatusLabel->SetAutoWrapText(true);
        Layout->AddChildToVerticalBox(StatusLabel)->SetPadding(FMargin(0, 0, 0, 14));
        UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
        Layout->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
        Scroll->AddChild(Body);
        MasterSlider = Slider(WidgetTree, Body, TEXT("Master volume"), 0, 1, .01f, MasterValue);
        SFXSlider = Slider(WidgetTree, Body, TEXT("Sound effects"), 0, 1, .01f, SFXValue);
        MusicSlider = Slider(WidgetTree, Body, TEXT("Background music"), 0, 1, .01f, MusicValue);
        UHorizontalBox* MusicLine = Row(WidgetTree, Body, TEXT("Enable background music"));
        MusicEnabledBox = WidgetTree->ConstructWidget<UCheckBox>();
        MusicEnabledBox->AddChild(Text(WidgetTree, TEXT("On"), 22, Muted));
        MusicLine->AddChildToHorizontalBox(MusicEnabledBox)->SetSize(Fill(.6f));
        SensitivitySlider = Slider(WidgetTree, Body, TEXT("Mouse sensitivity"), .2f, 3.f, .05f, SensitivityValue);
        WindowModeBox = Combo(WidgetTree, Body, TEXT("Window mode"), {TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed")});
        TextSlider = Slider(WidgetTree, Body, TEXT("Text size"), .8f, 1.4f, .05f, TextValue);
        ZoomSlider = Slider(WidgetTree, Body, TEXT("Exam zoom"), .75f, 2.f, .05f, ZoomValue);
        QualityBox = Combo(WidgetTree, Body, TEXT("Graphics quality"), {TEXT("Low"), TEXT("Medium"), TEXT("High")});
        UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
        Layout->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0, 20, 0, 0));
        UButton* Defaults = Button(WidgetTree, Actions, TEXT("Defaults"));
        UButton* Back = Button(WidgetTree, Actions, TEXT("Back"));
        Defaults->OnClicked.AddDynamic(this, &UGradingOptionsPanel::RestoreDefaults);
        Back->OnClicked.AddDynamic(this, &UGradingOptionsPanel::CloseOptions);
        MasterSlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::MasterChanged);
        SFXSlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::SFXChanged);
        MusicSlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::MusicChanged);
        SensitivitySlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::SensitivityChanged);
        TextSlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::TextChanged);
        ZoomSlider->OnValueChanged.AddDynamic(this, &UGradingOptionsPanel::ZoomChanged);
        MusicEnabledBox->OnCheckStateChanged.AddDynamic(this, &UGradingOptionsPanel::MusicEnabledChanged);
        WindowModeBox->OnSelectionChanged.AddDynamic(this, &UGradingOptionsPanel::WindowModeChanged);
        QualityBox->OnSelectionChanged.AddDynamic(this, &UGradingOptionsPanel::QualityChanged);
    }
    return Super::RebuildWidget();
}

void UGradingOptionsPanel::NativeConstruct()
{
    Super::NativeConstruct();
    if (!Settings) Settings = UGradingUserSettings::GetGradingSettings();
    if (Settings)
    {
        if (SettingsChangedHandle.IsValid()) Settings->OnOptionsChanged.Remove(SettingsChangedHandle);
        SettingsChangedHandle = Settings->OnOptionsChanged.AddUObject(this, &UGradingOptionsPanel::RefreshSettings);
        RefreshSettings(Settings->GetOptions());
    }
    else StatusLabel->SetText(FText::FromString(TEXT("Settings could not be loaded. Please restart the game.")));
    SetKeyboardFocus();
}

void UGradingOptionsPanel::NativeDestruct()
{
    if (Settings && SettingsChangedHandle.IsValid()) Settings->OnOptionsChanged.Remove(SettingsChangedHandle);
    SettingsChangedHandle.Reset();
    RestoreOrigin();
    Super::NativeDestruct();
}

FReply UGradingOptionsPanel::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey().GetFName() == FName(TEXT("Escape")))
    {
        if (!InKeyEvent.IsRepeat()) CloseOptions();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UGradingOptionsPanel::RefreshSettings(const FGradingOptions& Options)
{
    if (!MasterSlider) return;
    TGuardValue<bool> RefreshGuard(bRefreshing, true);
    MasterSlider->SetValue(Options.MasterVolume);
    SFXSlider->SetValue(Options.SFXVolume);
    MusicSlider->SetValue(Options.MusicVolume);
    MusicEnabledBox->SetIsChecked(Options.bMusicEnabled);
    MusicSlider->SetIsEnabled(Options.bMusicEnabled);
    SensitivitySlider->SetValue(Options.MouseSensitivity);
    TextSlider->SetValue(Options.TextScale);
    ZoomSlider->SetValue(Options.ExamZoom);
    // ComboBoxString generates Slate text directly, outside the global UTextBlock
    // font scaler. Regenerate its selected label only when the font size changes.
    const int32 ChoiceFontSize = FMath::RoundToInt(22.f * Options.TextScale);
    for (UComboBoxString* Choice : {WindowModeBox, QualityBox})
    {
        if (Choice->Font.Size != ChoiceFontSize)
        {
            Choice->Font.Size = ChoiceFontSize;
            Choice->ClearSelection();
            Choice->RefreshOptions();
        }
    }
    const TCHAR* Modes[] = {TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed")};
    const TCHAR* Qualities[] = {TEXT("Low"), TEXT("Medium"), TEXT("High")};
    WindowModeBox->SetSelectedOption(Modes[FMath::Clamp(static_cast<int32>(Options.WindowMode), 0, 2)]);
    QualityBox->SetSelectedOption(Qualities[FMath::Clamp(Options.QualityPreset, 0, 2)]);
    MasterValue->SetText(GradingOptionsUI::Percent(Options.MasterVolume));
    SFXValue->SetText(GradingOptionsUI::Percent(Options.SFXVolume));
    MusicValue->SetText(GradingOptionsUI::Percent(Options.MusicVolume));
    SensitivityValue->SetText(FText::FromString(FString::Printf(TEXT("%.2f×"), Options.MouseSensitivity)));
    TextValue->SetText(GradingOptionsUI::Percent(Options.TextScale));
    ZoomValue->SetText(GradingOptionsUI::Percent(Options.ExamZoom));
}

bool UGradingOptionsPanel::CanEdit() const { return Settings && !bRefreshing; }

void UGradingOptionsPanel::MasterChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.MasterVolume = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::SFXChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.SFXVolume = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::MusicChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.MusicVolume = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::SensitivityChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.MouseSensitivity = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::TextChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.TextScale = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::ZoomChanged(float Value)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.ExamZoom = Value; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::MusicEnabledChanged(bool bEnabled)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions(); Options.bMusicEnabled = bEnabled; Settings->SetOptions(Options);
}
void UGradingOptionsPanel::WindowModeChanged(FString Selected, ESelectInfo::Type SelectionType)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions();
    if (Selected == TEXT("Fullscreen")) Options.WindowMode = EGradingWindowMode::Fullscreen;
    else if (Selected == TEXT("Borderless")) Options.WindowMode = EGradingWindowMode::Borderless;
    else if (Selected == TEXT("Windowed")) Options.WindowMode = EGradingWindowMode::Windowed;
    else return;
    Settings->SetOptions(Options);
}
void UGradingOptionsPanel::QualityChanged(FString Selected, ESelectInfo::Type SelectionType)
{
    if (!CanEdit()) return;
    FGradingOptions Options = Settings->GetOptions();
    if (Selected == TEXT("Low")) Options.QualityPreset = 0;
    else if (Selected == TEXT("Medium")) Options.QualityPreset = 1;
    else if (Selected == TEXT("High")) Options.QualityPreset = 2;
    else return;
    Settings->SetOptions(Options);
}
void UGradingOptionsPanel::RestoreDefaults()
{
    if (CanEdit()) Settings->ResetOptions();
}

void UGradingOptionsPanel::RestoreOrigin()
{
    if (bOriginRestored) return;
    bOriginRestored = true;
    if (!IsValid(OriginMenu)) return;
    OriginMenu->SetIsEnabled(bOriginWasEnabled);
    if (!OriginMenu->IsInViewport()) return;
    if (APlayerController* Player = OriginMenu->GetOwningPlayer())
    {
        FInputModeUIOnly InputMode;
        InputMode.SetWidgetToFocus(OriginMenu->TakeWidget());
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Player->SetInputMode(InputMode);
        Player->bShowMouseCursor = true;
    }
    OriginMenu->SetKeyboardFocus();
}

void UGradingOptionsPanel::CloseOptions()
{
    RemoveFromParent();
    RestoreOrigin();
}

UGradingOptionsPanel* UGradingOptionsLibrary::OpenOptions(UUserWidget* OriginMenu)
{
    if (!IsValid(OriginMenu) || !OriginMenu->GetWorld()) return nullptr;
    TArray<UUserWidget*> Existing;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(OriginMenu, Existing, UGradingOptionsPanel::StaticClass(), true);
    for (UUserWidget* Widget : Existing)
    {
        if (UGradingOptionsPanel* Options = Cast<UGradingOptionsPanel>(Widget))
        {
            Options->SetKeyboardFocus();
            return Options;
        }
    }
    UGradingOptionsPanel* Options = CreateWidget<UGradingOptionsPanel>(OriginMenu);
    if (!Options) return nullptr;
    Options->Configure(OriginMenu);
    Options->AddToViewport(10010);
    if (APlayerController* Player = OriginMenu->GetOwningPlayer())
    {
        FInputModeUIOnly InputMode;
        InputMode.SetWidgetToFocus(Options->TakeWidget());
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Player->SetInputMode(InputMode);
        Player->bShowMouseCursor = true;
    }
    Options->SetKeyboardFocus();
    return Options;
}
