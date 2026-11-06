#include "GradingSetupLibrary.h"

#include "GradingDesktop.h"
#include "GradingStore.h"
#include "AssetRegistryModule.h"
#include "DataTableEditorUtils.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet/GameplayStatics.h"
#include "KismetCompiler.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace GradingSetup
{
    TWeakObjectPtr<UDataTable> PendingSaveTable;
    FDateTime PendingSaveTimestamp = FDateTime::MinValue();
    FString PendingTableJSON;
    FDateTime LastProcessedTimestamp = FDateTime::MinValue();

    bool CanReplaceAsset(UObject* Asset)
    {
        // Release this editor's own package reader before checking for another
        // process holding the file. SavePackage also resets package loaders.
        FlushAsyncLoading();
        Asset->GetOutermost()->FullyLoad();
        ResetLoaders(Asset->GetOutermost());
#if PLATFORM_WINDOWS
        const FString Filename = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()));
        // OPEN_EXISTING probes write sharing without truncating or changing the asset.
        HANDLE Handle = ::CreateFileW(*Filename, GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE) return false;
        ::CloseHandle(Handle);
#endif
        return true;
    }

    bool Backup(UObject* Asset, const FString& Folder, FString& Error)
    {
        const FString Source = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        const FString Destination = Folder / (Asset->GetName() + TEXT(".uasset"));
        if (IFileManager::Get().FileExists(*Destination)) return true;
        if (IFileManager::Get().Copy(*Destination, *Source, false, false) != COPY_OK)
        {
            Error = TEXT("Cannot back up ") + Source;
            return false;
        }
        return true;
    }

    bool Save(UObject* Asset, FString& Error)
    {
        UPackage* Package = Asset->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        Package->MarkPackageDirty();
        if (!UPackage::SavePackage(Package, Asset, RF_Public | RF_Standalone, *Filename, GError, nullptr, false, true, SAVE_NoError))
        {
            Error = TEXT("Failed to save ") + Filename;
            return false;
        }
        return true;
    }

    bool AddField(UUserDefinedStruct* Struct, const FString& Name, FName Category, const FString& Default, FString& Error)
    {
        for (const FStructVariableDescription& Field : FStructureEditorUtils::GetVarDesc(Struct))
        {
            if (Field.FriendlyName == Name)
            {
                if (Field.Category != Category)
                {
                    Error = TEXT("Existing field has a different type: ") + Name;
                    return false;
                }
                return true;
            }
        }
        FEdGraphPinType Type;
        Type.PinCategory = Category;
        if (!FStructureEditorUtils::AddVariable(Struct, Type))
        {
            Error = TEXT("Cannot add field: ") + Name;
            return false;
        }
        const FGuid Guid = FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
        if (!FStructureEditorUtils::RenameVariable(Struct, Guid, Name) ||
            (!Default.IsEmpty() && !FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Guid, Default)))
        {
            Error = TEXT("Cannot configure field: ") + Name;
            return false;
        }
        return true;
    }

    bool WireDesktop(UBlueprint* Blueprint, FString& Error)
    {
        UEdGraph* Graph = nullptr;
        UK2Node_MacroInstance* EachStudent = nullptr;
        for (UEdGraph* Candidate : Blueprint->UbergraphPages)
        {
            for (UEdGraphNode* Node : Candidate->Nodes)
            {
                if (Node->GetFName() == TEXT("EachCurrentStudent"))
                {
                    EachStudent = Cast<UK2Node_MacroInstance>(Node);
                    Graph = Candidate;
                }
            }
        }
        if (!Graph || !EachStudent)
        {
            Error = TEXT("Cannot find existing student initialization loop; no graph was changed.");
            return false;
        }
        UEdGraphPin* Completed = EachStudent->FindPin(TEXT("Completed"));
        if (!Completed)
        {
            Error = TEXT("Student loop has no Completed pin.");
            return false;
        }
        for (UEdGraphPin* Link : Completed->LinkedTo)
        {
            if (UK2Node_CallFunction* Existing = Cast<UK2Node_CallFunction>(Link->GetOwningNode()))
            {
                if (Existing->GetTargetFunction() == UGradingDesktopLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UGradingDesktopLibrary, InstallDesktop))) return true;
            }
        }
        Graph->Modify();
        UK2Node_CallFunction* Install = NewObject<UK2Node_CallFunction>(Graph, TEXT("InstallProfessorDesktop"), RF_Transactional);
        Graph->AddNode(Install, false, false);
        Install->CreateNewGuid();
        Install->SetFromFunction(UGradingDesktopLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UGradingDesktopLibrary, InstallDesktop)));
        Install->AllocateDefaultPins();
        Install->NodePosX = EachStudent->NodePosX + 400;
        Install->NodePosY = EachStudent->NodePosY + 650;
        UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph, TEXT("ProfessorDesktopSelf"), RF_Transactional);
        Graph->AddNode(Self, false, false);
        Self->CreateNewGuid();
        Self->AllocateDefaultPins();
        Self->NodePosX = Install->NodePosX - 220;
        Self->NodePosY = Install->NodePosY + 150;
        const UEdGraphSchema* Schema = Graph->GetSchema();
        const TArray<UEdGraphPin*> OldLinks = Completed->LinkedTo;
        Completed->BreakAllPinLinks();
        bool bConnected = Schema->TryCreateConnection(Completed, Install->FindPin(UEdGraphSchema_K2::PN_Execute));
        bConnected &= Schema->TryCreateConnection(Self->FindPin(UEdGraphSchema_K2::PN_Self), Install->FindPin(TEXT("Desktop")));
        for (UEdGraphPin* Link : OldLinks) bConnected &= Schema->TryCreateConnection(Install->GetThenPin(), Link);
        if (!bConnected)
        {
            Install->BreakAllNodeLinks();
            Self->BreakAllNodeLinks();
            Graph->RemoveNode(Install);
            Graph->RemoveNode(Self);
            for (UEdGraphPin* Link : OldLinks) Schema->TryCreateConnection(Completed, Link);
            Error = TEXT("Cannot connect desktop installation node; original graph links restored.");
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
}

