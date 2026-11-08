#include "GradingDesktop.h"
#include "GradingOptionsSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"

namespace GradingDesktop
{
    const FLinearColor Ink(0.91f, 0.96f, 0.98f, 1.f);
    const FLinearColor Muted(0.56f, 0.77f, 0.79f, 1.f);
    const FLinearColor Warning(1.f, 0.4f, 0.35f, 1.f);
    const float Widths[] = {230.f, 200.f, 125.f, 125.f, 100.f, 80.f, 120.f};

    UTextBlock* Text(UWidgetTree* Tree, const FString& Value, int32 Size = 20, FLinearColor Color = Ink)
    {
        UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Value));
        FSlateFontInfo Font = Label->Font;
        Font.Size = Size;
        Label->SetFont(Font);
        Label->SetColorAndOpacity(FSlateColor(Color));
        return Label;
    }

    USizeBox* Cell(UWidgetTree* Tree, UHorizontalBox* Row, UWidget* Child, float Width)
    {
        USizeBox* Box = Tree->ConstructWidget<USizeBox>();
        Box->SetWidthOverride(Width);
        Box->SetMinDesiredHeight(42.f);
        Box->AddChild(Child);
        UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Box);
        Slot->SetPadding(FMargin(0.f, 3.f, 12.f, 3.f));
        Slot->SetVerticalAlignment(VAlign_Center);
        return Box;
    }

    UEditableTextBox* Input(UWidgetTree* Tree, const FString& Value, bool bEditable)
    {
        UEditableTextBox* Field = Tree->ConstructWidget<UEditableTextBox>();
        Field->SetText(FText::FromString(Value));
        Field->SetIsReadOnly(!bEditable);
        Field->WidgetStyle.Font.Size = 18;
        return Field;
    }

    UButton* Button(UWidgetTree* Tree, UHorizontalBox* Parent, const FString& Caption)
    {
        UButton* Result = Tree->ConstructWidget<UButton>();
        Result->SetBackgroundColor(FLinearColor(0.07f, 0.3f, 0.32f, 1.f));
        Result->AddChild(Text(Tree, Caption, 21));
        Cell(Tree, Parent, Result, Caption == TEXT("Confirm Delete") ? 240.f : (Caption == TEXT("Add Student") ? 200.f : 130.f));
        return Result;
    }

    bool Score(const FString& Raw, int32& Number)
    {
        const FString Value = Raw.TrimStartAndEnd();
        if (Value.IsEmpty() || Value.Len() > 3) return false;
        for (const TCHAR C : Value) if (C < TCHAR('0') || C > TCHAR('9')) return false;
        Number = FCString::Atoi(*Value);
        return Number >= 0 && Number <= 100;
    }
}

void UGradingStudentEditorRow::Configure(const FProfessorStudent& Student, bool bEditable, UGradingDesktopPanel* InPanel)
{
    Original = Student;
    bEditMode = bEditable;
    Panel = InPanel;
}

