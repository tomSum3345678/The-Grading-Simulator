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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grading") FString Account;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grading") FString Password;
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FProfessorStudent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FString StudentName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FString StudentID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FName RowName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FString ReferenceNumber;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") float OCA = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") int32 OEA = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") bool bHasOEA = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") int32 ExpectedScore = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") bool bHasExpectedScore = false;
    // Stable reference to authored paper assets; no textures or widget objects in saves.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FString PaperReference;
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FStudentSessionSnapshot
{
    GENERATED_BODY()
    // Absent tags in pre-stage-4 saves must remain distinguishable from an empty class.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") int32 FormatVersion = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") bool bHasStudentSnapshot = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") TArray<FProfessorStudent> Students;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") int32 NextStudentReferenceNumber = 1;
};

UCLASS(BlueprintType)
class GRADINGRUNTIME_API UGradingGameSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Grading") FStudentSessionSnapshot StudentSnapshot;
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FProfessorGrade
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Grading") float Total = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Grading") FString Grade = TEXT("—");
    UPROPERTY(BlueprintReadOnly, Category="Grading") float GPA = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Grading") bool bGraded = false;
};

UCLASS()
class GRADINGRUNTIME_API UProfessorGradeSave : public USaveGame
{
    GENERATED_BODY()
public:
    // Keep the legacy default for old saves where the tag may be absent. New writes explicitly use 2.
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
    UFUNCTION(BlueprintCallable, Category="Grading") bool Login(const FString& Account, const FString& Password);
    UFUNCTION(BlueprintCallable, Category="Grading") void Logout();
    UFUNCTION(BlueprintPure, Category="Grading") bool IsLoggedIn() const { return bLoggedIn; }

    UFUNCTION(BlueprintCallable, Category="Grading") void InitializeRecords(UDataTable* InTable);
    UFUNCTION(BlueprintCallable, Category="Grading") bool StartNewSession(FString& Error);
    UFUNCTION(BlueprintPure, Category="Grading") UDataTable* GetSessionTable() const { return SourceTable; }
    UFUNCTION(BlueprintPure, Category="Grading") FStudentSessionSnapshot CaptureStudentSnapshot() const;
    UFUNCTION(BlueprintCallable, Category="Grading") bool ApplyStudentSnapshot(const FStudentSessionSnapshot& Snapshot, FString& Error);
    static bool ValidateStudentSnapshot(const FStudentSessionSnapshot& Snapshot, FString& Error);
    const TArray<FProfessorStudent>& GetRecords() const { return Records; }
    UFUNCTION(BlueprintCallable, Category="Grading") bool SaveRecords(const TArray<FProfessorStudent>& NewRecords, FString& Error);
    UFUNCTION(BlueprintPure, Category="Grading") static FProfessorGrade CalculateGrade(float OCA, int32 OEA, bool bHasOEA);
    UFUNCTION(BlueprintPure, Category="Grading") static bool IsSuspicious(const FProfessorStudent& Student);
    // Decimal OCA only; OEA keeps its separate integer parser.
    static bool TryParseOCA(const FString& Raw, float& OutOCA);
    static bool ValidateSave(const UProfessorGradeSave* Saved, FString& Error);
    static bool ApplySavedRecordsToTable(UDataTable* Table, const UProfessorGradeSave* Saved, FString& Error);
    static bool NormalizeStudentTable(UDataTable* Table, bool& bChanged, FString& Error);
    const FString& GetLastError() const { return LastError; }

private:
    friend class FProfessorSaveValidationTest;
    friend class FGradingSessionTest;
    bool bLoggedIn = false;
    bool bInitialized = false;
    bool bInvalidSave = false;
    FString LastError;
    UPROPERTY() UDataTable* SourceTable = nullptr;
    UPROPERTY() UDataTable* InitialTable = nullptr;
    UPROPERTY() TArray<FProfessorStudent> Records;
    UPROPERTY() TArray<FName> DeletedRowNames;
    UPROPERTY() TArray<FString> DeletedReferenceNumbers;
    int32 NextReferenceNumber = 1;
    TMap<FName, FProfessorStudent> AuthoritativeRows;

    static bool ValidateRecords(const TArray<FProfessorStudent>& Candidate, FString& Error);
    void ApplyToDataTable();
    void RestoreAuthoritativeScores(TArray<FProfessorStudent>& Candidate) const;
    bool BuildSessionTable(const TArray<FProfessorStudent>& Students, int32 Next, UDataTable*& Result, FString& Error) const;
};
