#include "GradingStore.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingOCAFloatCoreTest, "GradingRuntime.Stage44.DecimalOCA",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingOCAFloatCoreTest::RunTest(const FString& Parameters)
{
    struct FInput { const TCHAR* Text; bool bValid; float Value; };
    const FInput Inputs[] = {
        {TEXT("76.8"), true, 76.8f}, {TEXT("70.45"), true, 70.45f},
        {TEXT(" 60.4 "), true, 60.4f}, {TEXT("0"), true, 0.f},
        {TEXT("100.0"), true, 100.f}, {TEXT(".5"), true, .5f},
        {TEXT(""), false, 0.f}, {TEXT("."), false, 0.f}, {TEXT("1.2.3"), false, 0.f},
        {TEXT("-1"), false, 0.f}, {TEXT("100.01"), false, 0.f},
        {TEXT("76x"), false, 0.f}, {TEXT("NaN"), false, 0.f},
        {TEXT("inf"), false, 0.f}, {TEXT("1e2"), false, 0.f}, {TEXT("1,5"), false, 0.f}
    };
    for (const FInput& Input : Inputs)
    {
        float Value = -99.f;
        const FString Label = FString::Printf(TEXT("Input [%s]"), Input.Text);
        TestEqual(Label + TEXT(" validity"), UGradingStore::TryParseOCA(Input.Text, Value), Input.bValid);
        TestEqual(Label + TEXT(" value"), Value, Input.Value);
    }
    struct FGradeCase { float OCA; int32 OEA; const TCHAR* Grade; float Total; };
    const FGradeCase Cases[] = {
        {49.9f, 100, TEXT("F"), 74.95f}, {59.9f, 60, TEXT("F"), 59.95f},
        {60.f, 60, TEXT("D"), 60.f}, {69.9f, 70, TEXT("D"), 69.95f},
        {79.9f, 80, TEXT("C"), 79.95f}, {89.9f, 90, TEXT("B"), 89.95f},
        {90.f, 90, TEXT("A"), 90.f}, {76.8f, 80, TEXT("C"), 78.4f}
    };
    for (const FGradeCase& Case : Cases)
    {
        const FProfessorGrade Result = UGradingStore::CalculateGrade(Case.OCA, Case.OEA, true);
        const FString Label = FString::Printf(TEXT("OCA %.4f OEA %d"), Case.OCA, Case.OEA);
        TestEqual(Label + TEXT(" unrounded grade"), Result.Grade, FString(Case.Grade));
        TestTrue(Label + TEXT(" fractional total"), FMath::IsNearlyEqual(Result.Total, Case.Total, .0001f));
    }
    TestFalse(TEXT("Missing OEA is ungraded"), UGradingStore::CalculateGrade(76.8f, 0, false).bGraded);
    TestEqual(TEXT("Entered zero OEA is F"), UGradingStore::CalculateGrade(76.8f, 0, true).Grade, FString(TEXT("F")));

    UProfessorGradeSave* Saved = NewObject<UProfessorGradeSave>();
    Saved->FormatVersion = 2;
    FProfessorStudent Student;
    Student.RowName = TEXT("Student1");
    Student.ReferenceNumber = TEXT("Student1");
    Student.StudentName = TEXT("Decimal Fixture");
    Student.StudentID = TEXT("Test_1");
    Student.OCA = 76.8f;
    Student.OEA = 0;
    Student.bHasOEA = false;
    Student.ExpectedScore = 60;
    Student.bHasExpectedScore = true;
    Saved->Students.Add(Student);
    Saved->NextReferenceNumber = 2;
    FString Error;
    TestTrue(TEXT("Version 2 accepts fractional OCA"), UGradingStore::ValidateSave(Saved, Error));
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Float save serializes in memory"), UGameplayStatics::SaveGameToMemory(Saved, Bytes))) return false;
    UProfessorGradeSave* Loaded = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Float save reloads in memory"), Loaded)) return false;
    if (!TestEqual(TEXT("Student count survives"), Loaded->Students.Num(), 1)) return false;
    TestEqual(TEXT("Format 2 survives"), Loaded->FormatVersion, 2);
    TestEqual(TEXT("OCA survives exactly"), Loaded->Students[0].OCA, Student.OCA);
    TestFalse(TEXT("Absent OEA survives"), Loaded->Students[0].bHasOEA);
    Saved->FormatVersion = 1;
    Saved->Students[0].OCA = 62.f;
    TestTrue(TEXT("Legacy version 1 is accepted"), UGradingStore::ValidateSave(Saved, Error));
    Saved->FormatVersion = 99;
    TestFalse(TEXT("Unknown save version is rejected"), UGradingStore::ValidateSave(Saved, Error));
    Saved->FormatVersion = 2;
    Saved->Students[0].OCA = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN is rejected"), UGradingStore::ValidateSave(Saved, Error));
    TestFalse(TEXT("NaN cannot produce a grade"), UGradingStore::CalculateGrade(Saved->Students[0].OCA, 80, true).bGraded);
    Saved->Students[0].OCA = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Infinity is rejected"), UGradingStore::ValidateSave(Saved, Error));
    Student.bHasOEA = true;
    Student.OEA = 75;
    TestFalse(TEXT("OEA difference 15 remains allowed"), UGradingStore::IsSuspicious(Student));
    Student.OEA = 76;
    TestTrue(TEXT("OEA difference 16 remains suspicious"), UGradingStore::IsSuspicious(Student));
    Student.OCA = 1.25f;
    TestTrue(TEXT("Changing OCA does not change the OEA rule"), UGradingStore::IsSuspicious(Student));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingLegacyOCATagTest, "GradingRuntime.Stage44.LegacyIntegerTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingLegacyOCATagTest::RunTest(const FString& Parameters)
{
    // Produce an actual legacy IntProperty tag and read it through UE's tagged-struct loader.
    // This specifically exercises int32 -> float conversion, not a new float save relabeled v1.
    for (int32 LegacyOCA : {0, 62, 100})
    {
        TArray<uint8> Bytes;
        {
            FMemoryWriter Memory(Bytes, true);
            FObjectAndNameAsStringProxyArchive Writer(Memory, false);
            // UE4.27's object proxy hides the inherited FName operator overload.
            // Use the base interface; virtual dispatch still writes FNames as strings.
            FArchive& Archive = Writer;
            // FPropertyTag's write operator is not exported by UE4.27's installed DLL.
            // Emit its binary IntProperty layout (PropertyTag.cpp), then exercise the real loader.
            FName PropertyName = GET_MEMBER_NAME_CHECKED(FProfessorStudent, OCA);
            FName PropertyType = NAME_IntProperty;
            int32 PropertySize = sizeof(int32);
            int32 ArrayIndex = 0;
            uint8 HasPropertyGuid = 0;
            Archive << PropertyName;
            Archive << PropertyType;
            Archive << PropertySize;
            Archive << ArrayIndex;
            if (Archive.UE4Ver() >= VER_UE4_PROPERTY_GUID_IN_PROPERTY_TAG)
                Archive << HasPropertyGuid;
            Archive << LegacyOCA;
            FName End = NAME_None;
            Archive << End;
        }
        FProfessorStudent Restored;
        Restored.OCA = -1.f;
        FMemoryReader Memory(Bytes, true);
        FObjectAndNameAsStringProxyArchive Reader(Memory, false);
        FProfessorStudent::StaticStruct()->SerializeTaggedProperties(Reader, reinterpret_cast<uint8*>(&Restored), nullptr, nullptr);
        TestFalse(TEXT("Legacy tagged read has no archive error"), Reader.IsError());
        TestEqual(FString::Printf(TEXT("Legacy integer %d becomes the same float"), LegacyOCA), Restored.OCA, static_cast<float>(LegacyOCA));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingOCATablePrecisionTest, "GradingRuntime.Stage44.TablePrecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingOCATablePrecisionTest::RunTest(const FString& Parameters)
{
    UDataTable* Authored = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    if (!TestNotNull(TEXT("Authored student table is available"), Authored)) return false;
    UDataTable* Table = DuplicateObject<UDataTable>(Authored, GetTransientPackage());
    if (!TestTrue(TEXT("Table has student rows"), Table->GetRowMap().Num() > 0)) return false;
    const FString Before = Table->GetTableAsJSON();
    auto Field = [Table](const TCHAR* Name) -> FProperty*
    {
        const FString Prefix = FString(Name) + TEXT("_");
        for (TFieldIterator<FProperty> It(Table->GetRowStruct()); It; ++It)
            if (It->GetName() == Name || It->GetName().StartsWith(Prefix)) return *It;
        return nullptr;
    };
    FNumericProperty* OCA = CastField<FNumericProperty>(Field(TEXT("CA_Score")));
    if (!TestNotNull(TEXT("CA_Score is numeric"), OCA)) return false;
    UProfessorGradeSave* Saved = NewObject<UProfessorGradeSave>();
    Saved->FormatVersion = 2;
    const auto First = Table->GetRowMap().CreateConstIterator();
    FProfessorStudent Student;
    Student.RowName = First.Key();
    Student.StudentName = TEXT("Precision Fixture");
    Student.StudentID = TEXT("Precision_Test_1");
    if (FStrProperty* Ref = CastField<FStrProperty>(Field(TEXT("Reference_Number"))))
        Student.ReferenceNumber = Ref->GetPropertyValue_InContainer(First.Value());
    Student.OCA = 76.8f;
    Saved->Students.Add(Student);
    FString Error;
    const bool bApplied = UGradingStore::ApplySavedRecordsToTable(Table, Saved, Error);
    if (OCA->IsInteger())
    {
        TestFalse(TEXT("Decimal write into old Integer table is refused"), bApplied);
        TestTrue(TEXT("Error explains the required Float schema"), Error.Contains(TEXT("Float")));
        TestEqual(TEXT("Refused write leaves all rows intact"), Table->GetTableAsJSON(), Before);
    }
    else
    {
        if (!TestTrue(TEXT("Decimal write into Float table succeeds: ") + Error, bApplied)) return false;
        // Normalization may rename a row to its stable reference. Locate the saved
        // fixture by student ID instead of assuming the pre-migration row key survives.
        const uint8* Row = nullptr;
        int32 Matches = 0;
        if (FStrProperty* ID = CastField<FStrProperty>(Field(TEXT("StudentID"))))
            for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
                if (ID->GetPropertyValue_InContainer(Pair.Value) == Student.StudentID)
                {
                    Row = Pair.Value;
                    ++Matches;
                }
        TestEqual(TEXT("Saved student identity is unique"), Matches, 1);
        if (!TestNotNull(TEXT("Saved student is still present"), Row)) return false;
        TestEqual(TEXT("Table preserves fractional OCA"), static_cast<float>(OCA->GetFloatingPointPropertyValue(OCA->ContainerPtrToValuePtr<void>(Row))), 76.8f);
    }
    TestEqual(TEXT("Authored table is untouched"), Authored->GetTableAsJSON(), Before);
    return true;
}
#endif
