#include "GradingStore.h"

#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "DataTableUtils.h"

namespace
{


    // User-defined struct fields have a generated suffix; match the authored prefix.
    FProperty* FindField(const UStruct* Struct, const FString& Name)
    {
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
        {
            const FString FieldName = It->GetName();
            if (FieldName == Name || FieldName.StartsWith(Name + TEXT("_")))
            {
                return *It;
            }
        }
        return nullptr;
    }

    FString ReadString(const UStruct* Struct, const uint8* Row, const FString& Name)
    {
        if (FStrProperty* Property = CastField<FStrProperty>(FindField(Struct, Name)))
        {
            return Property->GetPropertyValue_InContainer(Row);
        }
        return FString();
    }

    int32 ReadNumber(const UStruct* Struct, const uint8* Row, const FString& Name, int32 Fallback = 0)
    {
        if (FNumericProperty* Property = CastField<FNumericProperty>(FindField(Struct, Name)))
        {
            const void* Value = Property->ContainerPtrToValuePtr<void>(Row);
            return Property->IsInteger() ? static_cast<int32>(Property->GetSignedIntPropertyValue(Value))
                : static_cast<int32>(Property->GetFloatingPointPropertyValue(Value));
        }
        return Fallback;
    }

    float ReadOCA(const UStruct* Struct, const uint8* Row)
    {
        if (FNumericProperty* Property = CastField<FNumericProperty>(FindField(Struct, TEXT("CA_Score"))))
        {
            const void* Value = Property->ContainerPtrToValuePtr<void>(Row);
            return Property->IsInteger() ? static_cast<float>(Property->GetSignedIntPropertyValue(Value))
                : static_cast<float>(Property->GetFloatingPointPropertyValue(Value));
        }
        return 0.f;
    }

    bool CanStoreOCA(const UDataTable* Table, const TArray<FProfessorStudent>& Students, FString& Error)
    {
        const UScriptStruct* Struct = Table ? Table->GetRowStruct() : nullptr;
        const FProperty* Property = Struct ? FindField(Struct, TEXT("CA_Score")) : nullptr;
        if (CastField<FFloatProperty>(Property)) return true;
        if (CastField<FIntProperty>(Property))
        {
            for (const FProfessorStudent& Student : Students)
            {
                // Never silently truncate a decimal while the authored table still uses int32.
                if (!FMath::IsFinite(Student.OCA) || Student.OCA != FMath::TruncToFloat(Student.OCA))
                {
                    Error = TEXT("CA_Score is still Integer. Change StudentData.CA_Score to Float before saving decimal OCA.");
                    return false;
                }
            }
            return true;
        }
        Error = TEXT("Student DataTable CA_Score must be Integer or Float.");
        return false;
    }

    void WriteOCA(const UStruct* Struct, uint8* Row, float Value)
    {
        // ApplySavedRecordsToTable preflights CanStoreOCA before any row mutation.
        FProperty* Property = FindField(Struct, TEXT("CA_Score"));
        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
            FloatProperty->SetPropertyValue_InContainer(Row, Value);
        else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
            IntProperty->SetPropertyValue_InContainer(Row, static_cast<int32>(Value));
    }

    bool ReadBool(const UStruct* Struct, const uint8* Row, const FString& Name, bool Fallback)
    {
        if (FBoolProperty* Property = CastField<FBoolProperty>(FindField(Struct, Name)))
        {
            return Property->GetPropertyValue_InContainer(Row);
        }
        return Fallback;
    }

    void WriteString(const UStruct* Struct, uint8* Row, const FString& Name, const FString& Value)
    {
        if (FStrProperty* Property = CastField<FStrProperty>(FindField(Struct, Name)))
        {
            Property->SetPropertyValue_InContainer(Row, Value);
        }
    }

    void WriteNumber(const UStruct* Struct, uint8* Row, const FString& Name, int32 Value)
    {
        if (FNumericProperty* Property = CastField<FNumericProperty>(FindField(Struct, Name)))
        {
            void* Destination = Property->ContainerPtrToValuePtr<void>(Row);
            if (Property->IsInteger()) Property->SetIntPropertyValue(Destination, static_cast<int64>(Value));
            else Property->SetFloatingPointPropertyValue(Destination, static_cast<double>(Value));
        }
    }

