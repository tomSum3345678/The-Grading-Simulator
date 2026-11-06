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
    const FString SaveSlot(TEXT("ProfessorGrades_v1"));
    const FString BackupSlot(TEXT("ProfessorGrades_v1_Backup"));

    bool AuthoredTableIsNewer(const UDataTable* Table)
    {
#if WITH_EDITOR
        if (!Table || !Table->GetOutermost()) return false;
        const FString AssetPath = FPackageName::LongPackageNameToFilename(Table->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        const FString SavedPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), SaveSlot + TEXT(".sav"));
        IFileManager& Files = IFileManager::Get();
        if (Files.FileExists(*AssetPath) && Files.FileExists(*SavedPath))
            return Files.GetTimeStamp(*AssetPath) > Files.GetTimeStamp(*SavedPath);
#endif
        return false;
    }

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
            S.OCA = ReadNumber(Struct, Pair.Value, TEXT("CA_Score"));
            S.OEA = ReadNumber(Struct, Pair.Value, TEXT("ExamScore"));
            S.bHasOEA = ReadBool(Struct, Pair.Value, TEXT("HasOEA"), true);
            S.ExpectedScore = ReadNumber(Struct, Pair.Value, TEXT("ExpectedScore"), -1);
            S.bHasExpectedScore = S.ExpectedScore >= 0 && S.ExpectedScore <= 100;
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
    // PIE shares editor asset objects. Restore the original authored table when the session ends.
    if (SourceTable && EditorTableSnapshot && SourceTable->GetRowStruct() == EditorTableSnapshot->GetRowStruct())
    {
        SourceTable->EmptyTable();
        for (const TPair<FName, uint8*>& Pair : EditorTableSnapshot->GetRowMap())
        {
            SourceTable->AddRow(Pair.Key, *reinterpret_cast<const FTableRowBase*>(Pair.Value));
        }
        SourceTable->HandleDataTableChanged();
        StoreSequence(SourceTable, ReadSequence(EditorTableSnapshot));
    }
    EditorTableSnapshot = nullptr;
    SourceTable = nullptr;
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

