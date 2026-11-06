#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Engine/DataTable.h"
#include "GradingStore.generated.h"

class UDataTable;

/** Fictional game accounts authored in /Game/DataTable/DT_ProfessorAccounts. */
USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FProfessorAccountRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Account;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Password;
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FProfessorStudent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString StudentName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString StudentID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FName RowName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString ReferenceNumber;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OCA = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OEA = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bHasOEA = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ExpectedScore = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bHasExpectedScore = false;
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FProfessorGrade
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) float Total = 0.f;
    UPROPERTY(BlueprintReadOnly) FString Grade = TEXT("—");
    UPROPERTY(BlueprintReadOnly) float GPA = 0.f;
    UPROPERTY(BlueprintReadOnly) bool bGraded = false;
};

UCLASS()
class GRADINGRUNTIME_API UProfessorGradeSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 FormatVersion = 1;
    UPROPERTY(SaveGame) TArray<FProfessorStudent> Students;
    // Kept in format 1: older saves deserialize this newly added field as an empty array.
    UPROPERTY(SaveGame) TArray<FName> DeletedRowNames;
    UPROPERTY(SaveGame) TArray<FString> DeletedReferenceNumbers;
    UPROPERTY(SaveGame) int32 NextReferenceNumber = 1;
};

/** Local fictional workstation account and durable professor grade records. */
UCLASS()
class GRADINGRUNTIME_API UGradingStore : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable) bool Login(const FString& Account, const FString& Password);
    UFUNCTION(BlueprintCallable) void Logout();
    UFUNCTION(BlueprintPure) bool IsLoggedIn() const { return bLoggedIn; }

    UFUNCTION(BlueprintCallable) void InitializeRecords(UDataTable* InTable);
    const TArray<FProfessorStudent>& GetRecords() const { return Records; }
    UFUNCTION(BlueprintCallable) bool SaveRecords(const TArray<FProfessorStudent>& NewRecords, FString& Error);
    UFUNCTION(BlueprintPure) static FProfessorGrade CalculateGrade(int32 OCA, int32 OEA, bool bHasOEA);
    UFUNCTION(BlueprintPure) static bool IsSuspicious(const FProfessorStudent& Student);
    static bool ValidateSave(const UProfessorGradeSave* Saved, FString& Error);
    static bool ApplySavedRecordsToTable(UDataTable* Table, const UProfessorGradeSave* Saved, FString& Error);
    static bool NormalizeStudentTable(UDataTable* Table, bool& bChanged, FString& Error);
    const FString& GetLastError() const { return LastError; }

private:
    friend class FProfessorSaveValidationTest;
    bool bLoggedIn = false;
    bool bInitialized = false;
    bool bInvalidSave = false;
    FString LastError;
    UPROPERTY() UDataTable* SourceTable = nullptr;
    UPROPERTY() UDataTable* EditorTableSnapshot = nullptr;
    UPROPERTY() TArray<FProfessorStudent> Records;
    UPROPERTY() TArray<FName> DeletedRowNames;
    UPROPERTY() TArray<FString> DeletedReferenceNumbers;
    int32 NextReferenceNumber = 1;
    TMap<FName, FProfessorStudent> AuthoritativeRows;

    static bool ValidateRecords(const TArray<FProfessorStudent>& Candidate, FString& Error);
    void ApplyToDataTable();
    void RestoreAuthoritativeScores(TArray<FProfessorStudent>& Candidate) const;
};