    void WriteFloat(const UStruct* Struct, uint8* Row, const FString& Name, double Value)
    {
        if (FNumericProperty* Property = CastField<FNumericProperty>(FindField(Struct, Name)))
        {
            if (Property->IsFloatingPoint())
                Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Row), Value);
        }
    }

    int32 ReferenceIndex(const FString& Ref)
    {
        if (!Ref.StartsWith(TEXT("Student"))) return 0;
        const FString Suffix = Ref.Mid(7);
        if (Suffix.IsEmpty()) return 0;
        for (TCHAR C : Suffix) if (!FChar::IsDigit(C)) return 0;
        return static_cast<int32>(FMath::Clamp<int64>(FCString::Atoi64(*Suffix), 0, MAX_int32 - 1));
    }

    bool IsLegacyGeneratedReference(const FString& Ref)
    {
        FGuid Guid;
        return Ref.StartsWith(TEXT("Professor_")) && FGuid::Parse(Ref.Mid(10), Guid);
    }

    int32 ReadSequence(const UDataTable* Table)
    {
        int32 Next = 1;
        if (!Table || !Table->GetRowStruct()) return Next;
        for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
            Next = FMath::Max(Next, ReferenceIndex(ReadString(Table->GetRowStruct(), Pair.Value, TEXT("Reference_Number"))) + 1);
#if WITH_EDITOR
        Next = FMath::Max(Next, FCString::Atoi(*Table->GetOutermost()->GetMetaData()->GetValue(Table, TEXT("GradingNextReference"))));
#endif
        return Next;
    }

    void StoreSequence(UDataTable* Table, int32 Next)
    {
#if WITH_EDITOR
        Table->GetOutermost()->GetMetaData()->SetValue(Table, TEXT("GradingNextReference"), *FString::FromInt(Next));
#endif
    }

    FString NameKey(const FString& Name, FName Fallback = NAME_None)
    {
        FString Result;
        for (TCHAR C : Name) if (!FChar::IsWhitespace(C)) Result.AppendChar(C);
        if (Result.IsEmpty()) Result = Fallback.IsNone() ? TEXT("Student") : Fallback.ToString();
        return DataTableUtils::MakeValidName(Result).ToString();
    }

    void AssignRowNames(TArray<FProfessorStudent>& Students)
    {
        TArray<int32> Order;
        for (int32 I = 0; I < Students.Num(); ++I) Order.Add(I);
        Order.Sort([&Students](int32 A, int32 B)
        {
            const int32 IA = ReferenceIndex(Students[A].ReferenceNumber);
            const int32 IB = ReferenceIndex(Students[B].ReferenceNumber);
            if (IA != IB) return (IA ? IA : MAX_int32) < (IB ? IB : MAX_int32);
            const int32 RefOrder = Students[A].ReferenceNumber.Compare(Students[B].ReferenceNumber);
            if (RefOrder != 0) return RefOrder < 0;
            return Students[A].RowName.LexicalLess(Students[B].RowName);
        });
        TSet<FName> Used;
        for (int32 I : Order)
        {
            const FString Base = NameKey(Students[I].StudentName, Students[I].RowName);
            FName Key(*Base);
            int32 Suffix = 2;
            while (Key.IsNone() || Used.Contains(Key)) Key = FName(*(Base + TEXT("_") + FString::FromInt(Suffix++)));
            Students[I].RowName = Key;
            Used.Add(Key);
        }
    }

    TArray<FProfessorStudent> ReadStudents(const UDataTable* Table)
    {
        TArray<FProfessorStudent> Result;
        if (!Table || !Table->GetRowStruct()) return Result;
        const UScriptStruct* Struct = Table->GetRowStruct();
        for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
        {
            FProfessorStudent S;
            S.RowName = Pair.Key;
            S.StudentName = ReadString(Struct, Pair.Value, TEXT("StudentName"));
            S.StudentID = ReadString(Struct, Pair.Value, TEXT("StudentID"));
            S.ReferenceNumber = ReadString(Struct, Pair.Value, TEXT("Reference_Number"));
            S.OCA = ReadOCA(Struct, Pair.Value);
            S.OEA = ReadNumber(Struct, Pair.Value, TEXT("ExamScore"));
            S.bHasOEA = ReadBool(Struct, Pair.Value, TEXT("HasOEA"), true);
            S.ExpectedScore = ReadNumber(Struct, Pair.Value, TEXT("ExpectedScore"), -1);
            S.bHasExpectedScore = S.ExpectedScore >= 0 && S.ExpectedScore <= 100;
            S.PaperReference = S.ReferenceNumber;
            Result.Add(S);
        }
        return Result;
    }

    void CopyRows(UDataTable* Destination, const UDataTable* Source)
    {
        Destination->EmptyTable();
        for (const TPair<FName, uint8*>& Pair : Source->GetRowMap())
            Destination->AddRow(Pair.Key, *reinterpret_cast<const FTableRowBase*>(Pair.Value));
        StoreSequence(Destination, ReadSequence(Source));
        Destination->HandleDataTableChanged();
    }
}

