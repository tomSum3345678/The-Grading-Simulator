#include "GradingSetupLibrary.h"

#include "AssetRegistryModule.h"
#include "AssetData.h"
#include "Blueprint/UserWidget.h"
#include "BlueprintCompilationManager.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace GradingOptionsSetup
{
    bool Backup(UObject* Asset, FString& Error)
    {
        const FString PackageName = Asset->GetOutermost()->GetName();
        const FString Source = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().FileExists(*Source)) return true;
        const FString Destination = FPaths::ProjectSavedDir() / TEXT("OptionsMigration/Assets") / (PackageName.RightChop(1) + TEXT(".uasset"));
        if (IFileManager::Get().FileExists(*Destination)) return true;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
        if (IFileManager::Get().Copy(*Destination, *Source, false, false) != COPY_OK)
        {
            Error = TEXT("Cannot back up Options asset: ") + Source;
            return false;
        }
        return true;
    }

    bool Save(UObject* Asset, FString& Error)
    {
        UPackage* Package = Asset->GetOutermost();
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();
            if (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error)
            {
                Error = TEXT("Options Blueprint is not ready to save: ") + Blueprint->GetName();
                return false;
            }

            // Skeleton classes are transient, but dependency reinstancing can leave a
            // public skeleton CDO in the asset package without that flag. It must not
            // be exported independently of its excluded class (SavePackage asserts).
            // Restore the same transient invariant used by the skeleton compiler.
            TArray<UObject*> PackageObjects;
            GetObjectsWithPackage(Package, PackageObjects, true);
            for (UObject* Object : PackageObjects)
            {
                UClass* ObjectClass = Object->GetClass();
                if (ObjectClass->HasAnyFlags(RF_Transient)
                    && ObjectClass->GetName().StartsWith(TEXT("SKEL_")))
                {
                    Object->SetFlags(RF_Transient);
                    Object->ClearFlags(RF_Standalone);
                    TArray<UObject*> Subobjects;
                    GetObjectsWithOuter(Object, Subobjects, true);
                    for (UObject* Subobject : Subobjects)
                    {
                        Subobject->SetFlags(RF_Transient);
                        Subobject->ClearFlags(RF_Standalone);
                    }
                }
            }
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        Package->MarkPackageDirty();
        // Match the editor's asset-save root selection; public generated objects
        // are reached through the asset, rather than treated as extra save roots.
        if (!UPackage::SavePackage(Package, Asset, RF_Standalone, *Filename, GError, nullptr, false, true, SAVE_NoError))
        {
            Error = TEXT("Cannot save Options asset: ") + Filename;
            return false;
        }
        return true;
    }

    template<typename T>
    T* CreateAsset(const TCHAR* PackagePath, const TCHAR* AssetName, TArray<UObject*>& Changed)
    {
        const FString ObjectPath = FString(PackagePath) + TEXT(".") + AssetName;
        if (T* Existing = LoadObject<T>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn)) return Existing;
        UPackage* Package = CreatePackage(PackagePath);
        T* Asset = NewObject<T>(Package, AssetName, RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Asset);
        Changed.Add(Asset);
        return Asset;
    }

    UEdGraph* EventGraph(UBlueprint* Blueprint)
    {
        for (UEdGraph* Graph : Blueprint->UbergraphPages) if (Graph->GetFName() == TEXT("EventGraph")) return Graph;
        return Blueprint->UbergraphPages.Num() ? Blueprint->UbergraphPages[0] : nullptr;
    }

    UK2Node_CallFunction* AddCall(UBlueprint* Blueprint, UEdGraph* Graph, UFunction* Function, const TCHAR* NodeName, const TCHAR* WidgetPin, int32 X, int32 Y, FString& Error)
    {
        UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph, NodeName, RF_Transactional);
        Graph->AddNode(Call, false, false);
        Call->CreateNewGuid();
        Call->SetFromFunction(Function);
        Call->AllocateDefaultPins();
        Call->NodePosX = X;
        Call->NodePosY = Y;
        UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Self, false, false);
        Self->CreateNewGuid();
        Self->AllocateDefaultPins();
        Self->NodePosX = X - 220;
        Self->NodePosY = Y + 160;
        if (!Graph->GetSchema()->TryCreateConnection(Self->FindPin(UEdGraphSchema_K2::PN_Self), Call->FindPin(WidgetPin)))
        {
            Error = TEXT("Cannot connect widget argument for ") + Function->GetName();
            Graph->RemoveNode(Self);
            Graph->RemoveNode(Call);
            return nullptr;
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return Call;
    }

    bool WireOptionClick(UBlueprint* Blueprint, UFunction* OpenOptions, FString& Error)
    {
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node);
                if (!Event || Event->ComponentPropertyName != TEXT("Button_OPTION") || Event->DelegatePropertyName != TEXT("OnClicked")) continue;
                UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then);
                if (!Then) break;
                for (UEdGraphPin* Link : Then->LinkedTo)
                {
                    const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Link->GetOwningNode());
                    if (Call && Call->GetTargetFunction() == OpenOptions) return true;
                }
                UK2Node_CallFunction* Call = AddCall(Blueprint, Graph, OpenOptions, TEXT("OpenProfessorOptions"), TEXT("OriginMenu"), Event->NodePosX + 400, Event->NodePosY, Error);
                if (!Call) return false;
                const TArray<UEdGraphPin*> OldLinks = Then->LinkedTo;
                Then->BreakAllPinLinks();
                const UEdGraphSchema* Schema = Graph->GetSchema();
                bool bConnected = Schema->TryCreateConnection(Then, Call->FindPin(UEdGraphSchema_K2::PN_Execute));
                for (UEdGraphPin* Link : OldLinks) bConnected &= Schema->TryCreateConnection(Call->GetThenPin(), Link);
                if (!bConnected) { Error = TEXT("Cannot wire Options click in ") + Blueprint->GetName(); return false; }
                return true;
            }
        }
        Error = TEXT("Missing Button_OPTION OnClicked event in ") + Blueprint->GetName();
        return false;
    }

    bool WirePresentation(UBlueprint* Blueprint, UFunction* ApplySettings, FString& Error)
    {
        UEdGraph* Graph = EventGraph(Blueprint);
        if (!Graph) { Error = TEXT("No event graph in ") + Blueprint->GetName(); return false; }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
            if (Call && Call->GetTargetFunction() == ApplySettings && Call->GetFName() == TEXT("ApplyProfessorPresentation")) return true;
        }
        UK2Node_Event* Construct = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
            if (Event && Event->EventReference.GetMemberName() == TEXT("Construct")) { Construct = Event; break; }
        }
        if (!Construct)
        {
            Construct = NewObject<UK2Node_Event>(Graph, TEXT("ProfessorPresentationConstruct"), RF_Transactional);
            Graph->AddNode(Construct, false, false);
            Construct->CreateNewGuid();
            Construct->EventReference.SetExternalMember(TEXT("Construct"), UUserWidget::StaticClass());
            Construct->bOverrideFunction = true;
            Construct->AllocateDefaultPins();
            Construct->NodePosY = 8000;
        }
        Construct->SetEnabledState(ENodeEnabledState::Enabled);
        UEdGraphPin* Then = Construct->FindPin(UEdGraphSchema_K2::PN_Then);
        if (!Then) { Error = TEXT("Construct has no execution output in ") + Blueprint->GetName(); return false; }
        UK2Node_CallFunction* Apply = AddCall(Blueprint, Graph, ApplySettings, TEXT("ApplyProfessorPresentation"), TEXT("Widget"), Construct->NodePosX + 600, Construct->NodePosY + 240, Error);
        if (!Apply) return false;
        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (Then->LinkedTo.Num() == 0)
        {
            if (!Schema->TryCreateConnection(Then, Apply->FindPin(UEdGraphSchema_K2::PN_Execute))) { Error = TEXT("Cannot wire Construct presentation hook."); return false; }
        }
        else
        {
            const TArray<UEdGraphPin*> OldLinks = Then->LinkedTo;
            UK2Node_ExecutionSequence* Sequence = NewObject<UK2Node_ExecutionSequence>(Graph, TEXT("ProfessorPresentationSequence"), RF_Transactional);
            Graph->AddNode(Sequence, false, false);
            Sequence->CreateNewGuid();
            Sequence->AllocateDefaultPins();
            Sequence->NodePosX = Construct->NodePosX + 300;
            Sequence->NodePosY = Construct->NodePosY;
            Then->BreakAllPinLinks();
            bool bConnected = Schema->TryCreateConnection(Then, Sequence->FindPin(UEdGraphSchema_K2::PN_Execute));
            for (UEdGraphPin* Link : OldLinks) bConnected &= Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(0), Link);
            bConnected &= Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(1), Apply->FindPin(UEdGraphSchema_K2::PN_Execute));
            if (!bConnected) { Error = TEXT("Cannot preserve Construct chain for ") + Blueprint->GetName(); return false; }
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }
}