TSharedRef<SWidget> UGradingStudentEditorRow::RebuildWidget()
{
    using namespace GradingDesktop;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>();
        WidgetTree->RootWidget = Root;
        NameInput = Input(WidgetTree, Original.StudentName, bEditMode);
        // Keep the last saved RowName in Original while editing. Save resolves the
        // original record before normalizing its key from the edited student name.
        IDInput = Input(WidgetTree, Original.StudentID, bEditMode);
        OCAInput = Input(WidgetTree, FString::SanitizeFloat(Original.OCA), bEditMode);
        OEAInput = Input(WidgetTree, Original.bHasOEA ? FString::FromInt(Original.OEA) : TEXT(""), bEditMode);
        OEAInput->SetHintText(FText::FromString(TEXT("Not entered")));
        GradeLabel = Text(WidgetTree, TEXT(""));
        GPALabel = Text(WidgetTree, TEXT(""));
        UWidget* Fields[] = {NameInput, IDInput, OCAInput, OEAInput, GradeLabel, GPALabel};
        for (int32 I = 0; I < 6; ++I) ColumnBoxes.Add(Cell(WidgetTree, Root, Fields[I], Widths[I]));
        UButton* DeleteButton = WidgetTree->ConstructWidget<UButton>();
        DeleteButton->SetBackgroundColor(FLinearColor(0.35f, 0.07f, 0.06f, 1.f));
        DeleteButton->AddChild(Text(WidgetTree, TEXT("Delete"), 18));
        DeleteButton->SetVisibility(bEditMode ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        DeleteButton->OnClicked.AddDynamic(this, &UGradingStudentEditorRow::RequestDelete);
        ColumnBoxes.Add(Cell(WidgetTree, Root, DeleteButton, Widths[6]));
        SetColumnScale(ColumnScale);
        OCAInput->OnTextChanged.AddDynamic(this, &UGradingStudentEditorRow::ScoresChanged);
        OEAInput->OnTextChanged.AddDynamic(this, &UGradingStudentEditorRow::ScoresChanged);
        RefreshGrade();
    }
    return Super::RebuildWidget();
}

void UGradingStudentEditorRow::SetColumnScale(float Scale)
{
    ColumnScale = FMath::Clamp(Scale, .8f, 1.4f);
    for (int32 I = 0; I < ColumnBoxes.Num(); ++I)
    {
        ColumnBoxes[I]->SetWidthOverride(GradingDesktop::Widths[I] * ColumnScale);
        ColumnBoxes[I]->SetMinDesiredHeight(42.f * ColumnScale);
    }
}

bool UGradingStudentEditorRow::ReadOCA(float& Value) const
{
    if (!OCAInput) return false;
    const FString Text = OCAInput->GetText().ToString().TrimStartAndEnd();
    // Formatting must not alter an untouched stored float when saving a name/ID/OEA edit.
    if (Text == FString::SanitizeFloat(Original.OCA))
    {
        Value = Original.OCA;
        return FMath::IsFinite(Value) && Value >= 0.f && Value <= 100.f;
    }
    return UGradingStore::TryParseOCA(Text, Value);
}

bool UGradingStudentEditorRow::ReadStudent(FProfessorStudent& Student, FString& Error) const
{
    if (!NameInput || !IDInput || !OCAInput || !OEAInput)
    {
        Error = TEXT("The student row is not ready. Please reopen the grade page.");
        return false;
    }
    Student = Original;
    Student.StudentName = NameInput->GetText().ToString().TrimStartAndEnd();
    Student.StudentID = IDInput->GetText().ToString().TrimStartAndEnd();
    if (Student.StudentName.IsEmpty() || Student.StudentID.IsEmpty())
    {
        Error = TEXT("Enter a name and student ID for every row.");
        return false;
    }
    if (!ReadOCA(Student.OCA))
    {
        Error = FString::Printf(TEXT("%s: OCA must be a number from 0 to 100 (decimals allowed)."), *Student.StudentID);
        return false;
    }
    const FString OEA = OEAInput->GetText().ToString().TrimStartAndEnd();
    Student.bHasOEA = !OEA.IsEmpty();
    Student.OEA = 0;
    if (Student.bHasOEA && !GradingDesktop::Score(OEA, Student.OEA))
    {
        Error = FString::Printf(TEXT("%s: OEA must be blank or a whole number from 0 to 100."), *Student.StudentID);
        return false;
    }
    return true;
}

void UGradingStudentEditorRow::ScoresChanged(const FText& Text)
{
    RefreshGrade();
}

FString UGradingStudentEditorRow::GetDisplayIdentity() const
{
    FString Name = NameInput ? NameInput->GetText().ToString().TrimStartAndEnd() : Original.StudentName;
    FString ID = IDInput ? IDInput->GetText().ToString().TrimStartAndEnd() : Original.StudentID;
    if (Name.IsEmpty()) Name = TEXT("Unnamed student");
    if (ID.IsEmpty()) ID = TEXT("ID not entered");
    return FString::Printf(TEXT("%s (%s)"), *Name, *ID);
}