bool UGradingStore::NormalizeStudentTable(UDataTable* Table, bool& bChanged, FString& Error)
{
    bChanged = false;
    if (!Table || !Table->GetRowStruct()) { Error = TEXT("Student DataTable is unavailable."); return false; }
    const UScriptStruct* Struct = Table->GetRowStruct();
    if (!CastField<FStrProperty>(FindField(Struct, TEXT("StudentName"))) || !CastField<FStrProperty>(FindField(Struct, TEXT("Reference_Number"))))
    { Error = TEXT("Student table needs StudentName and Reference_Number fields."); return false; }
    TArray<FProfessorStudent> Rows = ReadStudents(Table);
    Rows.Sort([](const FProfessorStudent& A, const FProfessorStudent& B)
    {
        const bool ACanonical = A.RowName == FName(*NameKey(A.StudentName));
        const bool BCanonical = B.RowName == FName(*NameKey(B.StudentName));
        if (ACanonical != BCanonical) return ACanonical;
        return A.RowName.LexicalLess(B.RowName);
    });
    TMap<FString, TSharedPtr<FStructOnScope>> Payloads;
    TSet<FString> UsedReferences;
    int32 Next = ReadSequence(Table);
    for (FProfessorStudent& Student : Rows)
    {
        TSharedPtr<FStructOnScope> Payload = MakeShared<FStructOnScope>(Struct);
        Struct->CopyScriptStruct(Payload->GetStructMemory(), Table->GetRowMap().FindChecked(Student.RowName));
        if (Student.ReferenceNumber.IsEmpty() || IsLegacyGeneratedReference(Student.ReferenceNumber) || UsedReferences.Contains(Student.ReferenceNumber.ToLower()))
        {
            if (Next >= MAX_int32) { Error = TEXT("Student reference sequence is exhausted."); return false; }
            Student.ReferenceNumber = TEXT("Student") + FString::FromInt(Next++);
            WriteString(Struct, Payload->GetStructMemory(), TEXT("Reference_Number"), Student.ReferenceNumber);
            bChanged = true;
        }
        UsedReferences.Add(Student.ReferenceNumber.ToLower());
        Payloads.Add(Student.ReferenceNumber, Payload);
    }
    const TArray<FProfessorStudent> Before = Rows;
    AssignRowNames(Rows);
    for (int32 I = 0; I < Rows.Num(); ++I) if (Rows[I].RowName != Before[I].RowName) bChanged = true;
#if WITH_EDITOR
    if (Table->GetOutermost()->GetMetaData()->GetValue(Table, TEXT("GradingNextReference")) != FString::FromInt(Next)) bChanged = true;
#endif
    if (bChanged)
    {
        Table->Modify(false);
        Table->EmptyTable();
        for (const FProfessorStudent& Student : Rows)
            Table->AddRow(Student.RowName, *reinterpret_cast<const FTableRowBase*>(Payloads.FindChecked(Student.ReferenceNumber)->GetStructMemory()));
        StoreSequence(Table, Next);
        Table->HandleDataTableChanged();
    }
    else StoreSequence(Table, Next);
    Error.Empty();
    return true;
}

void UGradingStore::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    InitializeRecords(LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students")));
}

void UGradingStore::Deinitialize()
{
    SourceTable = nullptr;
    InitialTable = nullptr;
    Records.Empty();
    bInitialized = false;
    bLoggedIn = false;
    Super::Deinitialize();
}

bool UGradingStore::Login(const FString& Account, const FString& Password)
{
    bLoggedIn = false;
    const FString EnteredAccount = Account.TrimStartAndEnd();
    if (EnteredAccount.IsEmpty() || Password.IsEmpty()) return false;
    UDataTable* Accounts = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_ProfessorAccounts.DT_ProfessorAccounts"));
    if (!Accounts || Accounts->GetRowStruct() != FProfessorAccountRow::StaticStruct()) return false;
    for (const TPair<FName, uint8*>& Pair : Accounts->GetRowMap())
    {
        const FProfessorAccountRow* Row = reinterpret_cast<const FProfessorAccountRow*>(Pair.Value);
        if (Row && !Row->Account.IsEmpty() && !Row->Password.IsEmpty()
            && EnteredAccount == Row->Account && Password == Row->Password)
        {
            bLoggedIn = true;
            break;
        }
    }
    return bLoggedIn;
}

void UGradingStore::Logout()
{
    bLoggedIn = false;
}

bool UGradingStore::TryParseOCA(const FString& Raw, float& OutOCA)
{
    OutOCA = 0.f;
    const FString Value = Raw.TrimStartAndEnd();
    bool bDot = false;
    bool bDigit = false;
    for (TCHAR Character : Value)
    {
        if (Character >= TEXT('0') && Character <= TEXT('9')) bDigit = true;
        else if (Character == TEXT('.') && !bDot) bDot = true;
        else return false;
    }
    if (!bDigit) return false;
    const double Number = FCString::Atod(*Value);
    if (!FMath::IsFinite(Number) || Number < 0.0 || Number > 100.0) return false;
    OutOCA = static_cast<float>(Number);
    return true;
}

FProfessorGrade UGradingStore::CalculateGrade(float OCA, int32 OEA, bool bHasOEA)
{
    FProfessorGrade Result;
    if (!bHasOEA || !FMath::IsFinite(OCA) || OCA < 0.f || OCA > 100.f || OEA < 0 || OEA > 100) return Result;
    Result.bGraded = true;
    Result.Total = .5f * OCA + .5f * OEA;
    if (OCA < 50 || OEA < 50 || Result.Total < 60.f)
    {
        Result.Grade = TEXT("F");
        return Result;
    }
    if (Result.Total >= 90.f) { Result.Grade = TEXT("A"); Result.GPA = 4.f; }
    else if (Result.Total >= 80.f) { Result.Grade = TEXT("B"); Result.GPA = 3.f; }
    else if (Result.Total >= 70.f) { Result.Grade = TEXT("C"); Result.GPA = 2.f; }
    else { Result.Grade = TEXT("D"); Result.GPA = 1.f; }
    return Result;
}

