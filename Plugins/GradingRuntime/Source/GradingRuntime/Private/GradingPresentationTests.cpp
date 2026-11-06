#include "GradingPresentation.h"
#include "GradingOptionsWidget.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingPresentationTest, "GradingRuntime.Options.TextAndPaperPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingPresentationTest::RunTest(const FString&)
{
    UUserWidget* Screen = NewObject<UGradingOptionsPanel>();
    Screen->WidgetTree = NewObject<UWidgetTree>(Screen);
    UCanvasPanel* Canvas = Screen->WidgetTree->ConstructWidget<UCanvasPanel>();
    Screen->WidgetTree->RootWidget = Canvas;
    UTextBlock* Label = Screen->WidgetTree->ConstructWidget<UTextBlock>();
    FSlateFontInfo Font = Label->Font;
    Font.Size = 20;
    Label->SetFont(Font);
    Canvas->AddChild(Label);
    UEditableTextBox* Draft = Screen->WidgetTree->ConstructWidget<UEditableTextBox>();
    Draft->WidgetStyle.Font.Size = 20;
    Draft->SetText(FText::FromString(TEXT("unfinished score 62x")));
    Canvas->AddChild(Draft);
    UImage* Image = Screen->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Img_PaperDisplay"));
    UCanvasPanelSlot* ImageSlot = Canvas->AddChildToCanvas(Image);
    ImageSlot->SetPosition(FVector2D(40.f, 20.f));
    ImageSlot->SetSize(FVector2D(600.f, 800.f));
    UButton* Next = Screen->WidgetTree->ConstructWidget<UButton>();
    UCanvasPanelSlot* NextSlot = Canvas->AddChildToCanvas(Next);
    NextSlot->SetPosition(FVector2D(540.f, 740.f));
    NextSlot->SetSize(FVector2D(80.f, 40.f));

    UGradingOptionsSettingsLibrary::ApplyWidgetValues(Screen, 1.4f, 1.75f);
    UGradingPaperViewport* Viewport = Cast<UGradingPaperViewport>(Image->GetParent());
    TestEqual(TEXT("Text follows selected size"), Label->Font.Size, 28);
    TestEqual(TEXT("Input text follows selected size"), Draft->WidgetStyle.Font.Size, 28);
    TestEqual(TEXT("Incomplete draft input is untouched"), Draft->GetText().ToString(), FString(TEXT("unfinished score 62x")));
    TestNotNull(TEXT("Paper gets a zoom viewport"), Viewport);
    if (!Viewport) return false;
    TestTrue(TEXT("Original image object survives wrapping"), Viewport->GetContent() == Image);
    TestTrue(TEXT("Viewport keeps original canvas position"), CastChecked<UCanvasPanelSlot>(Viewport->Slot)->GetPosition() == FVector2D(40.f, 20.f));
    TestTrue(TEXT("Viewport keeps original canvas size"), CastChecked<UCanvasPanelSlot>(Viewport->Slot)->GetSize() == FVector2D(600.f, 800.f));
    TestTrue(TEXT("Page controls remain above paper"), NextSlot->GetZOrder() > CastChecked<UCanvasPanelSlot>(Viewport->Slot)->GetZOrder());
    TestTrue(TEXT("Page controls do not scale or move"), NextSlot->GetPosition() == FVector2D(540.f, 740.f) && NextSlot->GetSize() == FVector2D(80.f, 40.f));
    TestEqual(TEXT("Paper zoom applies independently"), Viewport->GetZoom(), 1.75f);

    UGradingOptionsSettingsLibrary::ApplyWidgetValues(Screen, 1.4f, 1.75f);
    TestEqual(TEXT("Repeating updates does not multiply font size"), Label->Font.Size, 28);
    TestTrue(TEXT("Repeating updates does not wrap paper again"), Image->GetParent() == Viewport);
    UGradingOptionsSettingsLibrary::ApplyWidgetValues(Screen, 1.f, 1.f);
    TestEqual(TEXT("Returning to default restores base font"), Label->Font.Size, 20);
    TestEqual(TEXT("Returning to default restores paper zoom"), Viewport->GetZoom(), 1.f);
    TestEqual(TEXT("Returning to default preserves draft"), Draft->GetText().ToString(), FString(TEXT("unfinished score 62x")));
    return true;
}
#endif