void UGradingStudentEditorRow::RequestDelete()
{
    if (bEditMode && Panel) Panel->RequestDelete(this);
}

void UGradingStudentEditorRow::RefreshGrade()
{
    float OCA = 0.f;
    int32 OEA = 0;
    const FString OEAText = OEAInput->GetText().ToString().TrimStartAndEnd();
    const bool bHasOEA = !OEAText.IsEmpty();
    if (!ReadOCA(OCA) || (bHasOEA && !GradingDesktop::Score(OEAText, OEA)))
    {
        GradeLabel->SetText(FText::FromString(TEXT("Invalid")));
        GradeLabel->SetColorAndOpacity(FSlateColor(GradingDesktop::Warning));
        GPALabel->SetText(FText::FromString(TEXT("—")));
        return;
    }
    const FProfessorGrade Result = UGradingStore::CalculateGrade(OCA, OEA, bHasOEA);
    GradeLabel->SetText(FText::FromString(Result.Grade));
    GradeLabel->SetColorAndOpacity(FSlateColor(Result.bGraded && Result.Grade == TEXT("F") ? GradingDesktop::Warning : GradingDesktop::Ink));
    GPALabel->SetText(FText::FromString(Result.bGraded ? FString::Printf(TEXT("%.1f"), Result.GPA) : TEXT("—")));
}

void UGradingDesktopPanel::Configure(UGradingStore* InStore)
{
    Store = InStore;
    if (Store) Draft = Store->GetRecords();
}