bool UGradingStore::IsSuspicious(const FProfessorStudent& Student)
{
    return Student.bHasOEA && Student.bHasExpectedScore && FMath::Abs(Student.OEA - Student.ExpectedScore) > 15;
}

bool UGradingStore::ValidateRecords(const TArray<FProfessorStudent>& Candidate, FString& Error)
{
    TSet<FString> IDs;
    TSet<FString> References;
    TSet<FName> RowNames;
    for (const FProfessorStudent& Student : Candidate)
    {
        if (Student.StudentName.TrimStartAndEnd().IsEmpty() || Student.StudentID.TrimStartAndEnd().IsEmpty())
        {
            Error = TEXT("Every student needs a name and student ID.");
            return false;
        }
        const FString ID = Student.StudentID.TrimStartAndEnd().ToLower();
        if (IDs.Contains(ID))
        {
            Error = TEXT("Student IDs must be unique: ") + Student.StudentID;
            return false;
        }
        IDs.Add(ID);
        if (!Student.ReferenceNumber.IsEmpty())
        {
            const FString Ref = Student.ReferenceNumber.ToLower();
            if (References.Contains(Ref)) { Error = TEXT("Student references must be unique."); return false; }
            References.Add(Ref);
        }
        if (!Student.RowName.IsNone())
        {
            if (RowNames.Contains(Student.RowName))
            {
                Error = TEXT("Duplicate student record identity.");
                return false;
            }
            RowNames.Add(Student.RowName);
        }
        if (!FMath::IsFinite(Student.OCA) || Student.OCA < 0.f || Student.OCA > 100.f || (Student.bHasOEA && (Student.OEA < 0 || Student.OEA > 100)))
        {
            Error = TEXT("OCA must be a finite number from 0 to 100; entered OEA must be a whole number from 0 to 100.");
            return false;
        }
        if (Student.bHasExpectedScore && (Student.ExpectedScore < 0 || Student.ExpectedScore > 100))
        {
            Error = TEXT("The expected score must be between 0 and 100.");
            return false;
        }
    }
    Error.Empty();
    return true;
}

bool UGradingStore::ValidateSave(const UProfessorGradeSave* Saved, FString& Error)
{
    if (!Saved || (Saved->FormatVersion != 1 && Saved->FormatVersion != 2))
    {
        Error = TEXT("Unsupported or unreadable grade save.");
        return false;
    }
    if (!ValidateRecords(Saved->Students, Error)) return false;
    TSet<FName> PresentRows;
    for (const FProfessorStudent& Student : Saved->Students)
    {
        if (Student.RowName.IsNone()) { Error = TEXT("A saved student has no stable record identity."); return false; }
        PresentRows.Add(Student.RowName);
    }
    TSet<FName> RemovedRows;
    for (FName Name : Saved->DeletedRowNames)
    {
        if (Name.IsNone() || RemovedRows.Contains(Name) || PresentRows.Contains(Name))
        {
            Error = TEXT("The save contains inconsistent deleted student identities.");
            return false;
        }
        RemovedRows.Add(Name);
    }
    TSet<FString> RemovedReferences;
    for (const FString& Ref : Saved->DeletedReferenceNumbers)
    {
        if (Ref.IsEmpty() || RemovedReferences.Contains(Ref.ToLower()) || Saved->Students.ContainsByPredicate([&Ref](const FProfessorStudent& S) { return S.ReferenceNumber.Equals(Ref, ESearchCase::IgnoreCase); }))
        { Error = TEXT("The save contains inconsistent deleted student references."); return false; }
        RemovedReferences.Add(Ref.ToLower());
    }
    Error.Empty();
    return true;
}

void UGradingStore::RestoreAuthoritativeScores(TArray<FProfessorStudent>& Candidate) const
{
    for (FProfessorStudent& Student : Candidate)
    {
        const FProfessorStudent* Original = nullptr;
        for (const TPair<FName, FProfessorStudent>& Pair : AuthoritativeRows)
            if (!Student.ReferenceNumber.IsEmpty() && Student.ReferenceNumber == Pair.Value.ReferenceNumber) { Original = &Pair.Value; break; }
        Student.bHasExpectedScore = Original && Original->bHasExpectedScore;
        Student.ExpectedScore = Original ? Original->ExpectedScore : 0;
    }
}

void UGradingStore::InitializeRecords(UDataTable* InTable)
{
    if (bInitialized) return;
    InitialTable = InTable;
    StartNewSession(LastError);
}

