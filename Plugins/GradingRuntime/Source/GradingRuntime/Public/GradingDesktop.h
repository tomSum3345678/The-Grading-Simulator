#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GradingStore.h"
#include "GradingDesktop.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
class UScrollBox;
class UBorder;
class USizeBox;
class UGradingUserSettings;
struct FGradingOptions;
class UGradingDesktopPanel;

/** A single draft row. Changes remain local until the explicit Save action. */
UCLASS()
class GRADINGRUNTIME_API UGradingStudentEditorRow : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(const FProfessorStudent& Student, bool bEditable, UGradingDesktopPanel* InPanel);
    bool ReadStudent(FProfessorStudent& Student, FString& Error) const;
    FString GetDisplayIdentity() const;
    void SetColumnScale(float Scale);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    FProfessorStudent Original;
    bool bEditMode = false;
    float ColumnScale = 1.f;
    UPROPERTY() TArray<USizeBox*> ColumnBoxes;
    UPROPERTY() UGradingDesktopPanel* Panel = nullptr;
    UPROPERTY() UEditableTextBox* NameInput = nullptr;
    UPROPERTY() UEditableTextBox* IDInput = nullptr;
    UPROPERTY() UEditableTextBox* OCAInput = nullptr;
    UPROPERTY() UEditableTextBox* OEAInput = nullptr;
    UPROPERTY() UTextBlock* GradeLabel = nullptr;
    UPROPERTY() UTextBlock* GPALabel = nullptr;
    UFUNCTION() void ScoresChanged(const FText& Text);
    UFUNCTION() void RequestDelete();
    bool ReadOCA(float& Value) const;
    void RefreshGrade();
};

/** Durable grade editing surface installed into the existing workstation. */
UCLASS()
class GRADINGRUNTIME_API UGradingDesktopPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(UGradingStore* InStore);
    void RequestDelete(UGradingStudentEditorRow* Row);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY() UGradingStore* Store = nullptr;
    UPROPERTY() UVerticalBox* RowsBox = nullptr;
    UPROPERTY() UScrollBox* RowsScroll = nullptr;
    UPROPERTY() USizeBox* TableSize = nullptr;
    UPROPERTY() TArray<USizeBox*> HeaderColumns;
    UPROPERTY() UGradingUserSettings* UserSettings = nullptr;
    UPROPERTY() UTextBlock* StatusLabel = nullptr;
    UPROPERTY() UButton* EditButton = nullptr;
    UPROPERTY() UButton* AddButton = nullptr;
    UPROPERTY() UButton* SaveButton = nullptr;
    UPROPERTY() UButton* CancelButton = nullptr;
    UPROPERTY() UBorder* DeleteConfirmation = nullptr;
    UPROPERTY() UTextBlock* DeleteMessage = nullptr;
    UPROPERTY() UGradingStudentEditorRow* PendingDelete = nullptr;
    UPROPERTY() TArray<UGradingStudentEditorRow*> RowWidgets;
    TArray<FProfessorStudent> Draft;
    bool bEditMode = false;
    bool bRowsInitialized = false;
    float TableScale = 1.f;
    FDelegateHandle SettingsChangedHandle;
    void UpdateTableScale(const FGradingOptions& Options);
    void RebuildRows();
    bool CollectDraft(TArray<FProfessorStudent>& Out, FString& Error) const;
    void ShowStatus(const FString& Message, bool bError = false);
    void ClearDeleteConfirmation();
    UFUNCTION() void BeginEdit();
    UFUNCTION() void AddStudent();
    UFUNCTION() void Save();
    UFUNCTION() void Cancel();
    UFUNCTION() void ConfirmDelete();
    UFUNCTION() void KeepStudent();
};

/** Login blocks every workstation application while leaving the existing X available. */
UCLASS()
class GRADINGRUNTIME_API UGradingLoginGate : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(UGradingStore* InStore, UWidgetSwitcher* InApps, UButton* InHome, UUserWidget* InDesktop);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
private:
    UPROPERTY() UGradingStore* Store = nullptr;
    UPROPERTY() UWidgetSwitcher* Apps = nullptr;
    UPROPERTY() UButton* Home = nullptr;
    UPROPERTY() UUserWidget* Desktop = nullptr;
    UPROPERTY() UEditableTextBox* AccountInput = nullptr;
    UPROPERTY() UEditableTextBox* PasswordInput = nullptr;
    UPROPERTY() UTextBlock* ErrorLabel = nullptr;
    UFUNCTION() void SubmitLogin();
    UFUNCTION() void PasswordCommitted(const FText& Text, ETextCommit::Type Method);
};

UCLASS()
class GRADINGRUNTIME_API UGradingDesktopLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Grading|Desktop")
    static void InstallDesktop(UUserWidget* Desktop);
};