TSharedRef<SWidget> UGradingDesktopPanel::RebuildWidget()
{
    using namespace GradingDesktop;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
        WidgetTree->RootWidget = Root;
        Root->AddChildToVerticalBox(Text(WidgetTree, TEXT("Student Grades"), 28))->SetPadding(FMargin(0, 0, 0, 12));
        Root->AddChildToVerticalBox(Text(WidgetTree, TEXT("OCA 50% + OEA 50%  |  Each component ≥ 50, total ≥ 60 to pass"), 18, Muted))->SetPadding(FMargin(0, 0, 0, 12));
        UHorizontalBox* Toolbar = WidgetTree->ConstructWidget<UHorizontalBox>();
        Root->AddChild(Toolbar);
        EditButton = Button(WidgetTree, Toolbar, TEXT("Edit"));
        AddButton = Button(WidgetTree, Toolbar, TEXT("Add Student"));
        SaveButton = Button(WidgetTree, Toolbar, TEXT("Save"));
        CancelButton = Button(WidgetTree, Toolbar, TEXT("Cancel"));
        EditButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::BeginEdit);
        AddButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::AddStudent);
        SaveButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::Save);
        CancelButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::Cancel);
        DeleteConfirmation = WidgetTree->ConstructWidget<UBorder>();
        DeleteConfirmation->SetBrushColor(FLinearColor(0.16f, 0.035f, 0.025f, 1.f));
        DeleteConfirmation->SetPadding(FMargin(10));
        DeleteConfirmation->SetVisibility(ESlateVisibility::Collapsed);
        UVerticalBox* ConfirmationBody = WidgetTree->ConstructWidget<UVerticalBox>();
        DeleteConfirmation->AddChild(ConfirmationBody);
        DeleteMessage = Text(WidgetTree, TEXT(""), 18);
        DeleteMessage->SetAutoWrapText(true);
        ConfirmationBody->AddChild(DeleteMessage);
        UHorizontalBox* ConfirmationActions = WidgetTree->ConstructWidget<UHorizontalBox>();
        ConfirmationBody->AddChild(ConfirmationActions);
        UButton* ConfirmButton = Button(WidgetTree, ConfirmationActions, TEXT("Confirm Delete"));
        ConfirmButton->SetBackgroundColor(FLinearColor(0.35f, 0.07f, 0.06f, 1.f));
        ConfirmButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::ConfirmDelete);
        UButton* KeepButton = Button(WidgetTree, ConfirmationActions, TEXT("Keep"));
        KeepButton->OnClicked.AddDynamic(this, &UGradingDesktopPanel::KeepStudent);
        Root->AddChildToVerticalBox(DeleteConfirmation)->SetPadding(FMargin(0, 6, 0, 0));
        StatusLabel = Text(WidgetTree, TEXT(""), 17, Muted);
        StatusLabel->SetAutoWrapText(true);
        Root->AddChildToVerticalBox(StatusLabel)->SetPadding(FMargin(0, 7, 0, 7));
        // The header and rows share horizontal scrolling so they stay aligned at
        // larger text sizes. The inner scroll box still handles long student lists.
        UScrollBox* HorizontalScroll = WidgetTree->ConstructWidget<UScrollBox>();
        HorizontalScroll->SetOrientation(Orient_Horizontal);
        Root->AddChildToVerticalBox(HorizontalScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TableSize = WidgetTree->ConstructWidget<USizeBox>();
        HorizontalScroll->AddChild(TableSize);
        UVerticalBox* TableLayout = WidgetTree->ConstructWidget<UVerticalBox>();
        TableSize->AddChild(TableLayout);
        UBorder* HeaderBackground = WidgetTree->ConstructWidget<UBorder>();
        HeaderBackground->SetBrushColor(FLinearColor(0.04f, 0.3f, 0.27f, 1.f));
        HeaderBackground->SetPadding(FMargin(0));
        UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
        HeaderBackground->AddChild(Header);
        const TCHAR* Labels[] = {TEXT("Name"), TEXT("Student ID"), TEXT("OCA"), TEXT("OEA"), TEXT("Grade"), TEXT("GPA"), TEXT("")};
        for (int32 I = 0; I < 7; ++I) HeaderColumns.Add(Cell(WidgetTree, Header, Text(WidgetTree, Labels[I]), Widths[I]));
        TableLayout->AddChild(HeaderBackground);
        RowsScroll = WidgetTree->ConstructWidget<UScrollBox>();
        TableLayout->AddChildToVerticalBox(RowsScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        RowsBox = WidgetTree->ConstructWidget<UVerticalBox>();
        RowsScroll->AddChild(RowsBox);
    }
    return Super::RebuildWidget();
}

void UGradingDesktopPanel::NativeConstruct()
{
    Super::NativeConstruct();
    UserSettings = UGradingUserSettings::GetGradingSettings();
    if (UserSettings)
    {
        if (SettingsChangedHandle.IsValid()) UserSettings->OnOptionsChanged.Remove(SettingsChangedHandle);
        SettingsChangedHandle = UserSettings->OnOptionsChanged.AddUObject(this, &UGradingDesktopPanel::UpdateTableScale);
        UpdateTableScale(UserSettings->GetOptions());
    }
    else UpdateTableScale(FGradingOptions());
    // Construct may run again when a desktop is detached and reattached. Keep the
    // current controls (including incomplete text) until explicit Save or Cancel.
    if (!bRowsInitialized)
    {
        RebuildRows();
        ShowStatus(Store && !Store->GetLastError().IsEmpty() ? Store->GetLastError() : TEXT("Choose Edit to change grades or add students. Save applies edits to this session; F6 saves the game."), Store && !Store->GetLastError().IsEmpty());
    }
}

void UGradingDesktopPanel::NativeDestruct()
{
    if (UserSettings && SettingsChangedHandle.IsValid()) UserSettings->OnOptionsChanged.Remove(SettingsChangedHandle);
    SettingsChangedHandle.Reset();
    Super::NativeDestruct();
}

