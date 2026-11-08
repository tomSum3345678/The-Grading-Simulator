#include "GradingSessionLibrary.h"
#include "GradingStore.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
    bool Fail(const UObject* Context, const FString& Message)
    {
        UE_LOG(LogTemp, Warning, TEXT("STAGE4: %s"), *Message);
        UKismetSystemLibrary::PrintString(Context, Message, true, false, FLinearColor::Red, 6.f);
        return false;
    }
}

UGradingStore* UGradingSessionLibrary::Store(const UObject* Context)
{
    UGameInstance* GI = Cast<UGameInstance>(const_cast<UObject*>(Context));
    if (!GI && GEngine)
    {
        UWorld* World = GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull);
        if (World) GI = World->GetGameInstance();
    }
    return GI ? GI->GetSubsystem<UGradingStore>() : nullptr;
}

UDataTable* UGradingSessionLibrary::GetSessionStudentTable(const UObject* WorldContextObject)
{
    UGradingStore* Session = Store(WorldContextObject);
    return Session ? Session->GetSessionTable() : nullptr;
}

bool UGradingSessionLibrary::ValidateGameSave(const USaveGame* Save, bool& bLegacy, FString& Error)
{
    bLegacy = false;
    const UGradingGameSave* Game = Cast<UGradingGameSave>(Save);
    if (!Game) { Error = TEXT("Wrong or unreadable game save type."); return false; }
    const auto& Snapshot = Game->StudentSnapshot;
    if (Snapshot.FormatVersion == 0 && !Snapshot.bHasStudentSnapshot && Snapshot.Students.Num() == 0 && Snapshot.NextStudentReferenceNumber == 1)
    {
        bLegacy = true; Error.Empty(); return true;
    }
    return UGradingStore::ValidateStudentSnapshot(Snapshot, Error);
}

bool UGradingSessionLibrary::SaveSessionToSlot(const UObject* WorldContextObject, USaveGame* SaveGameObject, const FString& SlotName, int32 UserIndex)
{
    UGradingStore* Session = Store(WorldContextObject);
    UGradingGameSave* Game = Cast<UGradingGameSave>(SaveGameObject);
    if (!Session || !Game || SlotName.IsEmpty()) return Fail(WorldContextObject, TEXT("Game save is not ready."));
    const FStudentSessionSnapshot Snapshot = Session->CaptureStudentSnapshot();
    FString Error;
    if (!UGradingStore::ValidateStudentSnapshot(Snapshot, Error)) return Fail(WorldContextObject, Error);
    if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
    {
        USaveGame* Previous = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);
        bool Legacy = false;
        if (!ValidateGameSave(Previous, Legacy, Error)) return Fail(WorldContextObject, TEXT("Existing save was preserved: ") + Error);
        TArray<uint8> Bytes;
        if (!UGameplayStatics::LoadDataFromSlot(Bytes, SlotName, UserIndex)) return Fail(WorldContextObject, TEXT("Could not read previous save for backup."));
        if (Legacy && !UGameplayStatics::DoesSaveGameExist(SlotName + TEXT("_BeforeStage4"), UserIndex)
            && !UGameplayStatics::SaveDataToSlot(Bytes, SlotName + TEXT("_BeforeStage4"), UserIndex))
            return Fail(WorldContextObject, TEXT("Could not preserve the legacy test save."));
        if (!UGameplayStatics::SaveDataToSlot(Bytes, SlotName + TEXT("_Backup"), UserIndex)) return Fail(WorldContextObject, TEXT("Could not back up the previous game save."));
    }
    Game->StudentSnapshot = Snapshot;
    if (!UGameplayStatics::SaveGameToSlot(Game, SlotName, UserIndex)) return Fail(WorldContextObject, TEXT("Could not write game save."));
    UE_LOG(LogTemp, Display, TEXT("STAGE4 SAVE slot=%s students=%d next=%d"), *SlotName, Snapshot.Students.Num(), Snapshot.NextStudentReferenceNumber);
    return true;
}

bool UGradingSessionLibrary::RestoreSessionFromSave(const UObject* WorldContextObject, USaveGame* SaveGameObject)
{
    UGradingStore* Session = Store(WorldContextObject);
    bool Legacy = false; FString Error;
    if (!Session || !ValidateGameSave(SaveGameObject, Legacy, Error)) return Fail(WorldContextObject, Error.IsEmpty() ? TEXT("Student session is unavailable.") : Error);
    if (Legacy)
    {
        if (!Session->StartNewSession(Error)) return Fail(WorldContextObject, Error);
        UE_LOG(LogTemp, Display, TEXT("STAGE4 LEGACY: position-only test save; students start from initial data. Original file preserved."));
        UKismetSystemLibrary::PrintString(WorldContextObject, TEXT("Legacy position save: students use initial data."), true, false, FLinearColor::Yellow, 6.f);
        return true;
    }
    if (!Session->ApplyStudentSnapshot(CastChecked<UGradingGameSave>(SaveGameObject)->StudentSnapshot, Error)) return Fail(WorldContextObject, Error);
    return true;
}

void UGradingSessionLibrary::OpenNewGame(const UObject* WorldContextObject, FName LevelName, bool bAbsolute, FString Options)
{
    UGradingStore* Session = Store(WorldContextObject);
    FString Error;
    if (!Session || !Session->StartNewSession(Error)) { Fail(WorldContextObject, Error.IsEmpty() ? TEXT("Cannot start student session.") : Error); return; }
    UGameplayStatics::OpenLevel(WorldContextObject, LevelName, bAbsolute, Options);
}
