#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GradingSetupLibrary.generated.h"

/** Idempotent editor migration; never runs in a packaged game. */
UCLASS()
class GRADINGSETUPEDITOR_API UGradingSetupLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Grading|Setup")
    static FString Install();

    UFUNCTION(BlueprintCallable, Category="Grading|Setup")
    static FString ExportStudentsJSON();

    /** Copies a newer durable game save into the editor DataTable without discarding dirty edits. */
    UFUNCTION(BlueprintCallable, Category="Grading|Setup")
    static FString SyncSavedGrades(bool Force = false);

    /** Updates row keys/references after authoring StudentName; leaves changes unsaved for review. */
    UFUNCTION(BlueprintCallable, Category="Grading|Setup")
    static FString NormalizeStudentRows();

    /** Installs Options menu entry points, presentation hooks and audio routing. */
    UFUNCTION(BlueprintCallable, Category="Grading|Setup")
    static FString InstallOptions();
};