FString UGradingSetupLibrary::InstallOptions()
{
    using namespace GradingOptionsSetup;
    UClass* OptionsClass = FindObject<UClass>(nullptr, TEXT("/Script/GradingRuntime.GradingOptionsLibrary"));
    UClass* PresentationClass = FindObject<UClass>(nullptr, TEXT("/Script/GradingRuntime.GradingOptionsSettingsLibrary"));
    UFunction* OpenOptions = OptionsClass ? OptionsClass->FindFunctionByName(TEXT("OpenOptions")) : nullptr;
    UFunction* ApplySettings = PresentationClass ? PresentationClass->FindFunctionByName(TEXT("ApplyWidgetSettings")) : nullptr;
    if (!OpenOptions || !ApplySettings) return TEXT("ERROR: Options runtime functions are not loaded.");
    const TCHAR* Paths[] = {
        TEXT("/Game/FirstPersonBP/MainMenu/WBP_MainMenu.WBP_MainMenu"),
        TEXT("/Game/UI/WBP_PauseMenu.WBP_PauseMenu"),
        TEXT("/Game/ComputerDemo/WBP_Desktop.WBP_Desktop"),
        TEXT("/Game/ExamPapers/WBP_ExamPaper.WBP_ExamPaper"),
        TEXT("/Game/ExamPapers/WBP_SelectPaper.WBP_SelectPaper")
    };
    FString Error;
    TArray<UBlueprint*> Blueprints;
    for (const TCHAR* Path : Paths)
    {
        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, Path);
        if (!Blueprint) return FString(TEXT("ERROR: Missing Options target ")) + Path;
        if (!Backup(Blueprint, Error)) return TEXT("ERROR: ") + Error;
        Blueprints.Add(Blueprint);
    }
    for (int32 I = 0; I < Blueprints.Num(); ++I)
    {
        UBlueprint* Blueprint = Blueprints[I];
        if ((I < 2 && !WireOptionClick(Blueprint, OpenOptions, Error)) || !WirePresentation(Blueprint, ApplySettings, Error)) return TEXT("ERROR: ") + Error;
    }
    // Finish all structural edits before compiling dependent widgets. Otherwise
    // a later edit can invalidate an earlier widget's generated/skeleton classes.
    FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();
    for (UBlueprint* Blueprint : Blueprints)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
        if (Log.NumErrors || Blueprint->Status == BS_Error) return TEXT("ERROR: Options Blueprint compile failed: ") + Blueprint->GetName();
    }
    FBlueprintCompilationManager::FlushCompilationQueueAndReinstance();

    TArray<UObject*> ChangedAudio;
    USoundClass* Master = CreateAsset<USoundClass>(TEXT("/Game/Settings/SC_Master"), TEXT("SC_Master"), ChangedAudio);
    USoundClass* SFX = CreateAsset<USoundClass>(TEXT("/Game/Settings/SC_SFX"), TEXT("SC_SFX"), ChangedAudio);
    USoundClass* BGM = CreateAsset<USoundClass>(TEXT("/Game/Settings/SC_BGM"), TEXT("SC_BGM"), ChangedAudio);
    USoundMix* Mix = CreateAsset<USoundMix>(TEXT("/Game/Settings/SM_UserSettings"), TEXT("SM_UserSettings"), ChangedAudio);
    UObject* AudioDefinitions[] = { Master, SFX, BGM, Mix };
    for (UObject* Audio : AudioDefinitions) if (!Backup(Audio, Error)) return TEXT("ERROR: ") + Error;
    for (USoundClass* Child : { SFX, BGM })
    {
        if (Child->ParentClass != Master)
        {
            if (USoundClass* PreviousParent = Child->ParentClass)
            {
                if (!Backup(PreviousParent, Error)) return TEXT("ERROR: ") + Error;
                ChangedAudio.AddUnique(PreviousParent);
            }
            Child->SetParentClass(Master);
            ChangedAudio.AddUnique(Child);
        }
        // SetParentClass only removes the old parent's child link in UE4.27.
        // AudioDevice traverses ChildClasses when applying a master override.
        // Repair this side even on reruns where ParentClass already matches.
        if (!Master->ChildClasses.Contains(Child))
        {
            Master->Modify();
            Master->ChildClasses.AddUnique(Child);
            ChangedAudio.AddUnique(Master);
        }
    }
    if (ChangedAudio.Contains(Mix)) { Mix->Duration = -1.f; Mix->FadeInTime = 0.05f; Mix->FadeOutTime = 0.05f; }
    FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Sounds;
    Registry.Get().GetAssetsByClass(USoundBase::StaticClass()->GetFName(), Sounds, true);
    const TSet<FString> MusicPaths = {
        TEXT("/Game/FirstPersonBP/Sound/background_music"),
        TEXT("/Game/FirstPersonBP/Sound/background_music_Cue"),
        TEXT("/Game/StarterContent/Audio/Starter_Music01"),
        TEXT("/Game/StarterContent/Audio/Starter_Music_Cue")
    };
    int32 RoutedSounds = 0;
    for (const FAssetData& Data : Sounds)
    {
        if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/"))) continue;
        USoundBase* Sound = Cast<USoundBase>(Data.GetAsset());
        if (!Sound) return TEXT("ERROR: Could not load sound for Options routing: ") + Data.ObjectPath.ToString();
        USoundClass* Desired = MusicPaths.Contains(Data.PackageName.ToString()) ? BGM : SFX;
        if (Sound->SoundClassObject != Desired)
        {
            if (!Backup(Sound, Error)) return TEXT("ERROR: ") + Error;
            Sound->Modify();
            Sound->SoundClassObject = Desired;
            ChangedAudio.AddUnique(Sound);
        }
        ++RoutedSounds;
    }
    for (UObject* Audio : ChangedAudio) if (!Save(Audio, Error)) return TEXT("ERROR: ") + Error;
    for (UBlueprint* Blueprint : Blueprints) if (!Save(Blueprint, Error)) return TEXT("ERROR: ") + Error;
    return FString::Printf(TEXT("OK: Main and pause Options buttons wired; 5 widget presentation hooks compiled/saved; %d sounds routed through Master/SFX/BGM. Backups: Saved/OptionsMigration."), RoutedSounds);
}