void UGradingDesktopPanel::UpdateTableScale(const FGradingOptions& Options)
{
    TableScale = FMath::Clamp(Options.TextScale, .8f, 1.4f);
    float Width = 20.f; // Leave room for the vertical scrollbar beside the row cells.
    for (int32 I = 0; I < HeaderColumns.Num(); ++I)
    {
        HeaderColumns[I]->SetWidthOverride(GradingDesktop::Widths[I] * TableScale);
        HeaderColumns[I]->SetMinDesiredHeight(42.f * TableScale);
        Width += GradingDesktop::Widths[I] * TableScale + 12.f;
    }
    if (TableSize) TableSize->SetWidthOverride(Width);
    // Layout-only updates keep focus, selection and incomplete draft text intact.
    for (UGradingStudentEditorRow* Row : RowWidgets) if (Row) Row->SetColumnScale(TableScale);
}

void UGradingDesktopPanel::RebuildRows()
{
    if (!RowsBox) return;
    bRowsInitialized = true;
    ClearDeleteConfirmation();
    RowsBox->ClearChildren();
    RowWidgets.Reset();
    for (const FProfessorStudent& Student : Draft)
    {
        UGradingStudentEditorRow* Row = CreateWidget<UGradingStudentEditorRow>(this);
        Row->Configure(Student, bEditMode, this);
        Row->SetColumnScale(TableScale);
        RowsBox->AddChild(Row);
        RowWidgets.Add(Row);
    }
    EditButton->SetIsEnabled(!bEditMode);
    AddButton->SetIsEnabled(bEditMode);
    SaveButton->SetIsEnabled(bEditMode);
    CancelButton->SetIsEnabled(bEditMode);
}

bool UGradingDesktopPanel::CollectDraft(TArray<FProfessorStudent>& Out, FString& Error) const
{
    Out.Reset();
    for (const UGradingStudentEditorRow* Row : RowWidgets)
    {
        FProfessorStudent Student;
        if (!Row || !Row->ReadStudent(Student, Error)) return false;
        Out.Add(Student);
    }
    return true;
}

void UGradingDesktopPanel::ShowStatus(const FString& Message, bool bError)
{
    if (!StatusLabel) return;
    StatusLabel->SetText(FText::FromString(Message));
    StatusLabel->SetColorAndOpacity(FSlateColor(bError ? GradingDesktop::Warning : GradingDesktop::Muted));
}

void UGradingDesktopPanel::BeginEdit()
{
    if (!Store || !Store->IsLoggedIn()) return;
    Draft = Store->GetRecords();
    bEditMode = true;
    RebuildRows();
    ShowStatus(TEXT("Editing — changes are not saved until you press Save."));
}

void UGradingDesktopPanel::AddStudent()
{
    if (!bEditMode || PendingDelete) return;
    TArray<FProfessorStudent> Current;
    FString Error;
    if (!CollectDraft(Current, Error)) { ShowStatus(Error, true); return; }
    FProfessorStudent NewStudent;
    NewStudent.OCA = 0;
    NewStudent.bHasOEA = false;
    NewStudent.bHasExpectedScore = false;
    Current.Add(NewStudent);
    Draft = MoveTemp(Current);
    RebuildRows();
    if (RowsScroll) RowsScroll->ScrollToEnd();
    ShowStatus(TEXT("Enter the new student's name, unique ID and marks, then press Save."));
}

void UGradingDesktopPanel::Save()
{
    if (!bEditMode || !Store || PendingDelete) return;
    TArray<FProfessorStudent> Current;
    FString Error;
    if (!CollectDraft(Current, Error) || !Store->SaveRecords(Current, Error))
    {
        ShowStatus(Error, true);
        return;
    }
    Draft = Store->GetRecords();
    bEditMode = false;
    RebuildRows();
    ShowStatus(TEXT("Applied to this session. Press F6 outside the computer to save the game."));
}

