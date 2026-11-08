#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GradingSessionLibrary.generated.h"

class UDataTable;
class USaveGame;
class UGradingStore;

UCLASS()
class GRADINGRUNTIME_API UGradingSessionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Grading|Session", meta=(WorldContext="WorldContextObject"))
    static UDataTable* GetSessionStudentTable(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="Grading|Session", meta=(WorldContext="WorldContextObject"))
    static bool SaveSessionToSlot(const UObject* WorldContextObject, USaveGame* SaveGameObject, const FString& SlotName, int32 UserIndex);

    UFUNCTION(BlueprintCallable, Category="Grading|Session", meta=(WorldContext="WorldContextObject"))
    static bool RestoreSessionFromSave(const UObject* WorldContextObject, USaveGame* SaveGameObject);

    UFUNCTION(BlueprintCallable, Category="Grading|Session", meta=(WorldContext="WorldContextObject", AdvancedDisplay="2"))
    static void OpenNewGame(const UObject* WorldContextObject, FName LevelName, bool bAbsolute = true, FString Options = FString(TEXT("")));

    static bool ValidateGameSave(const USaveGame* Save, bool& bLegacy, FString& Error);
private:
    static UGradingStore* Store(const UObject* Context);
};