bool UGradingStore::StartNewSession(FString& Error)
{
    if (!InitialTable || !InitialTable->GetRowStruct()) { Error = TEXT("Initial student data is unavailable."); return false; }
    UDataTable* Baseline = DuplicateObject<UDataTable>(InitialTable, GetTransientPackage());
    Baseline->SetFlags(RF_Transient);
    Baseline->ClearFlags(RF_Public | RF_Standalone);
    StoreSequence(Baseline, ReadSequence(InitialTable));
    bool Changed = false;
    if (!NormalizeStudentTable(Baseline, Changed, Error)) return false;
    TArray<FProfessorStudent> Initial = ReadStudents(Baseline);
    const UScriptStruct* Struct = Baseline->GetRowStruct();
    FArrayProperty* Labs = CastField<FArrayProperty>(FindField(Struct, TEXT("LabLateDays")));
    FIntProperty* LateDay = Labs ? CastField<FIntProperty>(Labs->Inner) : nullptr;
    if (!Labs || !LateDay) { Error = TEXT("Initial lab details are missing."); return false; }
    for (FProfessorStudent& Student : Initial)
    {
        const uint8* Row = Baseline->FindRowUnchecked(Student.RowName);
        FScriptArrayHelper Values(Labs, Labs->ContainerPtrToValuePtr<void>(Row));
        if (Values.Num() != 10) { Error = TEXT("Each initial student needs ten labs: ") + Student.ReferenceNumber; return false; }
        float LabTotal = 0.f;
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            const int32 Days = LateDay->GetPropertyValue(Values.GetRawPtr(I));
            if (Days < 0) { Error = TEXT("Initial lab lateness cannot be negative."); return false; }
            LabTotal += Days == 0 ? 1.f : (Days <= 5 ? (6 - Days) * .1f : 0.f);
        }
        float Scores[3]; const TCHAR* Names[] = {TEXT("Assignment1Score"), TEXT("Assignment2Score"), TEXT("MidtermScore")};
        for (int32 I = 0; I < 3; ++I)
        {
            FNumericProperty* P = CastField<FNumericProperty>(FindField(Struct, Names[I]));
            if (!P) { Error = TEXT("Initial assessment score is missing."); return false; }
            const void* V = P->ContainerPtrToValuePtr<void>(Row);
            Scores[I] = P->IsInteger() ? static_cast<float>(P->GetSignedIntPropertyValue(V)) : static_cast<float>(P->GetFloatingPointPropertyValue(V));
            if (!FMath::IsFinite(Scores[I]) || Scores[I] < 0.f || Scores[I] > 100.f) { Error = TEXT("Invalid initial assessment score."); return false; }
        }
        Student.OCA = 2.f * (LabTotal + Scores[0] * .1f + Scores[1] * .1f + Scores[2] * .2f);
    }
    const int32 Next = ReadSequence(Baseline);
    if (!ValidateRecords(Initial, Error)) return false;
    UDataTable* Working = nullptr;
    if (!BuildSessionTable(Initial, Next, Working, Error)) return false;
    SourceTable = Working;
    Records = MoveTemp(Initial);
    NextReferenceNumber = Next;
    AuthoritativeRows.Empty();
    for (const auto& Student : Records) AuthoritativeRows.Add(Student.RowName, Student);
    DeletedRowNames.Empty(); DeletedReferenceNumbers.Empty();
    bInitialized = true; bInvalidSave = false; bLoggedIn = false;
    LastError.Empty(); Error.Empty();
    UE_LOG(LogTemp, Display, TEXT("STAGE4 NEW SESSION students=%d next=%d"), Records.Num(), NextReferenceNumber);
    return true;
}

bool UGradingStore::BuildSessionTable(const TArray<FProfessorStudent>& Students, int32 Next, UDataTable*& Result, FString& Error) const
{
    Result = nullptr;
    if (!InitialTable || !InitialTable->GetRowStruct()) { Error = TEXT("Initial student schema is unavailable."); return false; }
    UDataTable* Working = DuplicateObject<UDataTable>(InitialTable, GetTransientPackage());
    Working->SetFlags(RF_Transient); Working->ClearFlags(RF_Public | RF_Standalone);
    Working->EmptyTable();
    const UScriptStruct* Struct = InitialTable->GetRowStruct();
    if (!CanStoreOCA(Working, Students, Error)) return false;
    for (const FProfessorStudent& Student : Students)
    {
        FStructOnScope Payload(Struct);
        if (!Student.PaperReference.IsEmpty())
        {
            const uint8* Template = nullptr;
            for (const auto& Pair : InitialTable->GetRowMap())
                if (ReadString(Struct, Pair.Value, TEXT("Reference_Number")) == Student.PaperReference) { Template = Pair.Value; break; }
            if (!Template) { Error = TEXT("The saved paper reference is unavailable: ") + Student.PaperReference; return false; }
            Struct->CopyScriptStruct(Payload.GetStructMemory(), Template);
        }
        uint8* Row = Payload.GetStructMemory();
        WriteString(Struct, Row, TEXT("StudentName"), Student.StudentName);
        WriteString(Struct, Row, TEXT("StudentID"), Student.StudentID);
        WriteString(Struct, Row, TEXT("Reference_Number"), Student.ReferenceNumber);
        WriteOCA(Struct, Row, Student.OCA);
        WriteNumber(Struct, Row, TEXT("ExamScore"), Student.OEA);
        WriteNumber(Struct, Row, TEXT("ExpectedScore"), Student.bHasExpectedScore ? Student.ExpectedScore : -1);
        const FProfessorGrade Grade = CalculateGrade(Student.OCA, Student.OEA, Student.bHasOEA);
        WriteString(Struct, Row, TEXT("Grade"), Grade.Grade);
        WriteFloat(Struct, Row, TEXT("GPA"), Grade.GPA);
        WriteFloat(Struct, Row, TEXT("WeightedTotal"), Grade.Total);
        if (auto* Has = CastField<FBoolProperty>(FindField(Struct, TEXT("HasOEA")))) Has->SetPropertyValue_InContainer(Row, Student.bHasOEA);
        if (auto* Suspicious = CastField<FBoolProperty>(FindField(Struct, TEXT("IsMarkingSuspicious")))) Suspicious->SetPropertyValue_InContainer(Row, IsSuspicious(Student));
        Working->AddRow(Student.RowName, *reinterpret_cast<const FTableRowBase*>(Row));
    }
    StoreSequence(Working, Next);
    Result = Working; Error.Empty(); return true;
}

