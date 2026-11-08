#include "GradingStore.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorGradeBoundaryTest, "GradingRuntime.Grades.Boundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorGradeBoundaryTest::RunTest(const FString& Parameters)
{
    struct FCase { int32 OCA; int32 OEA; const TCHAR* Grade; float GPA; };
    const FCase Cases[] = {
        { 48, 100, TEXT("F"), 0.f }, { 100, 49, TEXT("F"), 0.f },
        { 50, 69, TEXT("F"), 0.f }, { 50, 70, TEXT("D"), 1.f },
        { 60, 60, TEXT("D"), 1.f }, { 69, 70, TEXT("D"), 1.f },
        { 70, 70, TEXT("C"), 2.f }, { 79, 80, TEXT("C"), 2.f },
        { 80, 80, TEXT("B"), 3.f }, { 89, 90, TEXT("B"), 3.f },
        { 90, 90, TEXT("A"), 4.f }, { 100, 100, TEXT("A"), 4.f }
    };
    for (const FCase& Case : Cases)
    {
        const FProfessorGrade Result = UGradingStore::CalculateGrade(Case.OCA, Case.OEA, true);
        const FString Label = FString::Printf(TEXT("OCA=%d OEA=%d"), Case.OCA, Case.OEA);
        TestEqual(Label + TEXT(" grade"), Result.Grade, FString(Case.Grade));
        TestEqual(Label + TEXT(" GPA"), Result.GPA, Case.GPA);
        TestEqual(Label + TEXT(" total"), Result.Total, .5f * (Case.OCA + Case.OEA));
        TestTrue(Label + TEXT(" graded"), Result.bGraded);
    }
    TestFalse(TEXT("Blank OEA remains ungraded"), UGradingStore::CalculateGrade(100, 0, false).bGraded);
    TestEqual(TEXT("Explicit zero OEA is F"), UGradingStore::CalculateGrade(100, 0, true).Grade, FString(TEXT("F")));

    FProfessorStudent Student;
    Student.ExpectedScore = 60;
    Student.bHasExpectedScore = Student.bHasOEA = true;
    Student.OEA = 45;
    TestFalse(TEXT("15 below expected is allowed"), UGradingStore::IsSuspicious(Student));
    Student.OEA = 75;
    TestFalse(TEXT("15 above expected is allowed"), UGradingStore::IsSuspicious(Student));
    Student.OEA = 44;
    TestTrue(TEXT("16 below expected is suspicious"), UGradingStore::IsSuspicious(Student));
    Student.OEA = 76;
    TestTrue(TEXT("16 above expected is suspicious"), UGradingStore::IsSuspicious(Student));
    Student.bHasExpectedScore = false;
    TestFalse(TEXT("Unknown expected score is not judged"), UGradingStore::IsSuspicious(Student));
    Student.bHasExpectedScore = true;
    Student.bHasOEA = false;
    TestFalse(TEXT("Ungraded paper is not judged"), UGradingStore::IsSuspicious(Student));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorSaveValidationTest, "GradingRuntime.Grades.SaveValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorSaveValidationTest::RunTest(const FString& Parameters)
{
    FProfessorStudent Student;
    Student.StudentName = TEXT("Example Student");
    Student.StudentID = TEXT("41000001");
    Student.RowName = TEXT("Student1");
    Student.OCA = 100;
    Student.OEA = 0;
    Student.bHasOEA = true;
    TArray<FProfessorStudent> Rows;
    Rows.Add(Student);
    FString Error;
    TestTrue(TEXT("Score endpoints are valid"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[0].OCA = 101;
    TestFalse(TEXT("OCA over 100 rejected"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[0].OCA = 0;
    Rows[0].OEA = -1;
    TestFalse(TEXT("Negative entered OEA rejected"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[0].bHasOEA = false;
    TestTrue(TEXT("Absent OEA is allowed"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[0] = Student;
    Rows.Add(Student);
    Rows[1].RowName = TEXT("Student2");
    TestFalse(TEXT("Duplicate student IDs rejected"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[1].StudentID = TEXT("41000002");
    Rows[1].RowName = TEXT("Student1");
    TestFalse(TEXT("Duplicate stable identities rejected"), UGradingStore::ValidateRecords(Rows, Error));
    Rows.SetNum(1);
    Rows[0].StudentName = TEXT("   ");
    TestFalse(TEXT("Blank student name rejected"), UGradingStore::ValidateRecords(Rows, Error));
    Rows[0] = Student;
    Rows[0].bHasExpectedScore = true;
    Rows[0].ExpectedScore = 101;
    TestFalse(TEXT("Invalid expected score rejected"), UGradingStore::ValidateRecords(Rows, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorSaveRoundTripTest, "GradingRuntime.Grades.SaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorSaveRoundTripTest::RunTest(const FString& Parameters)
{
    UProfessorGradeSave* Original = Cast<UProfessorGradeSave>(UGameplayStatics::CreateSaveGameObject(UProfessorGradeSave::StaticClass()));
    FProfessorStudent Existing;
    Existing.RowName = TEXT("Student1");
    Existing.ReferenceNumber = TEXT("Student1");
    Existing.StudentName = TEXT("Example Student");
    Existing.StudentID = TEXT("41000001");
    Existing.OCA = 62.25f;
    Existing.OEA = 76;
    Existing.bHasOEA = true;
    Existing.ExpectedScore = 60;
    Existing.bHasExpectedScore = true;
    Original->Students.Add(Existing);
    FProfessorStudent Added;
    Added.RowName = TEXT("Professor_NewRecord");
    Added.ReferenceNumber = TEXT("Student2");
    Added.StudentName = TEXT("New Student");
    Added.StudentID = TEXT("41000002");
    Added.OCA = 81;
    Added.bHasOEA = false;
    Original->Students.Add(Added);
    Original->NextReferenceNumber = 3;
    TArray<uint8> Bytes;
    TestTrue(TEXT("Save serialization succeeds without disk access"), UGameplayStatics::SaveGameToMemory(Original, Bytes));
    UProfessorGradeSave* Restored = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Save class survives serialization"), Restored)) return false;
    TestEqual(TEXT("Schema version survives"), Restored->FormatVersion, 1);
    if (!TestEqual(TEXT("Existing and added students survive"), Restored->Students.Num(), 2)) return false;
    const FProfessorStudent& First = Restored->Students[0];
    TestEqual(TEXT("Row identity survives"), First.RowName, Existing.RowName);
    TestEqual(TEXT("Stable reference survives"), First.ReferenceNumber, Existing.ReferenceNumber);
    TestEqual(TEXT("Reference counter survives"), Restored->NextReferenceNumber, 3);
    TestEqual(TEXT("Name survives"), First.StudentName, Existing.StudentName);
    TestEqual(TEXT("Student ID survives"), First.StudentID, Existing.StudentID);
    TestEqual(TEXT("OCA survives"), First.OCA, Existing.OCA);
    TestEqual(TEXT("OEA survives"), First.OEA, Existing.OEA);
    TestTrue(TEXT("Explicit OEA presence survives"), First.bHasOEA);
    TestEqual(TEXT("Expected mark survives"), First.ExpectedScore, Existing.ExpectedScore);
    TestTrue(TEXT("Expected mark presence survives"), First.bHasExpectedScore);
    TestTrue(TEXT("Fairness check survives reload"), UGradingStore::IsSuspicious(First));
    TestEqual(TEXT("Added student identity survives"), Restored->Students[1].RowName, Added.RowName);
    TestEqual(TEXT("Added student OCA survives"), Restored->Students[1].OCA, Added.OCA);
    TestFalse(TEXT("Missing OEA stays missing"), Restored->Students[1].bHasOEA);
    TestFalse(TEXT("Added student has no fabricated expected mark"), Restored->Students[1].bHasExpectedScore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorLoginTest, "GradingRuntime.Accounts.Login",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorLoginTest::RunTest(const FString& Parameters)
{
    UDataTable* Accounts = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_ProfessorAccounts.DT_ProfessorAccounts"));
    if (!TestNotNull(TEXT("Configured professor account table is available"), Accounts)) return false;
    if (!TestTrue(TEXT("Account table uses native account schema"), Accounts->GetRowStruct() == FProfessorAccountRow::StaticStruct())) return false;
    const FProfessorAccountRow* ValidAccount = nullptr;
    for (const TPair<FName, uint8*>& Pair : Accounts->GetRowMap())
    {
        const FProfessorAccountRow* Row = reinterpret_cast<const FProfessorAccountRow*>(Pair.Value);
        if (Row && !Row->Account.IsEmpty() && !Row->Password.IsEmpty()) { ValidAccount = Row; break; }
    }
    if (!TestNotNull(TEXT("At least one professor account is configured"), ValidAccount)) return false;
    // Read current authored credentials instead of changing the asset or depending on unchanged defaults.
    UGameInstance* TestInstance = NewObject<UGameInstance>();
    UGradingStore* Store = NewObject<UGradingStore>(TestInstance);
    TestFalse(TEXT("A new session starts logged out"), Store->IsLoggedIn());
    TestTrue(TEXT("Configured credentials log in"), Store->Login(ValidAccount->Account, ValidAccount->Password));
    TestTrue(TEXT("Successful login opens the session"), Store->IsLoggedIn());
    Store->Logout();
    TestFalse(TEXT("Logout closes the session"), Store->IsLoggedIn());
    TestFalse(TEXT("An empty account is rejected"), Store->Login(FString(), ValidAccount->Password));
    TestFalse(TEXT("An empty password is rejected"), Store->Login(ValidAccount->Account, FString()));
    const FString WrongPassword = TEXT("InvalidTestPassword_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestFalse(TEXT("A wrong password is rejected"), Store->Login(ValidAccount->Account, WrongPassword));
    TestFalse(TEXT("Rejected login remains logged out"), Store->IsLoggedIn());
    TestTrue(TEXT("A valid login works after rejection"), Store->Login(ValidAccount->Account, ValidAccount->Password));
    Store->Logout();
    TestFalse(TEXT("Final logout does not persist authentication"), Store->IsLoggedIn());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorDiskRoundTripTest, "GradingRuntime.Grades.DiskSaveRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorDiskRoundTripTest::RunTest(const FString& Parameters)
{
    const FString TestSlot = TEXT("GradingRuntime_Automation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    struct FTestSaveCleanup
    {
        FString Slot;
        explicit FTestSaveCleanup(const FString& InSlot) : Slot(InSlot) {}
        ~FTestSaveCleanup() { if (UGameplayStatics::DoesSaveGameExist(Slot, 0)) UGameplayStatics::DeleteGameInSlot(Slot, 0); }
    } Cleanup(TestSlot);
    TestFalse(TEXT("Isolated test slot is initially absent"), UGameplayStatics::DoesSaveGameExist(TestSlot, 0));
    UProfessorGradeSave* Original = Cast<UProfessorGradeSave>(UGameplayStatics::CreateSaveGameObject(UProfessorGradeSave::StaticClass()));
    FProfessorStudent Student;
    Student.RowName = TEXT("Professor_AutomationStudent");
    Student.ReferenceNumber = TEXT("Student1");
    Student.StudentName = TEXT("Automation Student");
    Student.StudentID = TEXT("Automation_41000001");
    Student.OCA = 50;
    Student.OEA = 70;
    Student.bHasOEA = true;
    Student.ExpectedScore = 60;
    Student.bHasExpectedScore = true;
    Original->Students.Add(Student);
    FProfessorStudent MissingOEA = Student;
    MissingOEA.RowName = TEXT("Professor_AutomationMissing");
    MissingOEA.ReferenceNumber = TEXT("Student2");
    MissingOEA.StudentID = TEXT("Automation_41000002");
    MissingOEA.bHasOEA = false;
    Original->Students.Add(MissingOEA);
    if (!TestTrue(TEXT("Isolated grades save to disk"), UGameplayStatics::SaveGameToSlot(Original, TestSlot, 0))) return false;
    TestTrue(TEXT("Saved slot exists on disk"), UGameplayStatics::DoesSaveGameExist(TestSlot, 0));
    UProfessorGradeSave* Loaded = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromSlot(TestSlot, 0));
    if (!TestNotNull(TEXT("Grades load from the isolated disk slot"), Loaded)) return false;
    TestEqual(TEXT("Disk format version survives"), Loaded->FormatVersion, 1);
    if (!TestEqual(TEXT("Both records survive disk reload"), Loaded->Students.Num(), 2)) return false;
    TestEqual(TEXT("Student identity survives disk reload"), Loaded->Students[0].RowName, Student.RowName);
    TestEqual(TEXT("Student name survives disk reload"), Loaded->Students[0].StudentName, Student.StudentName);
    TestEqual(TEXT("Student ID survives disk reload"), Loaded->Students[0].StudentID, Student.StudentID);
    TestEqual(TEXT("OCA survives disk reload"), Loaded->Students[0].OCA, 50.f);
    TestEqual(TEXT("OEA survives disk reload"), Loaded->Students[0].OEA, 70);
    TestTrue(TEXT("Entered OEA remains present"), Loaded->Students[0].bHasOEA);
    TestFalse(TEXT("Missing OEA remains missing"), Loaded->Students[1].bHasOEA);
    TestEqual(TEXT("Expected score survives disk reload"), Loaded->Students[0].ExpectedScore, 60);
    TestEqual(TEXT("Grade is reproducible after disk reload"), UGradingStore::CalculateGrade(Loaded->Students[0].OCA, Loaded->Students[0].OEA, Loaded->Students[0].bHasOEA).Grade, FString(TEXT("D")));
    TestTrue(TEXT("The isolated test slot is removed"), UGameplayStatics::DeleteGameInSlot(TestSlot, 0));
    TestFalse(TEXT("The isolated slot no longer exists"), UGameplayStatics::DoesSaveGameExist(TestSlot, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProfessorTableMergeTest, "GradingRuntime.Grades.TableMergeAndDeletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProfessorTableMergeTest::RunTest(const FString& Parameters)
{
    UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, TEXT("/Game/DataTable/StudentData.StudentData"));
    if (!TestNotNull(TEXT("Student row schema is available"), Struct)) return false;
    auto Field = [Struct](const FString& Name) -> FProperty*
    {
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
            if (It->GetName() == Name || It->GetName().StartsWith(Name + TEXT("_"))) return *It;
        return nullptr;
    };
    FStrProperty* NameField = CastField<FStrProperty>(Field(TEXT("StudentName")));
    FStrProperty* IDField = CastField<FStrProperty>(Field(TEXT("StudentID")));
    FStrProperty* ReferenceField = CastField<FStrProperty>(Field(TEXT("Reference_Number")));
    FNumericProperty* ExpectedField = CastField<FNumericProperty>(Field(TEXT("ExpectedScore")));
    FNumericProperty* QuestionField = CastField<FNumericProperty>(Field(TEXT("Q1_Score")));
    FObjectPropertyBase* ImageField = CastField<FObjectPropertyBase>(Field(TEXT("Page1_Img")));
    if (!TestTrue(TEXT("Required and preservation-test fields exist"), NameField && IDField && ReferenceField && ExpectedField && QuestionField && ImageField)) return false;

    // Only the schema is shared. Test rows and images live in an isolated transient DataTable.
    UDataTable* Table = NewObject<UDataTable>();
    Table->RowStruct = Struct;
    UTexture2D* Paper = NewObject<UTexture2D>();
    auto AddOriginal = [&](FName RowName, const FString& ID)
    {
        FStructOnScope Row(Struct);
        NameField->SetPropertyValue_InContainer(Row.GetStructMemory(), RowName.ToString());
        IDField->SetPropertyValue_InContainer(Row.GetStructMemory(), ID);
        ReferenceField->SetPropertyValue_InContainer(Row.GetStructMemory(), RowName == FName(TEXT("KeepRow")) ? TEXT("Student1") : RowName == FName(TEXT("DeletedRow")) ? TEXT("Student2") : TEXT("Student3"));
        ExpectedField->SetIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(Row.GetStructMemory()), static_cast<int64>(65));
        QuestionField->SetIntPropertyValue(QuestionField->ContainerPtrToValuePtr<void>(Row.GetStructMemory()), static_cast<int64>(12));
        ImageField->SetObjectPropertyValue_InContainer(Row.GetStructMemory(), Paper);
        Table->AddRow(RowName, *reinterpret_cast<FTableRowBase*>(Row.GetStructMemory()));
    };
    AddOriginal(TEXT("KeepRow"), TEXT("TableTest1"));
    AddOriginal(TEXT("DeletedRow"), TEXT("TableTest2"));
    AddOriginal(TEXT("UnaffectedRow"), TEXT("TableTest3"));
    UProfessorGradeSave* Saved = NewObject<UProfessorGradeSave>();
    FProfessorStudent Updated;
    Updated.RowName = TEXT("KeepRow");
    Updated.StudentName = TEXT("Updated Student");
    Updated.StudentID = TEXT("TableTest1");
    Updated.OCA = 50;
    Updated.OEA = 70;
    Updated.bHasOEA = true;
    Updated.ExpectedScore = 10;
    Updated.bHasExpectedScore = true;
    Saved->Students.Add(Updated);
    FProfessorStudent Added = Updated;
    Added.RowName = TEXT("NewRow");
    Added.StudentName = TEXT("HONGLI");
    Added.StudentID = TEXT("TableTest4");
    Saved->Students.Add(Added);
    Saved->DeletedRowNames.Add(TEXT("DeletedRow"));
    FString Error;
    if (!TestTrue(TEXT("Valid saved edits merge into isolated table"), UGradingStore::ApplySavedRecordsToTable(Table, Saved, Error)))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("Deleted authored row is removed"), Table->GetRowMap().Contains(TEXT("DeletedRow")));
    TestTrue(TEXT("Unaffected authored row remains"), Table->GetRowMap().Contains(TEXT("UnaffectedRow")));
    TestTrue(TEXT("Added student becomes a named DataTable row"), Table->GetRowMap().Contains(TEXT("HONGLI")));
    uint8* Kept = Table->GetRowMap().FindChecked(TEXT("UpdatedStudent"));
    TestEqual(TEXT("Edited student name is applied"), NameField->GetPropertyValue_InContainer(Kept), Updated.StudentName);
    TestEqual(TEXT("Authoritative expected score is preserved"), ExpectedField->GetSignedIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(Kept)), static_cast<int64>(65));
    TestEqual(TEXT("Unrelated question mark is preserved"), QuestionField->GetSignedIntPropertyValue(QuestionField->ContainerPtrToValuePtr<void>(Kept)), static_cast<int64>(12));
    TestTrue(TEXT("Original paper texture reference is preserved"), ImageField->GetObjectPropertyValue_InContainer(Kept) == Paper);
    uint8* NewRow = Table->GetRowMap().FindChecked(TEXT("HONGLI"));
    TestEqual(TEXT("New reference follows existing sequence"), ReferenceField->GetPropertyValue_InContainer(NewRow), FString(TEXT("Student4")));
    TestEqual(TEXT("Renamed existing student retains reference"), ReferenceField->GetPropertyValue_InContainer(Kept), FString(TEXT("Student1")));
    TestEqual(TEXT("New student has unknown expected score"), ExpectedField->GetSignedIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(NewRow)), static_cast<int64>(-1));
    TestNull(TEXT("New student has no fabricated paper texture"), ImageField->GetObjectPropertyValue_InContainer(NewRow));

    TArray<uint8> Bytes;
    TestTrue(TEXT("Deletion identities serialize"), UGameplayStatics::SaveGameToMemory(Saved, Bytes));
    UProfessorGradeSave* Reloaded = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Saved deletion data reloads"), Reloaded)) return false;
    TestTrue(TEXT("Tombstone identity survives reload"), Reloaded->DeletedRowNames.Contains(TEXT("DeletedRow")));
    AddOriginal(TEXT("DeletedRow"), TEXT("TableTest2"));
    TestTrue(TEXT("Reload applies the saved deletions again"), UGradingStore::ApplySavedRecordsToTable(Table, Reloaded, Error));
    TestFalse(TEXT("Deleted default is not resurrected after reload"), Table->GetRowMap().Contains(TEXT("DeletedRow")));

    Reloaded->DeletedRowNames.Add(TEXT("KeepRow"));
    TestFalse(TEXT("Conflicting deletion and live record are rejected"), UGradingStore::ApplySavedRecordsToTable(Table, Reloaded, Error));
    TestTrue(TEXT("Invalid save does not partially delete original row"), Table->GetRowMap().Contains(TEXT("UpdatedStudent")));

    // Stable references carry full row data through a two-way name swap.
    UProfessorGradeSave* Swapped = NewObject<UProfessorGradeSave>();
    Updated.ReferenceNumber = TEXT("Student1");
    Updated.RowName = TEXT("UpdatedStudent");
    Updated.StudentName = TEXT("HONGLI");
    Added.ReferenceNumber = TEXT("Student4");
    Added.RowName = TEXT("HONGLI");
    Added.StudentName = TEXT("Updated Student");
    Swapped->Students = { Updated, Added };
    Swapped->NextReferenceNumber = 5;
    TestTrue(TEXT("Name swap succeeds by immutable references"), UGradingStore::ApplySavedRecordsToTable(Table, Swapped, Error));
    uint8* SwappedOriginal = Table->GetRowMap().FindChecked(TEXT("HONGLI"));
    TestEqual(TEXT("Original reference follows its swapped name"), ReferenceField->GetPropertyValue_InContainer(SwappedOriginal), FString(TEXT("Student1")));
    TestTrue(TEXT("Paper follows reference through swap"), ImageField->GetObjectPropertyValue_InContainer(SwappedOriginal) == Paper);
    TestNull(TEXT("New student's empty paper follows its swapped name"), ImageField->GetObjectPropertyValue_InContainer(Table->GetRowMap().FindChecked(TEXT("UpdatedStudent"))));

    // Delete one student, then add a different student with exactly the same name.
    UProfessorGradeSave* Replacement = NewObject<UProfessorGradeSave>();
    FProfessorStudent NewPerson = Updated;
    NewPerson.ReferenceNumber = TEXT("Student5");
    NewPerson.StudentID = TEXT("TableTest5");
    Replacement->Students.Add(NewPerson);
    Replacement->DeletedReferenceNumbers.Add(TEXT("Student1"));
    Replacement->NextReferenceNumber = 6;
    TestTrue(TEXT("Delete and add identical display name succeeds"), UGradingStore::ApplySavedRecordsToTable(Table, Replacement, Error));
    uint8* ReplacementRow = Table->GetRowMap().FindChecked(TEXT("HONGLI"));
    TestEqual(TEXT("Replacement has a new reference"), ReferenceField->GetPropertyValue_InContainer(ReplacementRow), FString(TEXT("Student5")));
    TestNull(TEXT("Replacement does not inherit deleted student's paper"), ImageField->GetObjectPropertyValue_InContainer(ReplacementRow));
    TestEqual(TEXT("Replacement does not inherit expected score"), ExpectedField->GetSignedIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(ReplacementRow)), static_cast<int64>(-1));

    // Deleting the highest reference does not let the editor normalizer reuse it.
    Table->RemoveRow(TEXT("HONGLI"));
    FStructOnScope Fresh(Struct);
    NameField->SetPropertyValue_InContainer(Fresh.GetStructMemory(), TEXT("New Name"));
    IDField->SetPropertyValue_InContainer(Fresh.GetStructMemory(), TEXT("TableTest6"));
    Table->AddRow(TEXT("Temporary"), *reinterpret_cast<FTableRowBase*>(Fresh.GetStructMemory()));
    bool Changed = false;
    TestTrue(TEXT("Direct editor normalization succeeds"), UGradingStore::NormalizeStudentTable(Table, Changed, Error));
    TestTrue(TEXT("New row normalization reports change"), Changed);
    TestEqual(TEXT("High water mark prevents deleted reference reuse"), ReferenceField->GetPropertyValue_InContainer(Table->GetRowMap().FindChecked(TEXT("NewName"))), FString(TEXT("Student6")));
    TestTrue(TEXT("Repeated normalization succeeds"), UGradingStore::NormalizeStudentTable(Table, Changed, Error));
    TestFalse(TEXT("Normalization is idempotent"), Changed);

    FStructOnScope Collision(Struct);
    NameField->SetPropertyValue_InContainer(Collision.GetStructMemory(), TEXT("NewName"));
    IDField->SetPropertyValue_InContainer(Collision.GetStructMemory(), TEXT("TableTest7"));
    Table->AddRow(TEXT("Temporary2"), *reinterpret_cast<FTableRowBase*>(Collision.GetStructMemory()));
    TestTrue(TEXT("Duplicate display names normalize"), UGradingStore::NormalizeStudentTable(Table, Changed, Error));
    TestTrue(TEXT("Duplicate gets deterministic suffix"), Table->GetRowMap().Contains(TEXT("NewName_2")));

    UDataTable* DuplicateReferences = NewObject<UDataTable>();
    DuplicateReferences->RowStruct = Struct;
    for (int32 I = 0; I < 2; ++I)
    {
        FStructOnScope Row(Struct);
        NameField->SetPropertyValue_InContainer(Row.GetStructMemory(), I == 0 ? TEXT("Alpha") : TEXT("Beta"));
        IDField->SetPropertyValue_InContainer(Row.GetStructMemory(), I == 0 ? TEXT("DuplicateRef1") : TEXT("DuplicateRef2"));
        ReferenceField->SetPropertyValue_InContainer(Row.GetStructMemory(), TEXT("Student1"));
        ExpectedField->SetIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(Row.GetStructMemory()), static_cast<int64>(60 + I));
        DuplicateReferences->AddRow(I == 0 ? FName(TEXT("OldAlpha")) : FName(TEXT("OldBeta")), *reinterpret_cast<FTableRowBase*>(Row.GetStructMemory()));
    }
    UProfessorGradeSave* LegacyDuplicateSave = NewObject<UProfessorGradeSave>();
    for (int32 I = 0; I < 2; ++I)
    {
        FProfessorStudent Legacy = Updated;
        Legacy.ReferenceNumber.Empty();
        Legacy.RowName = I == 0 ? FName(TEXT("OldAlpha")) : FName(TEXT("OldBeta"));
        Legacy.StudentName = I == 0 ? TEXT("Renamed Alpha") : TEXT("Renamed Beta");
        Legacy.StudentID = I == 0 ? TEXT("DuplicateRef1") : TEXT("DuplicateRef2");
        LegacyDuplicateSave->Students.Add(Legacy);
    }
    TestTrue(TEXT("Legacy saves migrate after duplicate authored references are repaired"), UGradingStore::ApplySavedRecordsToTable(DuplicateReferences, LegacyDuplicateSave, Error));
    if (DuplicateReferences->GetRowMap().Contains(TEXT("RenamedAlpha")) && DuplicateReferences->GetRowMap().Contains(TEXT("RenamedBeta")))
    {
        uint8* Alpha = DuplicateReferences->GetRowMap().FindChecked(TEXT("RenamedAlpha"));
        uint8* Beta = DuplicateReferences->GetRowMap().FindChecked(TEXT("RenamedBeta"));
        TestTrue(TEXT("Repaired identities stay distinct"), ReferenceField->GetPropertyValue_InContainer(Alpha) != ReferenceField->GetPropertyValue_InContainer(Beta));
        TestEqual(TEXT("First repaired identity keeps own expected mark"), ExpectedField->GetSignedIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(Alpha)), static_cast<int64>(60));
        TestEqual(TEXT("Second repaired identity keeps own expected mark"), ExpectedField->GetSignedIntPropertyValue(ExpectedField->ContainerPtrToValuePtr<void>(Beta)), static_cast<int64>(61));
    }
    return true;
}
#endif