FProfessorGrade UGradingStore::CalculateGrade(int32 OCA, int32 OEA, bool bHasOEA)
{
    FProfessorGrade Result;
    if (!bHasOEA) return Result;
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
        if (Student.OCA < 0 || Student.OCA > 100 || (Student.bHasOEA && (Student.OEA < 0 || Student.OEA > 100)))
        {
            Error = TEXT("OCA and OEA must be whole numbers from 0 to 100.");
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
    if (!Saved || Saved->FormatVersion != 1)
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
    SourceTable = InTable;
    if (!SourceTable || !SourceTable->GetRowStruct())
    {
        LastError = TEXT("Student data could not be loaded.");
        return;
    }
    bInitialized = true;
#if WITH_EDITOR
    if (GIsEditor)
    {
        EditorTableSnapshot = DuplicateObject<UDataTable>(SourceTable, this);
        StoreSequence(EditorTableSnapshot, ReadSequence(SourceTable));
    }
#endif
    const TArray<FProfessorStudent> LegacyRows = ReadStudents(SourceTable);
    UDataTable* Baseline = DuplicateObject<UDataTable>(SourceTable, GetTransientPackage());
    StoreSequence(Baseline, ReadSequence(SourceTable));
    bool Changed = false;
    if (!NormalizeStudentTable(Baseline, Changed, LastError)) { bInvalidSave = true; return; }
    Records = ReadStudents(Baseline);
    NextReferenceNumber = ReadSequence(Baseline);
    for (const FProfessorStudent& Student : Records) AuthoritativeRows.Add(Student.RowName, Student);

    if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
    {
        UProfessorGradeSave* Saved = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
        FString ValidationError;
        if (!ValidateSave(Saved, ValidationError))
        {
            bInvalidSave = true;
            LastError = TEXT("Saved grades could not be read. The existing save is protected; restore its backup before saving.");
            return;
        }
        NextReferenceNumber = FMath::Max(NextReferenceNumber, Saved->NextReferenceNumber);
        for (const FString& Ref : Saved->DeletedReferenceNumbers) NextReferenceNumber = FMath::Max(NextReferenceNumber, ReferenceIndex(Ref) + 1);
        for (const FProfessorStudent& S : Saved->Students) NextReferenceNumber = FMath::Max(NextReferenceNumber, ReferenceIndex(S.ReferenceNumber) + 1);
        // Editor/Standalone development respects explicitly saved DataTable edits.
        // Cooked builds always restore the durable player save.
        if (AuthoredTableIsNewer(SourceTable))
        {
            CopyRows(SourceTable, Baseline);
            ApplyToDataTable();
            return;
        }
        DeletedReferenceNumbers = Saved->DeletedReferenceNumbers;
        for (FName DeletedName : Saved->DeletedRowNames)
        {
            const FProfessorStudent* Legacy = LegacyRows.FindByPredicate([DeletedName](const FProfessorStudent& S) { return S.RowName == DeletedName; });
            const FProfessorStudent* Identity = Records.FindByPredicate([&Legacy, DeletedName](const FProfessorStudent& S) { return S.ReferenceNumber == DeletedName.ToString() || (Legacy && S.StudentID == Legacy->StudentID); });
            if (Identity) DeletedReferenceNumbers.AddUnique(Identity->ReferenceNumber);
            else DeletedRowNames.AddUnique(DeletedName);
        }
        if (!ApplySavedRecordsToTable(SourceTable, Saved, LastError)) { bInvalidSave = true; return; }
        Records = ReadStudents(SourceTable);
        NextReferenceNumber = FMath::Max(NextReferenceNumber, ReadSequence(SourceTable));
        return;
    }
    CopyRows(SourceTable, Baseline);
    ApplyToDataTable();
}

bool UGradingStore::SaveRecords(const TArray<FProfessorStudent>& NewRecords, FString& Error)
{
    if (!bLoggedIn) { Error = TEXT("Log in before saving grades."); return false; }
    if (!bInitialized) { Error = TEXT("Student data is not ready."); return false; }
    if (bInvalidSave) { Error = LastError; return false; }
    TArray<FProfessorStudent> Candidate = NewRecords;
    int32 CandidateNext = FMath::Max(NextReferenceNumber, ReadSequence(SourceTable));
    for (const FString& Ref : DeletedReferenceNumbers) CandidateNext = FMath::Max(CandidateNext, ReferenceIndex(Ref) + 1);
    for (FProfessorStudent& Student : Candidate)
    {
        Student.StudentName.TrimStartAndEndInline();
        Student.StudentID.TrimStartAndEndInline();
        const FProfessorStudent* Existing = Records.FindByPredicate([&Student](const FProfessorStudent& S)
        {
            return !Student.ReferenceNumber.IsEmpty() ? S.ReferenceNumber == Student.ReferenceNumber : (!Student.RowName.IsNone() && S.RowName == Student.RowName);
        });
        if (Existing) Student.ReferenceNumber = Existing->ReferenceNumber;
        else
        {
            if (CandidateNext >= MAX_int32) { Error = TEXT("Student reference sequence is exhausted."); return false; }
            Student.ReferenceNumber = TEXT("Student") + FString::FromInt(CandidateNext++);
        }
    }
    RestoreAuthoritativeScores(Candidate);
    AssignRowNames(Candidate);
    if (!ValidateRecords(Candidate, Error)) return false;
    TArray<FName> CandidateDeletedRows = DeletedRowNames;
    TArray<FString> CandidateDeletedReferences = DeletedReferenceNumbers;
    for (const FProfessorStudent& Original : Records)
    {
        if (!Candidate.ContainsByPredicate([&Original](const FProfessorStudent& Student) { return Student.ReferenceNumber == Original.ReferenceNumber; }))
            CandidateDeletedReferences.AddUnique(Original.ReferenceNumber);
    }
    CandidateDeletedRows.RemoveAll([&Candidate](FName Name)
    {
        return Candidate.ContainsByPredicate([Name](const FProfessorStudent& Student) { return Student.RowName == Name; });
    });

    if (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
    {
        UProfessorGradeSave* Previous = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
        FString ValidationError;
        if (!ValidateSave(Previous, ValidationError))
        {
            bInvalidSave = true;
            LastError = Error = TEXT("The previous save is unreadable; it has not been overwritten.");
            return false;
        }
        if (!UGameplayStatics::SaveGameToSlot(Previous, BackupSlot, 0))
        {
            Error = TEXT("Could not back up previous grades. Nothing was changed.");
            return false;
        }
    }

    UProfessorGradeSave* Saved = Cast<UProfessorGradeSave>(UGameplayStatics::CreateSaveGameObject(UProfessorGradeSave::StaticClass()));
    Saved->Students = Candidate;
    Saved->DeletedRowNames = CandidateDeletedRows;
    Saved->DeletedReferenceNumbers = CandidateDeletedReferences;
    Saved->NextReferenceNumber = CandidateNext;
    if (!ValidateSave(Saved, Error)) return false;
    UDataTable* ValidationTable = DuplicateObject<UDataTable>(SourceTable, GetTransientPackage());
    if (!ApplySavedRecordsToTable(ValidationTable, Saved, Error)) return false;
    if (!UGameplayStatics::SaveGameToSlot(Saved, SaveSlot, 0))
    {
        Error = TEXT("Could not save grades. Your edits remain open; check disk space and try again.");
        return false;
    }
    Records = MoveTemp(Candidate);
    DeletedRowNames = MoveTemp(CandidateDeletedRows);
    DeletedReferenceNumbers = MoveTemp(CandidateDeletedReferences);
    NextReferenceNumber = CandidateNext;
    ApplyToDataTable();
    LastError.Empty();
    Error.Empty();
    return true;
}

void UGradingStore::ApplyToDataTable()
{
    UProfessorGradeSave* Snapshot = NewObject<UProfessorGradeSave>();
    Snapshot->Students = Records;
    Snapshot->DeletedRowNames = DeletedRowNames;
    Snapshot->DeletedReferenceNumbers = DeletedReferenceNumbers;
    Snapshot->NextReferenceNumber = NextReferenceNumber;
    FString Error;
    if (!ApplySavedRecordsToTable(SourceTable, Snapshot, Error)) LastError = Error;
}

bool UGradingStore::ApplySavedRecordsToTable(UDataTable* Table, const UProfessorGradeSave* Saved, FString& Error)
{
    if (!Table || !Table->GetRowStruct()) { Error = TEXT("Student DataTable is unavailable."); return false; }
    if (!ValidateSave(Saved, Error)) return false;
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
        WriteNumber(Struct, *Row, TEXT("CA_Score"), Student.OCA);
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