FStudentSessionSnapshot UGradingStore::CaptureStudentSnapshot() const
{
    FStudentSessionSnapshot Snapshot;
    if (!bInitialized) return Snapshot;
    Snapshot.FormatVersion = 1; Snapshot.bHasStudentSnapshot = true;
    Snapshot.Students = Records; Snapshot.NextStudentReferenceNumber = NextReferenceNumber;
    return Snapshot;
}

bool UGradingStore::ValidateStudentSnapshot(const FStudentSessionSnapshot& Snapshot, FString& Error)
{
    if (Snapshot.FormatVersion != 1 || !Snapshot.bHasStudentSnapshot)
    { Error = TEXT("Unsupported or absent student snapshot."); return false; }
    if (!ValidateRecords(Snapshot.Students, Error)) return false;
    if (Snapshot.NextStudentReferenceNumber < 1) { Error = TEXT("Invalid student reference counter."); return false; }
    for (const auto& S : Snapshot.Students)
    {
        const int32 Index = ReferenceIndex(S.ReferenceNumber);
        if (Index < 1 || S.ReferenceNumber != TEXT("Student") + FString::FromInt(Index) || Index >= Snapshot.NextStudentReferenceNumber || S.RowName.IsNone())
        { Error = TEXT("The student snapshot contains an invalid identity or reference counter."); return false; }
        if (!S.bHasOEA && S.OEA != 0) { Error = TEXT("Missing OEA must have a zero payload."); return false; }
    }
    Error.Empty(); return true;
}

bool UGradingStore::ApplyStudentSnapshot(const FStudentSessionSnapshot& Snapshot, FString& Error)
{
    if (!ValidateStudentSnapshot(Snapshot, Error)) return false;
    TArray<FProfessorStudent> Candidate = Snapshot.Students;
    AssignRowNames(Candidate);
    UDataTable* Working = nullptr;
    if (!BuildSessionTable(Candidate, Snapshot.NextStudentReferenceNumber, Working, Error)) return false;
    SourceTable = Working; Records = MoveTemp(Candidate);
    NextReferenceNumber = Snapshot.NextStudentReferenceNumber;
    AuthoritativeRows.Empty();
    for (const auto& Student : Records) AuthoritativeRows.Add(Student.RowName, Student);
    DeletedRowNames.Empty(); DeletedReferenceNumbers.Empty();
    bInitialized = true; bInvalidSave = false; bLoggedIn = false;
    LastError.Empty(); Error.Empty();
    UE_LOG(LogTemp, Display, TEXT("STAGE4 RESTORE students=%d next=%d"), Records.Num(), NextReferenceNumber);
    return true;
}

bool UGradingStore::SaveRecords(const TArray<FProfessorStudent>& NewRecords, FString& Error)
{
    if (!bLoggedIn) { Error = TEXT("Log in before saving grades."); return false; }
    if (!bInitialized) { Error = TEXT("Student data is not ready."); return false; }
    TArray<FProfessorStudent> Candidate = NewRecords;
    int32 CandidateNext = NextReferenceNumber;
    TSet<FString> Seen;
    for (FProfessorStudent& Student : Candidate)
    {
        Student.StudentName.TrimStartAndEndInline(); Student.StudentID.TrimStartAndEndInline();
        if (!Student.bHasOEA) Student.OEA = 0;
        const FProfessorStudent* Existing = Records.FindByPredicate([&Student](const FProfessorStudent& S) { return !Student.ReferenceNumber.IsEmpty() && S.ReferenceNumber == Student.ReferenceNumber; });
        if (Existing)
        {
            if (Seen.Contains(Existing->ReferenceNumber)) { Error = TEXT("Duplicate student reference."); return false; }
            Seen.Add(Existing->ReferenceNumber);
            Student.PaperReference = Existing->PaperReference;
            Student.ExpectedScore = Existing->ExpectedScore; Student.bHasExpectedScore = Existing->bHasExpectedScore;
        }
        else
        {
            if (!Student.ReferenceNumber.IsEmpty()) { Error = TEXT("An edited student no longer exists in this session."); return false; }
            if (CandidateNext >= MAX_int32) { Error = TEXT("Student reference sequence is exhausted."); return false; }
            Student.ReferenceNumber = TEXT("Student") + FString::FromInt(CandidateNext++);
            Student.PaperReference.Empty(); Student.ExpectedScore = 0; Student.bHasExpectedScore = false;
        }
    }
    AssignRowNames(Candidate);
    if (!ValidateRecords(Candidate, Error)) return false;
    UDataTable* Working = nullptr;
    if (!BuildSessionTable(Candidate, CandidateNext, Working, Error)) return false;
    SourceTable = Working; Records = MoveTemp(Candidate); NextReferenceNumber = CandidateNext;
    LastError.Empty(); Error.Empty();
    UE_LOG(LogTemp, Display, TEXT("STAGE4 COMMIT students=%d next=%d"), Records.Num(), NextReferenceNumber);
    return true;
}

