#include "GradingStore.h"
#include "GradingSessionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingSessionTest, "GradingRuntime.Stage4.SessionLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingSessionTest::RunTest(const FString& Parameters)
{
    UDataTable* Initial = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    if (!TestNotNull(TEXT("Initial table exists"), Initial)) return false;
    const FString Original = Initial->GetTableAsJSON();
    const bool WasDirty = Initial->GetOutermost()->IsDirty();
    UGameInstance* GI = NewObject<UGameInstance>();
    UGradingStore* Store = NewObject<UGradingStore>(GI);
    Store->InitializeRecords(Initial);
    if (!TestTrue(TEXT("Session initialized"), Store->bInitialized)) { AddError(Store->LastError); return false; }
    TestTrue(TEXT("Session table is a separate transient object"), Store->GetSessionTable() != Initial && Store->GetSessionTable()->HasAnyFlags(RF_Transient));
    const float Expected[] = {76.8f,80.8f,83.f,60.4f,66.6f,69.2f,63.2f,79.8f,67.8f};
    TestEqual(TEXT("New session has nine authored students"), Store->Records.Num(), 9);
    for (int32 I = 0; I < 9; ++I)
    {
        const FString Ref = TEXT("Student") + FString::FromInt(I + 1);
        const auto* S = Store->Records.FindByPredicate([&Ref](const FProfessorStudent& X){return X.ReferenceNumber == Ref;});
        if (!TestNotNull(Ref, S)) return false;
        TestTrue(Ref + TEXT(" initial OCA"), FMath::IsNearlyEqual(S->OCA, Expected[I], .0001f));
        TestEqual(Ref + TEXT(" paper identity"), S->PaperReference, Ref);
    }
    const FStudentSessionSnapshot NewGame = Store->CaptureStudentSnapshot();
    TArray<FProfessorStudent> Draft = Store->Records;
    const FString KeptRef = Draft[0].ReferenceNumber;
    const FString PaperRef = Draft[0].PaperReference;
    const int32 ExpectedScore = Draft[0].ExpectedScore;
    const FString DeletedRef = Draft[1].ReferenceNumber;
    Draft[0].StudentName = TEXT("Renamed Student");
    Draft[0].StudentID = TEXT("New_Student_ID");
    Draft[0].OCA = 70.44994f; Draft[0].OEA = 0; Draft[0].bHasOEA = true;
    Draft[0].PaperReference = TEXT("tampered"); Draft[0].ExpectedScore = 1;
    Draft.RemoveAt(1);
    FProfessorStudent Added; Added.StudentName = TEXT("Added Student"); Added.StudentID = TEXT("Added_ID"); Added.OCA = 61.25f;
    Draft.Add(Added);
    FString Error;
    TestEqual(TEXT("Uncommitted draft does not alter live records"), Store->Records[0].OCA, NewGame.Students[0].OCA);
    TestFalse(TEXT("Commit requires login"), Store->SaveRecords(Draft, Error));
    Store->bLoggedIn = true;
    if (!TestTrue(TEXT("Rename, decimal edit, delete and add commit"), Store->SaveRecords(Draft, Error))) { AddError(Error); return false; }
    const auto* Kept = Store->Records.FindByPredicate([&KeptRef](const FProfessorStudent& S){return S.ReferenceNumber == KeptRef;});
    if (!TestNotNull(TEXT("Stable reference survives rename"), Kept)) return false;
    TestEqual(TEXT("RowName follows renamed display name"), Kept->RowName, FName(TEXT("RenamedStudent")));
    TestEqual(TEXT("Paper relation cannot be edited through grade form"), Kept->PaperReference, PaperRef);
    TestEqual(TEXT("Expected score remains authoritative"), Kept->ExpectedScore, ExpectedScore);
    TestEqual(TEXT("Decimal preserved exactly"), Kept->OCA, 70.44994f);
    TestTrue(TEXT("Explicit zero remains graded"), Kept->bHasOEA);
    TestFalse(TEXT("Deleted student absent"), Store->Records.ContainsByPredicate([&DeletedRef](const FProfessorStudent& S){return S.ReferenceNumber == DeletedRef;}));
    TestEqual(TEXT("New student takes high-water reference"), Store->Records.Last().ReferenceNumber, TEXT("Student") + FString::FromInt(NewGame.NextStudentReferenceNumber));
    TestTrue(TEXT("Added student has no borrowed paper"), Store->Records.Last().PaperReference.IsEmpty());

    UGradingGameSave* Save = NewObject<UGradingGameSave>(); Save->StudentSnapshot = Store->CaptureStudentSnapshot();
    TArray<uint8> Bytes;
    TestTrue(TEXT("Complete snapshot serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
    UGradingGameSave* Reloaded = Cast<UGradingGameSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Complete snapshot reloads"), Reloaded)) return false;
    UGradingStore* Fresh = NewObject<UGradingStore>(GI); Fresh->InitializeRecords(Initial);
    if (!TestTrue(TEXT("Fresh session applies complete snapshot"), Fresh->ApplyStudentSnapshot(Reloaded->StudentSnapshot, Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Load never merges missing initial rows"), Fresh->Records.Num(), Draft.Num());
    TestFalse(TEXT("Deleted student is not resurrected"), Fresh->Records.ContainsByPredicate([&DeletedRef](const FProfessorStudent& S){return S.ReferenceNumber == DeletedRef;}));
    TestEqual(TEXT("Load retains saved OCA instead of initial formula"), Fresh->Records[0].OCA, 70.44994f);
    TestFalse(TEXT("Authentication is not restored"), Fresh->IsLoggedIn());
    TestEqual(TEXT("Runtime table contains exact snapshot row count"), Fresh->SourceTable->GetRowMap().Num(), Fresh->Records.Num());
    const auto& Restored = Fresh->Records[0];
    for (TFieldIterator<FProperty> It(Initial->GetRowStruct()); It; ++It)
        if (It->GetName().StartsWith(TEXT("Page1_Img_")))
        {
            FObjectPropertyBase* Page = CastField<FObjectPropertyBase>(*It);
            const uint8* Before = Store->SourceTable->FindRowUnchecked(Restored.RowName);
            const uint8* After = Fresh->SourceTable->FindRowUnchecked(Restored.RowName);
            TestTrue(TEXT("Paper asset relation survives rename and load"), Page && Before && After && Page->GetObjectPropertyValue_InContainer(Before) == Page->GetObjectPropertyValue_InContainer(After));
        }
    const FString BeforeInvalid = Fresh->SourceTable->GetTableAsJSON();
    auto Invalid = Reloaded->StudentSnapshot; Invalid.FormatVersion = 99;
    TestFalse(TEXT("Unknown version rejected"), Fresh->ApplyStudentSnapshot(Invalid, Error));
    Invalid = Reloaded->StudentSnapshot; Invalid.NextStudentReferenceNumber = 1;
    TestFalse(TEXT("Reused reference counter rejected"), Fresh->ApplyStudentSnapshot(Invalid, Error));
    Invalid = Reloaded->StudentSnapshot; Invalid.Students[0].PaperReference = TEXT("MissingPaper");
    TestFalse(TEXT("Unresolvable paper relation rejected"), Fresh->ApplyStudentSnapshot(Invalid, Error));
    TestEqual(TEXT("Rejected loads leave live table untouched"), Fresh->SourceTable->GetTableAsJSON(), BeforeInvalid);
    Fresh->bLoggedIn = true;
    auto InvalidDraft = Fresh->Records; InvalidDraft[1].StudentID = InvalidDraft[0].StudentID;
    TestFalse(TEXT("Duplicate IDs reject the entire edit"), Fresh->SaveRecords(InvalidDraft, Error));
    TestEqual(TEXT("Rejected edit does not partially apply"), Fresh->SourceTable->GetTableAsJSON(), BeforeInvalid);

    const int32 HighWater = Fresh->NextReferenceNumber;
    TestTrue(TEXT("Deleting all students is valid"), Fresh->SaveRecords({}, Error));
    const auto Empty = Fresh->CaptureStudentSnapshot();
    TestTrue(TEXT("Empty snapshot is explicitly present"), Empty.bHasStudentSnapshot);
    TestEqual(TEXT("Empty snapshot keeps reference counter"), Empty.NextStudentReferenceNumber, HighWater);
    TestTrue(TEXT("Empty snapshot restores"), Store->ApplyStudentSnapshot(Empty, Error));
    TestEqual(TEXT("Empty snapshot stays empty"), Store->GetSessionTable()->GetRowMap().Num(), 0);
    Store->bLoggedIn = true;
    TestTrue(TEXT("Add after empty restore succeeds"), Store->SaveRecords({Added}, Error));
    TestEqual(TEXT("Deleted highest reference is not reused"), Store->Records[0].ReferenceNumber, TEXT("Student") + FString::FromInt(HighWater));
    bool Legacy = false;
    UGradingGameSave* Old = NewObject<UGradingGameSave>();
    TestTrue(TEXT("Old missing tags have an explicit compatibility path"), UGradingSessionLibrary::ValidateGameSave(Old, Legacy, Error));
    TestTrue(TEXT("Absent snapshot is legacy"), Legacy);
    Old->StudentSnapshot = Empty;
    TestTrue(TEXT("Versioned zero-student snapshot valid"), UGradingSessionLibrary::ValidateGameSave(Old, Legacy, Error));
    TestFalse(TEXT("Zero students is not legacy"), Legacy);
    Old->StudentSnapshot.FormatVersion = 99;
    TestFalse(TEXT("Unknown format cannot use legacy fallback"), UGradingSessionLibrary::ValidateGameSave(Old, Legacy, Error));
    TestTrue(TEXT("New Game resets session"), Store->StartNewSession(Error));
    TestEqual(TEXT("New Game restores nine initial students"), Store->Records.Num(), 9);
    TestEqual(TEXT("New Game resets reference high water"), Store->NextReferenceNumber, NewGame.NextStudentReferenceNumber);
    TestEqual(TEXT("Initial table stays byte-for-byte equivalent in JSON"), Initial->GetTableAsJSON(), Original);
    TestEqual(TEXT("Initial asset dirty state unchanged"), Initial->GetOutermost()->IsDirty(), WasDirty);
    return true;
}
#endif