void UGradingDesktopPanel::Cancel()
{
    if (!Store) return;
    Draft = Store->GetRecords();
    bEditMode = false;
    RebuildRows();
    ShowStatus(TEXT("Unsaved edits discarded."));
}

void UGradingDesktopPanel::RequestDelete(UGradingStudentEditorRow* Row)
{
    if (!bEditMode || !Row || !RowWidgets.Contains(Row)) return;
    ClearDeleteConfirmation();
    PendingDelete = Row;
    PendingDelete->SetIsEnabled(false);
    DeleteMessage->SetText(FText::FromString(FString::Printf(
        TEXT("Delete %s from this draft? Press Save afterwards to keep the deletion."), *Row->GetDisplayIdentity())));
    DeleteConfirmation->SetVisibility(ESlateVisibility::Visible);
    SaveButton->SetIsEnabled(false);
    AddButton->SetIsEnabled(false);
}

void UGradingDesktopPanel::ClearDeleteConfirmation()
{
    if (PendingDelete) PendingDelete->SetIsEnabled(true);
    PendingDelete = nullptr;
    if (DeleteConfirmation) DeleteConfirmation->SetVisibility(ESlateVisibility::Collapsed);
    if (SaveButton) SaveButton->SetIsEnabled(bEditMode);
    if (AddButton) AddButton->SetIsEnabled(bEditMode);
}

void UGradingDesktopPanel::ConfirmDelete()
{
    if (!bEditMode || !PendingDelete || !RowWidgets.Contains(PendingDelete))
    {
        ClearDeleteConfirmation();
        return;
    }
    // Remove only the selected widget. Reading/rebuilding every row here would
    // discard or reject incomplete edits in unrelated rows (or a blank new row).
    const FString Identity = PendingDelete->GetDisplayIdentity();
    RowsBox->RemoveChild(PendingDelete);
    RowWidgets.Remove(PendingDelete);
    ClearDeleteConfirmation();
    ShowStatus(FString::Printf(TEXT("%s removed from the draft. Save to keep this change, or Cancel to restore."), *Identity));
}

void UGradingDesktopPanel::KeepStudent()
{
    ClearDeleteConfirmation();
    ShowStatus(TEXT("Student kept. Your current edits are unchanged."));
}

void UGradingLoginGate::Configure(UGradingStore* InStore, UWidgetSwitcher* InApps, UButton* InHome, UUserWidget* InDesktop)
{
    Store = InStore;
    Apps = InApps;
    Home = InHome;
    Desktop = InDesktop;
}

TSharedRef<SWidget> UGradingLoginGate::RebuildWidget()
{
    using namespace GradingDesktop;
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
    if (!WidgetTree->RootWidget)
    {
        UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
        Background->SetBrushColor(FLinearColor(0.008f, 0.022f, 0.035f, 1.f));
        Background->SetPadding(FMargin(275.f, 50.f));
        WidgetTree->RootWidget = Background;
        UVerticalBox* Form = WidgetTree->ConstructWidget<UVerticalBox>();
        Background->AddChild(Form);
        Form->AddChildToVerticalBox(Text(WidgetTree, TEXT("Professor Login"), 30))->SetPadding(FMargin(0, 0, 0, 25));
        Form->AddChildToVerticalBox(Text(WidgetTree, TEXT("Account"), 20))->SetPadding(FMargin(0, 0, 0, 6));
        AccountInput = Input(WidgetTree, TEXT(""), true);
        AccountInput->SetHintText(FText::FromString(TEXT("Account name")));
        Form->AddChildToVerticalBox(AccountInput)->SetPadding(FMargin(0, 0, 0, 18));
        Form->AddChildToVerticalBox(Text(WidgetTree, TEXT("Password"), 20))->SetPadding(FMargin(0, 0, 0, 6));
        PasswordInput = Input(WidgetTree, TEXT(""), true);
        PasswordInput->SetIsPassword(true);
        PasswordInput->SetHintText(FText::FromString(TEXT("Password")));
        Form->AddChildToVerticalBox(PasswordInput)->SetPadding(FMargin(0, 0, 0, 18));
        PasswordInput->OnTextCommitted.AddDynamic(this, &UGradingLoginGate::PasswordCommitted);
        UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
        Form->AddChild(Actions);
        UButton* Submit = Button(WidgetTree, Actions, TEXT("Login"));
        Submit->OnClicked.AddDynamic(this, &UGradingLoginGate::SubmitLogin);
        ErrorLabel = Text(WidgetTree, TEXT(""), 18, Warning);
        ErrorLabel->SetAutoWrapText(true);
        Form->AddChildToVerticalBox(ErrorLabel)->SetPadding(FMargin(0, 14, 0, 0));
    }
    return Super::RebuildWidget();
}

