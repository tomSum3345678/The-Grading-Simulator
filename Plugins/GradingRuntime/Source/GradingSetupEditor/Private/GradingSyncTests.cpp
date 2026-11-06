#include "GradingStore.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "HAL/FileManager.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingEditorSyncTest, "GradingRuntime.Editor.SaveToDataTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingEditorSyncTest::RunTest(const FString& Parameters)
{
    UDataTable* Source = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    UUserDefinedStruct* Struct = Source ? Cast<UUserDefinedStruct>(Source->RowStruct) : nullptr;
    if (!TestNotNull(TEXT("StudentData schema is installed"), Struct) || !TestTrue(TEXT("Initial fixture is available"), Source && Source->GetRowMap().Num() > 0)) return false;
    FIntProperty* Expected = CastField<FIntProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("ExpectedScore")));
    FStrProperty* Reference = CastField<FStrProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("Reference_Number")));
    FObjectProperty* Texture = CastField<FObjectProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("Page1_Img")));
    FIntProperty* OEA = CastField<FIntProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("ExamScore")));
    FStrProperty* StudentName = CastField<FStrProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("StudentName")));
    FStrProperty* StudentID = CastField<FStrProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("StudentID")));
    if (!TestTrue(TEXT("Required preservation properties exist"), Expected && Reference && Texture && OEA && StudentName && StudentID)) return false;

    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/GradingSync");
    IFileManager::Get().MakeDirectory(*Folder, true);
    FPackageName::RegisterMountPoint(TEXT("/GradingSyncTests/"), Folder + TEXT("/"));
    const FString FixtureName = TEXT("Students_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = TEXT("/GradingSyncTests/") + FixtureName;
    UPackage* Package = CreatePackage(*PackageName);
    UDataTable* Fixture = NewObject<UDataTable>(Package, TEXT("Students"), RF_Public | RF_Standalone);
    Fixture->RowStruct = Struct;
    const uint8* Original = Source->GetRowMap().CreateConstIterator().Value();
    Fixture->AddRow(TEXT("Retained"), *reinterpret_cast<const FTableRowBase*>(Original));
    Fixture->AddRow(TEXT("Deleted"), *reinterpret_cast<const FTableRowBase*>(Original));
    Fixture->AddRow(TEXT("Unaffected"), *reinterpret_cast<const FTableRowBase*>(Original));
    uint8* Retained = Fixture->FindRowUnchecked(TEXT("Retained"));
    Expected->SetPropertyValue_InContainer(Retained, 60);
    Reference->SetPropertyValue_InContainer(Retained, TEXT("PRESERVE_REFERENCE"));
    uint8* Unaffected = Fixture->FindRowUnchecked(TEXT("Unaffected"));
    StudentName->SetPropertyValue_InContainer(Unaffected, TEXT("Unchanged Student"));
    StudentID->SetPropertyValue_InContainer(Unaffected, TEXT("SYNC999"));
    Reference->SetPropertyValue_InContainer(Unaffected, TEXT("Student3"));
    UObject* OriginalTexture = Texture->GetObjectPropertyValue_InContainer(Retained);

    UProfessorGradeSave* Save = NewObject<UProfessorGradeSave>();
    FProfessorStudent Student;
    Student.RowName = TEXT("Retained");
    Student.StudentName = TEXT("Updated Student");
    Student.StudentID = TEXT("SYNC001");
    Student.OCA = 80;
    Student.OEA = 76;
    Student.bHasOEA = true;
    Student.ExpectedScore = 100; // A saved player's copy must not alter the marking standard.
    Student.bHasExpectedScore = true;
    Save->Students.Add(Student);
    Student.RowName = TEXT("Added");
    Student.StudentName = TEXT("New Student");
    Student.StudentID = TEXT("SYNC002");
    Save->Students.Add(Student);
    Save->DeletedRowNames.Add(TEXT("Deleted"));
    FString Error;
    if (!TestTrue(TEXT("Valid save applies to the isolated DataTable"), UGradingStore::ApplySavedRecordsToTable(Fixture, Save, Error)))
    {
        AddError(Error);
        return false;
    }
    uint8* Added = Fixture->FindRowUnchecked(TEXT("NewStudent"));
    if (!TestNotNull(TEXT("Added row uses its student name"), Added)) return false;
    TestTrue(TEXT("Explicit tombstone removes row"), Fixture->FindRowUnchecked(TEXT("Deleted")) == nullptr);
    TestTrue(TEXT("Unmentioned row is retained"), Fixture->FindRowUnchecked(TEXT("UnchangedStudent")) != nullptr);
    Retained = Fixture->FindRowUnchecked(TEXT("UpdatedStudent"));
    if (!TestNotNull(TEXT("Edited student row uses its updated name"), Retained)) return false;
    TestEqual(TEXT("OEA synchronized"), OEA->GetPropertyValue_InContainer(Retained), 76);
    TestEqual(TEXT("Authoritative expected score preserved"), Expected->GetPropertyValue_InContainer(Retained), 60);
    TestEqual(TEXT("Paper reference preserved"), Reference->GetPropertyValue_InContainer(Retained), FString(TEXT("PRESERVE_REFERENCE")));
    TestEqual(TEXT("Paper image preserved"), Texture->GetObjectPropertyValue_InContainer(Retained), OriginalTexture);
    TestEqual(TEXT("New rows do not fabricate expected marks"), Expected->GetPropertyValue_InContainer(Added), -1);
    TestEqual(TEXT("New row reference follows the existing sequence"), Reference->GetPropertyValue_InContainer(Added), FString(TEXT("Student4")));

    // Multiple game saves can arrive before a locked asset is writable. The newest
    // snapshot must replace the earlier imported marks before the eventual disk save.
    Save->Students[0].OEA = 88;
    if (!TestTrue(TEXT("A newer save supersedes a pending imported snapshot"), UGradingStore::ApplySavedRecordsToTable(Fixture, Save, Error))) return false;
    TestEqual(TEXT("Newest OEA is present before deferred persistence"), OEA->GetPropertyValue_InContainer(Fixture->FindRowUnchecked(TEXT("UpdatedStudent"))), 88);

    const FString Filename = Folder / (FixtureName + TEXT(".uasset"));
    if (!TestTrue(TEXT("Isolated DataTable asset saves"), UPackage::SavePackage(Package, Fixture, RF_Public | RF_Standalone, *Filename))) return false;
    Package->SetDirtyFlag(false);
    Fixture = nullptr;
    TArray<UPackage*> Packages;
    Packages.Add(Package);
    if (!TestTrue(TEXT("Isolated package unloads"), UPackageTools::UnloadPackages(Packages))) return false;
    Package = LoadPackage(nullptr, *PackageName, LOAD_None);
    UDataTable* Reloaded = Package ? FindObject<UDataTable>(Package, TEXT("Students")) : nullptr;
    if (!TestNotNull(TEXT("Saved DataTable asset reloads"), Reloaded)) return false;
    TestTrue(TEXT("Added row survives reload"), Reloaded->FindRowUnchecked(TEXT("NewStudent")) != nullptr);
    TestTrue(TEXT("Deleted row stays deleted after reload"), Reloaded->FindRowUnchecked(TEXT("Deleted")) == nullptr);
    TestEqual(TEXT("Persisted row count"), Reloaded->GetRowMap().Num(), 3);
    Retained = Reloaded->FindRowUnchecked(TEXT("UpdatedStudent"));
    if (!TestNotNull(TEXT("Renamed existing row survives reload"), Retained)) return false;
    TestEqual(TEXT("Newest pending OEA survives reload"), OEA->GetPropertyValue_InContainer(Retained), 88);
    TestEqual(TEXT("Authoritative expected score survives reload"), Expected->GetPropertyValue_InContainer(Retained), 60);
    TestEqual(TEXT("Paper image survives reload"), Texture->GetObjectPropertyValue_InContainer(Retained), OriginalTexture);

    // Simulate a committed edit in the DataTable row editor. The live editor watcher
    // calls this same normalizer on its next tick without saving unrelated author edits.
    StudentName->SetPropertyValue_InContainer(Retained, TEXT(" New\tStudent "));
    bool bChanged = false;
    if (!TestTrue(TEXT("Direct StudentName edit normalizes"), UGradingStore::NormalizeStudentTable(Reloaded, bChanged, Error))) return false;
    TestTrue(TEXT("Name edit reports a change"), bChanged);
    TestTrue(TEXT("Whitespace-free duplicate receives a stable suffix"), Reloaded->FindRowUnchecked(TEXT("NewStudent")) && Reloaded->FindRowUnchecked(TEXT("NewStudent_2")));
    uint8* RenamedRetained = nullptr;
    for (const auto& Pair : Reloaded->GetRowMap())
    {
        if (Reference->GetPropertyValue_InContainer(Pair.Value) == TEXT("PRESERVE_REFERENCE")) RenamedRetained = Pair.Value;
    }
    if (!TestNotNull(TEXT("Stable reference retains identity after rename"), RenamedRetained)) return false;
    TestEqual(TEXT("Rename preserves the entire paper image field"), Texture->GetObjectPropertyValue_InContainer(RenamedRetained), OriginalTexture);
    TestEqual(TEXT("Rename preserves authoritative expected score"), Expected->GetPropertyValue_InContainer(RenamedRetained), 60);
    if (!TestTrue(TEXT("Repeated normalization succeeds"), UGradingStore::NormalizeStudentTable(Reloaded, bChanged, Error))) return false;
    TestFalse(TEXT("Repeated normalization makes no changes"), bChanged);
    return true;
}
#endif