void UGradingStore::ApplyToDataTable()
{
    UDataTable* Working = nullptr;
    if (BuildSessionTable(Records, NextReferenceNumber, Working, LastError)) SourceTable = Working;
}

bool UGradingStore::ApplySavedRecordsToTable(UDataTable* Table, const UProfessorGradeSave* Saved, FString& Error)
{
    if (!Table || !Table->GetRowStruct()) { Error = TEXT("Student DataTable is unavailable."); return false; }
    if (!ValidateSave(Saved, Error) || !CanStoreOCA(Table, Saved->Students, Error)) return false;
    const UScriptStruct* Struct = Table->GetRowStruct();
    if (!CastField<FStrProperty>(FindField(Struct, TEXT("StudentName")))
        || !CastField<FStrProperty>(FindField(Struct, TEXT("StudentID")))
        || !CastField<FNumericProperty>(FindField(Struct, TEXT("CA_Score")))
        || !CastField<FNumericProperty>(FindField(Struct, TEXT("ExamScore"))))
    {
        Error = TEXT("Student DataTable has an incompatible row schema.");
        return false;
    }
    // All work happens on a copy: swaps and invalid saves cannot overwrite another student's paper.
    const TArray<FProfessorStudent> LegacyRows = ReadStudents(Table);
    UDataTable* Working = DuplicateObject<UDataTable>(Table, GetTransientPackage());
    int32 ReservedNext = FMath::Max(ReadSequence(Table), Saved->NextReferenceNumber);
    for (const FString& Ref : Saved->DeletedReferenceNumbers) ReservedNext = FMath::Max(ReservedNext, ReferenceIndex(Ref) + 1);
    StoreSequence(Working, ReservedNext);
    bool Changed = false;
    if (!NormalizeStudentTable(Working, Changed, Error)) return false;
    const TArray<FProfessorStudent> NormalizedRows = ReadStudents(Working);
    TMap<FName, FString> LegacyIdentity;
    TMap<FString, FString> LegacyReferenceIdentity;
    for (const FProfessorStudent& Legacy : LegacyRows)
    {
        const bool UniqueReference = !Legacy.ReferenceNumber.IsEmpty() && LegacyRows.FilterByPredicate([&Legacy](const FProfessorStudent& S) { return S.ReferenceNumber.Equals(Legacy.ReferenceNumber, ESearchCase::IgnoreCase); }).Num() == 1;
        const FProfessorStudent* Identity = NormalizedRows.FindByPredicate([&Legacy, UniqueReference](const FProfessorStudent& S)
        {
            return UniqueReference && !IsLegacyGeneratedReference(Legacy.ReferenceNumber) ? S.ReferenceNumber == Legacy.ReferenceNumber : (S.StudentID == Legacy.StudentID && S.StudentName == Legacy.StudentName);
        });
        if (Identity)
        {
            LegacyIdentity.Add(Legacy.RowName, Identity->ReferenceNumber);
            if (UniqueReference) LegacyReferenceIdentity.Add(Legacy.ReferenceNumber, Identity->ReferenceNumber);
        }
    }
    TSet<FString> DeletedReferences;
    int32 Next = FMath::Max(ReadSequence(Working), Saved->NextReferenceNumber);
    for (const FString& Ref : Saved->DeletedReferenceNumbers)
    {
        const FString* Migrated = LegacyReferenceIdentity.Find(Ref);
        DeletedReferences.Add((Migrated ? *Migrated : Ref).ToLower());
        Next = FMath::Max(Next, ReferenceIndex(Ref) + 1);
    }
    for (FName LegacyName : Saved->DeletedRowNames)
    {
        const FString* Ref = LegacyIdentity.Find(LegacyName);
        if (Ref) DeletedReferences.Add(Ref->ToLower());
        else if (NormalizedRows.ContainsByPredicate([LegacyName](const FProfessorStudent& S) { return S.ReferenceNumber == LegacyName.ToString(); }))
            DeletedReferences.Add(LegacyName.ToString().ToLower());
    }
    for (const FProfessorStudent& S : Saved->Students) Next = FMath::Max(Next, ReferenceIndex(S.ReferenceNumber) + 1);
    TArray<FProfessorStudent> Resolved = Saved->Students;
    TSet<FString> UsedReferences;
    for (FProfessorStudent& Student : Resolved)
    {
        if (IsLegacyGeneratedReference(Student.ReferenceNumber))
        {
            const FString* Migrated = LegacyReferenceIdentity.Find(Student.ReferenceNumber);
            Student.ReferenceNumber = Migrated ? *Migrated : FString();
        }
        // Only legacy saves without a reference may use old keys or student IDs for migration.
        if (Student.ReferenceNumber.IsEmpty())
        {
            if (const FString* Ref = LegacyIdentity.Find(Student.RowName)) Student.ReferenceNumber = *Ref;
            else
            {
                const FProfessorStudent* Match = NormalizedRows.FindByPredicate([&Student](const FProfessorStudent& S) { return S.ReferenceNumber == Student.RowName.ToString(); });
                if (!Match)
                {
                    const TArray<FProfessorStudent> Matches = NormalizedRows.FilterByPredicate([&Student](const FProfessorStudent& S) { return S.StudentID.Equals(Student.StudentID, ESearchCase::IgnoreCase); });
                    if (Matches.Num() == 1) Student.ReferenceNumber = Matches[0].ReferenceNumber;
                }
                else Student.ReferenceNumber = Match->ReferenceNumber;
            }
            if (Student.ReferenceNumber.IsEmpty())
            {
                if (Next >= MAX_int32) { Error = TEXT("Student reference sequence is exhausted."); return false; }
                Student.ReferenceNumber = TEXT("Student") + FString::FromInt(Next++);
            }
        }
        if (DeletedReferences.Contains(Student.ReferenceNumber.ToLower()) || UsedReferences.Contains(Student.ReferenceNumber.ToLower()))
        { Error = TEXT("Saved student identities overlap or refer to deleted students."); return false; }
        UsedReferences.Add(Student.ReferenceNumber.ToLower());
    }
    for (const FProfessorStudent& Student : NormalizedRows)
        if (DeletedReferences.Contains(Student.ReferenceNumber.ToLower())) Working->RemoveRow(Student.RowName);
    for (const FProfessorStudent& Student : Resolved)
    {
        FName WorkingKey = NAME_None;
        for (const TPair<FName, uint8*>& Pair : Working->GetRowMap())
            if (ReadString(Struct, Pair.Value, TEXT("Reference_Number")) == Student.ReferenceNumber) { WorkingKey = Pair.Key; break; }
        uint8* const* Row = Working->GetRowMap().Find(WorkingKey);
        if (!Row || !*Row)
        {
            // Initialize the actual table struct: texture references and optional fields stay empty.
            FStructOnScope NewRow(Struct);
            WriteNumber(Struct, NewRow.GetStructMemory(), TEXT("ExpectedScore"), -1);
            WorkingKey = FName(*(TEXT("NewStudent_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            Working->AddRow(WorkingKey, *reinterpret_cast<const FTableRowBase*>(NewRow.GetStructMemory()));
            Row = Working->GetRowMap().Find(WorkingKey);
            if (!Row || !*Row) continue;
        }
        WriteString(Struct, *Row, TEXT("StudentName"), Student.StudentName);
        WriteString(Struct, *Row, TEXT("StudentID"), Student.StudentID);
        WriteString(Struct, *Row, TEXT("Reference_Number"), Student.ReferenceNumber);
        WriteOCA(Struct, *Row, Student.OCA);
        WriteNumber(Struct, *Row, TEXT("ExamScore"), Student.OEA);
        const FProfessorGrade Grade = CalculateGrade(Student.OCA, Student.OEA, Student.bHasOEA);
        WriteString(Struct, *Row, TEXT("Grade"), Grade.Grade);
        WriteFloat(Struct, *Row, TEXT("GPA"), Grade.GPA);
        WriteFloat(Struct, *Row, TEXT("WeightedTotal"), Grade.Total);
        if (FBoolProperty* HasOEA = CastField<FBoolProperty>(FindField(Struct, TEXT("HasOEA"))))
            HasOEA->SetPropertyValue_InContainer(*Row, Student.bHasOEA);
        if (FBoolProperty* Suspicious = CastField<FBoolProperty>(FindField(Struct, TEXT("IsMarkingSuspicious"))))
        {
            FProfessorStudent AuthoritativeStudent = Student;
            AuthoritativeStudent.ExpectedScore = ReadNumber(Struct, *Row, TEXT("ExpectedScore"), -1);
            AuthoritativeStudent.bHasExpectedScore = AuthoritativeStudent.ExpectedScore >= 0 && AuthoritativeStudent.ExpectedScore <= 100;
            Suspicious->SetPropertyValue_InContainer(*Row, IsSuspicious(AuthoritativeStudent));
        }
    }
    StoreSequence(Working, Next);
    if (!NormalizeStudentTable(Working, Changed, Error)) return false;
    if (!ValidateRecords(ReadStudents(Working), Error)) return false;
    Table->Modify(false);
    CopyRows(Table, Working);
    Error.Empty();
    return true;
}