void UGradingLoginGate::NativeConstruct()
{
    Super::NativeConstruct();
    if (AccountInput && GetVisibility() != ESlateVisibility::Collapsed && (!Store || !Store->IsLoggedIn()))
        AccountInput->SetKeyboardFocus();
}

void UGradingLoginGate::SubmitLogin()
{
    if (!Store || !Store->Login(AccountInput->GetText().ToString().TrimStartAndEnd(), PasswordInput->GetText().ToString()))
    {
        ErrorLabel->SetText(FText::FromString(TEXT("Incorrect account or password.")));
        PasswordInput->SetText(FText::GetEmpty());
        PasswordInput->SetKeyboardFocus();
        return;
    }
    PasswordInput->SetText(FText::GetEmpty());
    if (Apps) { Apps->SetVisibility(ESlateVisibility::Visible); Apps->SetIsEnabled(true); Apps->SetActiveWidgetIndex(0); }
    if (Home) Home->SetIsEnabled(true);
    SetVisibility(ESlateVisibility::Collapsed);
    if (Desktop) Desktop->SetKeyboardFocus();
}

void UGradingLoginGate::PasswordCommitted(const FText& Text, ETextCommit::Type Method)
{
    if (Method == ETextCommit::OnEnter) SubmitLogin();
}

void UGradingDesktopLibrary::InstallDesktop(UUserWidget* Desktop)
{
    if (!Desktop || !Desktop->GetGameInstance()) return;
    UGradingStore* Store = Desktop->GetGameInstance()->GetSubsystem<UGradingStore>();
    UWidgetSwitcher* Apps = Cast<UWidgetSwitcher>(Desktop->GetWidgetFromName(TEXT("Apps")));
    UCanvasPanel* Canvas = Cast<UCanvasPanel>(Desktop->GetWidgetFromName(TEXT("DesktopCanvas")));
    UButton* Home = Cast<UButton>(Desktop->GetWidgetFromName(TEXT("HomeButton")));
    if (!Store || !Apps || !Canvas || Apps->GetChildrenCount() < 3) return;
    if (Cast<UGradingDesktopPanel>(Apps->GetChildAt(2))) return;
    Store->InitializeRecords(LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students")));
    const int32 ActiveIndex = Apps->GetActiveWidgetIndex();
    UGradingDesktopPanel* Grades = CreateWidget<UGradingDesktopPanel>(Desktop);
    Grades->Configure(Store);
    Apps->RemoveChildAt(2);
    Apps->AddChild(Grades);
    Apps->SetActiveWidgetIndex(FMath::Clamp(ActiveIndex, 0, 2));
    if (!Store->IsLoggedIn())
    {
        Apps->SetIsEnabled(false);
        Apps->SetVisibility(ESlateVisibility::Hidden);
        if (Home) Home->SetIsEnabled(false);
        UGradingLoginGate* Gate = CreateWidget<UGradingLoginGate>(Desktop);
        Gate->Configure(Store, Apps, Home, Desktop);
        UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Gate);
        Slot->SetPosition(FVector2D(40, 140));
        Slot->SetSize(FVector2D(1120, 545));
        Slot->SetZOrder(50);
    }
}