FString UGradingSetupLibrary::ExportStudentsJSON()
{
    const UDataTable* Students = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    return Students ? Students->GetTableAsJSON() : FString();
}

FString UGradingSetupLibrary::NormalizeStudentRows()
{
    if (GEditor && GEditor->PlayWorld) return TEXT("WAIT: Student row normalization resumes after Play In Editor ends.");
    UDataTable* Students = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    if (!Students) return TEXT("ERROR: Cannot load DT_Students for row normalization.");
    bool bChanged = false;
    FString Error;
    FDataTableEditorUtils::BroadcastPreChange(Students, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
    const bool bSuccess = UGradingStore::NormalizeStudentTable(Students, bChanged, Error);
    FDataTableEditorUtils::BroadcastPostChange(Students, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
    if (!bSuccess) return TEXT("ERROR: Student rows were not normalized: ") + Error;
    if (bChanged)
    {
        Students->MarkPackageDirty();
        return TEXT("OK: Student row names and reference numbers updated. Save DT_Students to keep these editor changes.");
    }
    return TEXT("UP_TO_DATE: Student row names and reference numbers already match.");
}

FString UGradingSetupLibrary::SyncSavedGrades(bool Force)
{
    using namespace GradingSetup;
    if (GEditor && GEditor->PlayWorld) return TEXT("WAIT: Synchronization resumes after Play In Editor ends.");
    const FString SaveFilename = FPaths::ProjectSavedDir() / TEXT("SaveGames/ProfessorGrades_v1.sav");
    if (!IFileManager::Get().FileExists(*SaveFilename)) return TEXT("NO_SAVE: No saved professor grades exist yet.");
    const FDateTime SaveTimestamp = IFileManager::Get().GetTimeStamp(*SaveFilename);
    UDataTable* Students = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    if (!Students) return TEXT("ERROR: Cannot load DT_Students for grade synchronization.");
    FString Error;

    if (PendingSaveTable.IsValid())
    {
        UDataTable* Pending = PendingSaveTable.Get();
        if (Pending->GetTableAsJSON() != PendingTableJSON)
        {
            // An author edited the asset after our import. Their explicit save takes priority;
            // do not silently replace or save their unfinished edits on a timer.
            if (Pending->GetOutermost()->IsDirty()) return TEXT("WAIT: DT_Students was edited after importing grades. Save or undo those editor changes before synchronization resumes.");
            PendingSaveTable.Reset();
            PendingTableJSON.Empty();
            return TEXT("UP_TO_DATE: Explicitly saved DataTable edits take priority over the pending game import.");
        }
        if (SaveTimestamp > PendingSaveTimestamp)
        {
            // A second game save may arrive while the first import is waiting for the game
            // to release the .uasset. Import it before saving; otherwise our newer asset
            // timestamp would incorrectly hide this later save forever.
            UProfessorGradeSave* Newest = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromSlot(TEXT("ProfessorGrades_v1"), 0));
            if (!UGradingStore::ValidateSave(Newest, Error)) return TEXT("ERROR: Newer saved grades are invalid; the previous import will not overwrite them: ") + Error;
            FDataTableEditorUtils::BroadcastPreChange(Pending, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
            const bool bApplied = UGradingStore::ApplySavedRecordsToTable(Pending, Newest, Error);
            FDataTableEditorUtils::BroadcastPostChange(Pending, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
            if (!bApplied) return TEXT("ERROR: Newer saved grades could not be imported; disk synchronization is paused: ") + Error;
            Pending->MarkPackageDirty();
            PendingSaveTimestamp = SaveTimestamp;
            PendingTableJSON = Pending->GetTableAsJSON();
        }
        if (!CanReplaceAsset(PendingSaveTable.Get())) return TEXT("WAIT: Saved grades are visible in DT_Students; close the running game to finish writing its asset file.");
        if (!Save(PendingSaveTable.Get(), Error)) return TEXT("ERROR: ") + Error;
        LastProcessedTimestamp = PendingSaveTimestamp;
        PendingSaveTable.Reset();
        PendingTableJSON.Empty();
        return TEXT("OK: Saved professor grades synchronized to DT_Students and written to disk.");
    }

    const FString AssetFilename = FPackageName::LongPackageNameToFilename(Students->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (!Force && (SaveTimestamp <= LastProcessedTimestamp || SaveTimestamp <= IFileManager::Get().GetTimeStamp(*AssetFilename))) return TEXT("UP_TO_DATE: DT_Students already contains these saved grades or newer editor changes.");
    if (Students->GetOutermost()->IsDirty()) return TEXT("WAIT: Save or revert your existing DT_Students editor changes before importing game grades.");
    UProfessorGradeSave* SavedGrades = Cast<UProfessorGradeSave>(UGameplayStatics::LoadGameFromSlot(TEXT("ProfessorGrades_v1"), 0));
    if (!UGradingStore::ValidateSave(SavedGrades, Error)) return TEXT("ERROR: Saved grades were not imported: ") + Error;

    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("GradingMigration/BeforeSaveSync");
    IFileManager::Get().MakeDirectory(*BackupFolder, true);
    if (!Backup(Students, BackupFolder, Error)) return TEXT("ERROR: ") + Error;
    const FString SaveBackup = BackupFolder / TEXT("ProfessorGrades_v1.sav");
    if (!IFileManager::Get().FileExists(*SaveBackup) && IFileManager::Get().Copy(*SaveBackup, *SaveFilename, false, false) != COPY_OK) return TEXT("ERROR: Cannot back up the professor save before synchronization.");
    const FString JSONBackup = BackupFolder / TEXT("DT_Students.before.json");
    if (!IFileManager::Get().FileExists(*JSONBackup) && !FFileHelper::SaveStringToFile(Students->GetTableAsJSON(), *JSONBackup)) return TEXT("ERROR: Cannot back up current student values before synchronization.");

    FDataTableEditorUtils::BroadcastPreChange(Students, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
    const bool bApplied = UGradingStore::ApplySavedRecordsToTable(Students, SavedGrades, Error);
    FDataTableEditorUtils::BroadcastPostChange(Students, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
    if (!bApplied) return TEXT("ERROR: Saved grades were not imported: ") + Error;
    Students->MarkPackageDirty();
    PendingSaveTable = Students;
    PendingSaveTimestamp = SaveTimestamp;
    PendingTableJSON = Students->GetTableAsJSON();
    if (!CanReplaceAsset(Students)) return TEXT("WAIT: Saved grades are visible in DT_Students; close the running game to finish writing its asset file.");
    if (!Save(Students, Error)) return TEXT("ERROR: ") + Error;
    PendingSaveTable.Reset();
    PendingTableJSON.Empty();
    LastProcessedTimestamp = SaveTimestamp;
    return FString::Printf(TEXT("OK: Saved professor grades synchronized to DT_Students (%d rows) and written to disk."), Students->GetRowMap().Num());
}

FString UGradingSetupLibrary::Install()
{
    using namespace GradingSetup;
    UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, TEXT("/Game/DataTable/StudentData.StudentData"));
    UDataTable* Students = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
    UBlueprint* Desktop = LoadObject<UBlueprint>(nullptr, TEXT("/Game/ComputerDemo/WBP_Desktop.WBP_Desktop"));
    if (!Struct || !Students || !Desktop) return TEXT("ERROR: Missing StudentData, DT_Students or WBP_Desktop.");
    FString Error;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("GradingMigration");
    IFileManager::Get().MakeDirectory(*Folder, true);
    if (!Backup(Struct, Folder, Error) || !Backup(Students, Folder, Error) || !Backup(Desktop, Folder, Error)) return TEXT("ERROR: ") + Error;
    const FString OriginalJSON = Students->GetTableAsJSON();
    const FString JSONBackup = Folder / TEXT("DT_Students.before.json");
    if (!IFileManager::Get().FileExists(*JSONBackup) && !FFileHelper::SaveStringToFile(OriginalJSON, *JSONBackup)) return TEXT("ERROR: Cannot back up current student row values.");
    const bool bHadExpected = FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("ExpectedScore")) != nullptr;
    const bool bHadHasOEA = FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("HasOEA")) != nullptr;
    if (!AddField(Struct, TEXT("ExpectedScore"), UEdGraphSchema_K2::PC_Int, TEXT("-1"), Error) ||
        !AddField(Struct, TEXT("Grade"), UEdGraphSchema_K2::PC_String, TEXT(""), Error) ||
        !AddField(Struct, TEXT("GPA"), UEdGraphSchema_K2::PC_Float, TEXT("0.0"), Error) ||
        !AddField(Struct, TEXT("WeightedTotal"), UEdGraphSchema_K2::PC_Float, TEXT("0.0"), Error) ||
        !AddField(Struct, TEXT("HasOEA"), UEdGraphSchema_K2::PC_Boolean, TEXT("True"), Error) ||
        !AddField(Struct, TEXT("IsMarkingSuspicious"), UEdGraphSchema_K2::PC_Boolean, TEXT("False"), Error)) return TEXT("ERROR: ") + Error;

    // Restore the exact pre-migration row values; missing new columns use explicit defaults below.
    const bool bOldIgnoreMissing = Students->bIgnoreMissingFields;
    Students->bIgnoreMissingFields = true;
    const TArray<FString> Problems = Students->CreateTableFromJSONString(OriginalJSON);
    Students->bIgnoreMissingFields = bOldIgnoreMissing;
    if (Problems.Num()) return TEXT("ERROR restoring original student data: ") + FString::Join(Problems, TEXT("; "));
    FIntProperty* Expected = CastField<FIntProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("ExpectedScore")));
    FBoolProperty* HasOEA = CastField<FBoolProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("HasOEA")));
    FIntProperty* OCA = CastField<FIntProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("CA_Score")));
    FIntProperty* OEA = CastField<FIntProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("ExamScore")));
    FStrProperty* Grade = CastField<FStrProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("Grade")));
    FFloatProperty* GPA = CastField<FFloatProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("GPA")));
    FFloatProperty* Total = CastField<FFloatProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("WeightedTotal")));
    FBoolProperty* Suspicious = CastField<FBoolProperty>(FStructureEditorUtils::GetPropertyByFriendlyName(Struct, TEXT("IsMarkingSuspicious")));
    if (!Expected || !HasOEA || !OCA || !OEA || !Grade || !GPA || !Total || !Suspicious) return TEXT("ERROR: Student property types do not match the required schema.");
    for (const auto& Pair : Students->GetRowMap())
    {
        uint8* Row = Pair.Value;
        if (!bHadExpected) Expected->SetPropertyValue_InContainer(Row, -1);
        if (!bHadHasOEA) HasOEA->SetPropertyValue_InContainer(Row, true);
        const FProfessorGrade Result = UGradingStore::CalculateGrade(OCA->GetPropertyValue_InContainer(Row), OEA->GetPropertyValue_InContainer(Row), HasOEA->GetPropertyValue_InContainer(Row));
        Grade->SetPropertyValue_InContainer(Row, Result.Grade);
        GPA->SetPropertyValue_InContainer(Row, Result.GPA);
        Total->SetPropertyValue_InContainer(Row, Result.Total);
        const int32 Standard = Expected->GetPropertyValue_InContainer(Row);
        Suspicious->SetPropertyValue_InContainer(Row, HasOEA->GetPropertyValue_InContainer(Row) && Standard >= 0 && Standard <= 100 && FMath::Abs(OEA->GetPropertyValue_InContainer(Row) - Standard) > 15);
    }
    UDataTable* Accounts = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_ProfessorAccounts.DT_ProfessorAccounts"));
    if (!Accounts)
    {
        UPackage* Package = CreatePackage(TEXT("/Game/DataTable/DT_ProfessorAccounts"));
        Accounts = NewObject<UDataTable>(Package, TEXT("DT_ProfessorAccounts"), RF_Public | RF_Standalone);
        Accounts->RowStruct = FProfessorAccountRow::StaticStruct();
        FProfessorAccountRow Default;
        Default.Account = TEXT("DrLee");
        Default.Password = TEXT("drleepw");
        Accounts->AddRow(TEXT("DrLee"), Default);
        FAssetRegistryModule::AssetCreated(Accounts);
    }
    if (Accounts->RowStruct != FProfessorAccountRow::StaticStruct()) return TEXT("ERROR: DT_ProfessorAccounts has an unexpected row type.");
    if (!WireDesktop(Desktop, Error)) return TEXT("ERROR: ") + Error;
    const TCHAR* Dependencies[] = {
        TEXT("/Game/ComputerDemo/WBP_StudentRow.WBP_StudentRow"),
        TEXT("/Game/ExamPapers/WBP_ExamPaper.WBP_ExamPaper"),
        TEXT("/Game/ExamPapers/WBP_SelectPaper.WBP_SelectPaper"),
        TEXT("/Game/ComputerDemo/WBP_Desktop.WBP_Desktop")
    };
    TArray<UBlueprint*> Compiled;
    for (const TCHAR* Path : Dependencies)
    {
        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, Path);
        if (!Blueprint) return FString(TEXT("ERROR: Missing dependent Blueprint ")) + Path;
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
        if (Log.NumErrors > 0 || Blueprint->Status == BS_Error) return FString(TEXT("ERROR: Blueprint compile failed: ")) + Path;
        Compiled.Add(Blueprint);
    }
    if (!Save(Struct, Error) || !Save(Students, Error) || !Save(Accounts, Error)) return TEXT("ERROR: ") + Error;
    for (UBlueprint* Blueprint : Compiled) if (!Save(Blueprint, Error)) return TEXT("ERROR: ") + Error;
    return FString::Printf(TEXT("OK: Student schema and %d rows preserved; account table created/retained; desktop installed; 4 Blueprints compiled and saved. Backup: %s"), Students->GetRowMap().Num(), *Folder);
}
