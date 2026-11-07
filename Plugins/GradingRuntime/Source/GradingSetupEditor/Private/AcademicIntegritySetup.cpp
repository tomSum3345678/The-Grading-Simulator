#include "GradingSetupLibrary.h"
#include "AcademicIntegrityCommandlet.h"

#include "AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/UserDefinedStruct.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetTextLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace AcademicIntegrity
{
    const TCHAR* GameInstancePath = TEXT("/Game/FirstPersonBP/Blueprints/BP_GradingGameInstance.BP_GradingGameInstance");
    const TCHAR* GameInstancePackage = TEXT("/Game/FirstPersonBP/Blueprints/BP_GradingGameInstance");
    const TCHAR* WidgetPath = TEXT("/Game/FirstPersonBP/Blueprints/WBP_AcademicIntegrity.WBP_AcademicIntegrity");
    const TCHAR* WidgetPackage = TEXT("/Game/FirstPersonBP/Blueprints/WBP_AcademicIntegrity");
    const TCHAR* ElevatorPath = TEXT("/Game/FirstPersonBP/Blueprints/BP_ElevatorNextDay.BP_ElevatorNextDay");
    const TCHAR* GameModePath = TEXT("/Game/FirstPersonBP/Blueprints/FirstPersonGameMode.FirstPersonGameMode");
    const TCHAR* StudentsPath = TEXT("/Game/DataTable/DT_Students.DT_Students");
    const TCHAR* GameInstanceClass = TEXT("/Game/FirstPersonBP/Blueprints/BP_GradingGameInstance.BP_GradingGameInstance_C");

    FString PinsOf(const UEdGraphNode* Node)
    {
        FString Text;
        if (!Node) return TEXT("<null>");
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            Text += FString::Printf(TEXT(" [%s %s]"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"), *Pin->PinName.ToString());
        }
        return Node->GetName() + Text;
    }

    bool Link(const UEdGraphSchema* Schema, UEdGraphPin* From, UEdGraphPin* To, const TCHAR* What, FString& Error)
    {
        if (!From || !To)
        {
            Error = FString(What) + TEXT(" is missing a pin. From: ") + PinsOf(From ? From->GetOwningNode() : nullptr) + TEXT(" To: ") + PinsOf(To ? To->GetOwningNode() : nullptr);
            return false;
        }
        if (!Schema->TryCreateConnection(From, To))
        {
            Error = FString(What) + TEXT(" could not be connected. From: ") + PinsOf(From->GetOwningNode()) + TEXT(" To: ") + PinsOf(To->GetOwningNode());
            return false;
        }
        return true;
    }

    UEdGraphPin* Pin(UEdGraphNode* Node, const TCHAR* Name, EEdGraphPinDirection Direction = EGPD_MAX)
    {
        if (!Node) return nullptr;
        for (UEdGraphPin* Candidate : Node->Pins)
        {
            if (Candidate->PinName == Name && (Direction == EGPD_MAX || Candidate->Direction == Direction)) return Candidate;
        }
        return nullptr;
    }

    UEdGraphPin* Prefixed(UEdGraphNode* Node, const TCHAR* Prefix, EEdGraphPinDirection Direction)
    {
        const FString Needle(Prefix);
        for (UEdGraphPin* Candidate : Node->Pins)
        {
            if (Candidate->Direction != Direction) continue;
            const FString Name = Candidate->PinName.ToString();
            if (Name == Needle || (Name.StartsWith(Needle) && Name.Len() > Needle.Len() && Name[Needle.Len()] == TEXT('_'))) return Candidate;
        }
        return nullptr;
    }

    UEdGraphPin* FirstData(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        for (UEdGraphPin* Candidate : Node->Pins)
        {
            if (Candidate->Direction == Direction && Candidate->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) return Candidate;
        }
        return nullptr;
    }

    UEdGraph* EventGraph(UBlueprint* Blueprint)
    {
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph->GetFName() == TEXT("EventGraph")) return Graph;
        }
        return Blueprint->UbergraphPages.Num() ? Blueprint->UbergraphPages[0] : nullptr;
    }

    bool HasFunction(const UBlueprint* Blueprint, const TCHAR* Name)
    {
        for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetFName() == Name) return true;
        }
        return false;
    }

    bool HasVariable(const UBlueprint* Blueprint, const FName& Name)
    {
        for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
        {
            if (Variable.VarName == Name) return true;
        }
        return false;
    }

    bool AddInt(UBlueprint* Blueprint, const TCHAR* Name, const TCHAR* Default, FString& Error)
    {
        if (HasVariable(Blueprint, Name)) return true;
        FEdGraphPinType Type;
        Type.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, Default))
        {
            Error = FString::Printf(TEXT("Cannot add integer variable %s to %s."), Name, *Blueprint->GetName());
            return false;
        }
        return true;
    }

    bool AddBool(UBlueprint* Blueprint, const TCHAR* Name, FString& Error)
    {
        if (HasVariable(Blueprint, Name)) return true;
        FEdGraphPinType Type;
        Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, TEXT("false")))
        {
            Error = FString::Printf(TEXT("Cannot add boolean variable %s to %s."), Name, *Blueprint->GetName());
            return false;
        }
        return true;
    }

    UK2Node_CallFunction* Call(UEdGraph* Graph, UFunction* Function, const TCHAR* Name, int32 X, int32 Y, FString& Error)
    {
        if (!Function)
        {
            Error = FString::Printf(TEXT("Missing function for node %s."), Name);
            return nullptr;
        }
        UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph, Name, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    int32 NodeSerial = 0;

    template<typename T>
    UK2Node_CallFunction* Call(UEdGraph* Graph, const TCHAR* FunctionName, int32 X, int32 Y, FString& Error)
    {
        const FString NodeName = FString::Printf(TEXT("%s_%d"), FunctionName, ++NodeSerial);
        return Call(Graph, T::StaticClass()->FindFunctionByName(FunctionName), *NodeName, X, Y, Error);
    }

    UK2Node_VariableGet* Getter(UEdGraph* Graph, FName Variable, UClass* Owner, int32 X, int32 Y)
    {
        UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        if (Owner) Node->VariableReference.SetExternalMember(Variable, Owner);
        else Node->VariableReference.SetSelfMember(Variable);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    UK2Node_VariableSet* Setter(UEdGraph* Graph, FName Variable, UClass* Owner, int32 X, int32 Y)
    {
        UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph, NAME_None, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        if (Owner) Node->VariableReference.SetExternalMember(Variable, Owner);
        else Node->VariableReference.SetSelfMember(Variable);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    UK2Node_IfThenElse* Branch(UEdGraph* Graph, const TCHAR* Name, int32 X, int32 Y, const TCHAR* Comment)
    {
        UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(Graph, Name, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        if (Comment && *Comment)
        {
            Node->NodeComment = Comment;
            Node->bCommentBubbleVisible = true;
        }
        return Node;
    }

    UK2Node_DynamicCast* CastTo(UEdGraph* Graph, UClass* Type, const TCHAR* Name, int32 X, int32 Y)
    {
        UK2Node_DynamicCast* Node = NewObject<UK2Node_DynamicCast>(Graph, Name, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->TargetType = Type;
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    UEdGraphPin* CastResult(UK2Node_DynamicCast* Node)
    {
        for (UEdGraphPin* Candidate : Node->Pins)
        {
            if (Candidate->Direction == EGPD_Output && Candidate->PinName.ToString().StartsWith(TEXT("As"))) return Candidate;
        }
        return nullptr;
    }

    UK2Node_MacroInstance* ForEach(UEdGraph* Graph, UEdGraph* Macro, const TCHAR* Name, int32 X, int32 Y, FString& Error)
    {
        if (!Macro)
        {
            Error = TEXT("The ForEachLoop macro is not loaded.");
            return nullptr;
        }
        UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph, Name, RF_Transactional);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->SetMacroGraph(Macro);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        return Node;
    }

    UClass* Generated(UBlueprint* Blueprint)
    {
        return Blueprint ? (Blueprint->GeneratedClass ? *Blueprint->GeneratedClass : *Blueprint->SkeletonGeneratedClass) : nullptr;
    }

    bool Compile(UBlueprint* Blueprint, FString& Error)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        if (Log.NumErrors > 0 || Blueprint->Status == BS_Error)
        {
            Error = TEXT("Blueprint compile failed: ") + Blueprint->GetName();
            int32 Shown = 0;
            for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
            {
                if (Message->GetSeverity() != EMessageSeverity::Error) continue;
                Error += TEXT("\n") + Message->ToText().ToString();
                if (++Shown >= 12) break;
            }
            return false;
        }
        return true;
    }

    bool Save(UObject* Asset, FString& Error)
    {
        UPackage* Package = Asset->GetOutermost();
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            TArray<UObject*> PackageObjects;
            GetObjectsWithPackage(Package, PackageObjects, true);
            for (UObject* Object : PackageObjects)
            {
                if (Object->GetClass()->HasAnyFlags(RF_Transient) && Object->GetClass()->GetName().StartsWith(TEXT("SKEL_")))
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
        if (!UPackage::SavePackage(Package, Asset, RF_Standalone, *Filename, GError, nullptr, false, true, SAVE_NoError))
        {
            Error = TEXT("Cannot save ") + Filename;
            return false;
        }
        return true;
    }

    bool Backup(UObject* Asset, FString& Error)
    {
        const FString PackageName = Asset->GetOutermost()->GetName();
        const FString Source = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().FileExists(*Source)) return true;
        const FString Destination = FPaths::ProjectSavedDir() / TEXT("AcademicIntegrity/Backup") / (PackageName.RightChop(1) + TEXT(".uasset"));
        if (IFileManager::Get().FileExists(*Destination)) return true;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
        if (IFileManager::Get().Copy(*Destination, *Source, false, false) != COPY_OK)
        {
            Error = TEXT("Cannot back up ") + Source;
            return false;
        }
        return true;
    }

    UBlueprint* MakeBlueprint(UClass* Parent, const TCHAR* PackageName, const TCHAR* AssetName, UClass* BlueprintClass, UClass* GeneratedClass, FString& Error)
    {
        UPackage* Package = CreatePackage(PackageName);
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, AssetName, BPTYPE_Normal, BlueprintClass, GeneratedClass, NAME_None);
        if (!Blueprint)
        {
            Error = FString::Printf(TEXT("Cannot create Blueprint %s."), AssetName);
            return nullptr;
        }
        FAssetRegistryModule::AssetCreated(Blueprint);
        return Blueprint;
    }

    bool BuildWidgetTree(UWidgetBlueprint* Widget, FString& Error)
    {
        if (!Widget->WidgetTree)
        {
            Widget->WidgetTree = NewObject<UWidgetTree>(Widget, TEXT("WidgetTree"), RF_Transactional);
        }
        if (Widget->WidgetTree->FindWidget(TEXT("IntegrityBar"))) return true;
        Widget->WidgetTree->RootWidget = nullptr;
        UWidgetTree* Tree = Widget->WidgetTree;
        UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
        Tree->RootWidget = Root;

        USizeBox* Panel = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Panel"));
        Panel->SetWidthOverride(440.f);
        UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
        PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
        PanelSlot->SetAlignment(FVector2D(0.f, 0.f));
        PanelSlot->SetPosition(FVector2D(28.f, 24.f));
        PanelSlot->SetAutoSize(true);

        UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Card"));
        Card->SetBrushColor(FLinearColor(0.035f, 0.04f, 0.05f, 0.9f));
        Card->SetPadding(FMargin(16.f, 12.f));
        Panel->AddChild(Card);

        UVerticalBox* Body = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Body"));
        Card->SetContent(Body);
        UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Header"));
        Body->AddChildToVerticalBox(Header);

        auto MakeText = [&](const TCHAR* Name, const FString& Value, int32 Size, const FLinearColor& Color)
        {
            UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
            Label->SetText(FText::FromString(Value));
            FSlateFontInfo Font = Label->Font;
            Font.Size = Size;
            Label->SetFont(Font);
            Label->SetColorAndOpacity(FSlateColor(Color));
            return Label;
        };

        UTextBlock* Title = MakeText(TEXT("Title"), TEXT("Academic Integrity"), 22, FLinearColor(0.96f, 0.93f, 0.84f));
        UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title);
        TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TitleSlot->SetVerticalAlignment(VAlign_Center);

        UTextBlock* Value = MakeText(TEXT("IntegrityValue"), TEXT("100"), 24, FLinearColor(0.96f, 0.93f, 0.84f));
        Value->bIsVariable = true;
        UHorizontalBoxSlot* ValueSlot = Header->AddChildToHorizontalBox(Value);
        ValueSlot->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
        ValueSlot->SetVerticalAlignment(VAlign_Center);

        UTextBlock* Deduction = MakeText(TEXT("DeductionText"), TEXT(""), 22, FLinearColor(1.f, 0.42f, 0.38f));
        Deduction->bIsVariable = true;
        Deduction->SetVisibility(ESlateVisibility::Collapsed);
        UHorizontalBoxSlot* DeductionSlot = Header->AddChildToHorizontalBox(Deduction);
        DeductionSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
        DeductionSlot->SetVerticalAlignment(VAlign_Center);

        USizeBox* BarBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarBox"));
        BarBox->SetHeightOverride(16.f);
        UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("IntegrityBar"));
        Bar->bIsVariable = true;
        Bar->SetPercent(1.f);
        Bar->SetFillColorAndOpacity(FLinearColor(0.78f, 0.16f, 0.2f, 1.f));
        BarBox->AddChild(Bar);
        UVerticalBoxSlot* BarSlot = Body->AddChildToVerticalBox(BarBox);
        BarSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
        return true;
    }

    bool BuildUpdateFunction(UWidgetBlueprint* Widget, FString& Error)
    {
        if (HasFunction(Widget, TEXT("UpdateIntegrity"))) return true;
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Widget, TEXT("UpdateIntegrity"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Widget, Graph, true, static_cast<UClass*>(nullptr));
        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            Entry = Cast<UK2Node_FunctionEntry>(Node);
            if (Entry) break;
        }
        if (!Entry)
        {
            Error = TEXT("UpdateIntegrity has no function entry.");
            return false;
        }
        FEdGraphPinType IntType;
        IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
        Entry->CreateUserDefinedPin(TEXT("Integrity"), IntType, EGPD_Output, false);
        Entry->CreateUserDefinedPin(TEXT("Deducted"), IntType, EGPD_Output, false);
        const UEdGraphSchema* Schema = Graph->GetSchema();

        UK2Node_CallFunction* ToFloat = Call<UKismetMathLibrary>(Graph, TEXT("Conv_IntToFloat"), 280, 180, Error);
        UK2Node_CallFunction* Divide = Call<UKismetMathLibrary>(Graph, TEXT("Divide_FloatFloat"), 560, 180, Error);
        UK2Node_VariableGet* Bar = Getter(Graph, TEXT("IntegrityBar"), nullptr, 700, 360);
        UK2Node_CallFunction* SetPercent = Call<UProgressBar>(Graph, TEXT("SetPercent"), 880, 0, Error);
        UK2Node_VariableGet* ValueText = Getter(Graph, TEXT("IntegrityValue"), nullptr, 1100, 280);
        UK2Node_CallFunction* ToText = Call<UKismetTextLibrary>(Graph, TEXT("Conv_IntToText"), 1100, 160, Error);
        UK2Node_CallFunction* SetValue = Call<UTextBlock>(Graph, TEXT("SetText"), 1400, 0, Error);
        UK2Node_CallFunction* OverZero = Call<UKismetMathLibrary>(Graph, TEXT("Greater_IntInt"), 1400, 220, Error);
        UK2Node_IfThenElse* Decision = Branch(Graph, TEXT("HasDeduction"), 1680, 0, TEXT("Show the deducted amount only when academic integrity was reduced."));
        UK2Node_CallFunction* LossText = Call<UKismetStringLibrary>(Graph, TEXT("BuildString_Int"), 1960, -80, Error);
        UK2Node_CallFunction* LossAsText = Call<UKismetTextLibrary>(Graph, TEXT("Conv_StringToText"), 2240, -80, Error);
        UK2Node_CallFunction* EmptyAsText = Call<UKismetTextLibrary>(Graph, TEXT("Conv_StringToText"), 1960, 220, Error);
        UK2Node_VariableGet* DeductionTrue = Getter(Graph, TEXT("DeductionText"), nullptr, 2480, -20);
        UK2Node_VariableGet* DeductionFalse = Getter(Graph, TEXT("DeductionText"), nullptr, 2480, 260);
        UK2Node_CallFunction* SetLoss = Call<UTextBlock>(Graph, TEXT("SetText"), 2520, -120, Error);
        UK2Node_CallFunction* ClearLoss = Call<UTextBlock>(Graph, TEXT("SetText"), 2520, 180, Error);
        UK2Node_CallFunction* ShowLoss = Call<UWidget>(Graph, TEXT("SetVisibility"), 2840, -120, Error);
        UK2Node_CallFunction* HideLoss = Call<UWidget>(Graph, TEXT("SetVisibility"), 2840, 180, Error);
        if (!ToFloat || !Divide || !SetPercent || !ToText || !SetValue || !OverZero || !LossText || !LossAsText || !EmptyAsText || !SetLoss || !ClearLoss || !ShowLoss || !HideLoss) return false;

        if (UEdGraphPin* Amount = Pin(Divide, TEXT("B"))) Amount->DefaultValue = TEXT("100.0");
        if (UEdGraphPin* Prefix = Pin(LossText, TEXT("Prefix"))) Prefix->DefaultValue = TEXT("-");
        if (UEdGraphPin* Zero = Pin(OverZero, TEXT("B"))) Zero->DefaultValue = TEXT("0");
        if (UEdGraphPin* Shown = Pin(ShowLoss, TEXT("InVisibility"))) Shown->DefaultValue = TEXT("SelfHitTestInvisible");
        if (UEdGraphPin* Hidden = Pin(HideLoss, TEXT("InVisibility"))) Hidden->DefaultValue = TEXT("Collapsed");

        return Link(Schema, Pin(Entry, TEXT("then")), Pin(SetPercent, TEXT("execute")), TEXT("Update entry"), Error)
            && Link(Schema, Pin(Entry, TEXT("Integrity"), EGPD_Output), Pin(ToFloat, TEXT("InInt")), TEXT("Integrity to float"), Error)
            && Link(Schema, Pin(ToFloat, TEXT("ReturnValue")), Pin(Divide, TEXT("A")), TEXT("Integrity percent"), Error)
            && Link(Schema, Pin(Divide, TEXT("ReturnValue")), Pin(SetPercent, TEXT("InPercent")), TEXT("Bar percent"), Error)
            && Link(Schema, Pin(Bar, TEXT("IntegrityBar")), Pin(SetPercent, TEXT("self")), TEXT("Integrity bar"), Error)
            && Link(Schema, Pin(SetPercent, TEXT("then")), Pin(SetValue, TEXT("execute")), TEXT("Value text"), Error)
            && Link(Schema, Pin(Entry, TEXT("Integrity"), EGPD_Output), Pin(ToText, TEXT("Value")), TEXT("Integrity text"), Error)
            && Link(Schema, Pin(ToText, TEXT("ReturnValue")), Pin(SetValue, TEXT("InText")), TEXT("Value label"), Error)
            && Link(Schema, Pin(ValueText, TEXT("IntegrityValue")), Pin(SetValue, TEXT("self")), TEXT("Value widget"), Error)
            && Link(Schema, Pin(SetValue, TEXT("then")), Pin(Decision, TEXT("execute")), TEXT("Deduction branch"), Error)
            && Link(Schema, Pin(Entry, TEXT("Deducted"), EGPD_Output), Pin(OverZero, TEXT("A")), TEXT("Deducted test"), Error)
            && Link(Schema, Pin(OverZero, TEXT("ReturnValue")), Pin(Decision, TEXT("Condition")), TEXT("Deduction condition"), Error)
            && Link(Schema, Pin(Decision, TEXT("then")), Pin(SetLoss, TEXT("execute")), TEXT("Show loss"), Error)
            && Link(Schema, Pin(Decision, TEXT("else")), Pin(ClearLoss, TEXT("execute")), TEXT("Hide loss"), Error)
            && Link(Schema, Pin(Entry, TEXT("Deducted"), EGPD_Output), Pin(LossText, TEXT("InInt")), TEXT("Loss number"), Error)
            && Link(Schema, Pin(LossText, TEXT("ReturnValue")), Pin(LossAsText, TEXT("InString")), TEXT("Loss string"), Error)
            && Link(Schema, Pin(LossAsText, TEXT("ReturnValue")), Pin(SetLoss, TEXT("InText")), TEXT("Loss label"), Error)
            && Link(Schema, Pin(DeductionTrue, TEXT("DeductionText")), Pin(SetLoss, TEXT("self")), TEXT("Loss widget"), Error)
            && Link(Schema, Pin(EmptyAsText, TEXT("ReturnValue")), Pin(ClearLoss, TEXT("InText")), TEXT("Empty loss"), Error)
            && Link(Schema, Pin(DeductionFalse, TEXT("DeductionText")), Pin(ClearLoss, TEXT("self")), TEXT("Clear widget"), Error)
            && Link(Schema, Pin(SetLoss, TEXT("then")), Pin(ShowLoss, TEXT("execute")), TEXT("Show deduction"), Error)
            && Link(Schema, Pin(ClearLoss, TEXT("then")), Pin(HideLoss, TEXT("execute")), TEXT("Collapse deduction"), Error)
            && Link(Schema, Pin(DeductionTrue, TEXT("DeductionText")), Pin(ShowLoss, TEXT("self")), TEXT("Show target"), Error)
            && Link(Schema, Pin(DeductionFalse, TEXT("DeductionText")), Pin(HideLoss, TEXT("self")), TEXT("Hide target"), Error);
    }

    bool BuildWidgetConstruct(UWidgetBlueprint* Widget, UClass* GameInstanceType, FString& Error)
    {
        UEdGraph* Graph = EventGraph(Widget);
        if (!Graph)
        {
            Error = TEXT("Academic integrity widget has no event graph.");
            return false;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node->GetFName() == TEXT("ReadAcademicIntegrity")) return true;
        }
        UFunction* Update = Widget->SkeletonGeneratedClass ? Widget->SkeletonGeneratedClass->FindFunctionByName(TEXT("UpdateIntegrity")) : nullptr;
        if (!Update && Widget->GeneratedClass) Update = Widget->GeneratedClass->FindFunctionByName(TEXT("UpdateIntegrity"));
        if (!Update)
        {
            Error = TEXT("UpdateIntegrity is not on the widget skeleton yet.");
            return false;
        }
        const UEdGraphSchema* Schema = Graph->GetSchema();
        UK2Node_Event* Construct = NewObject<UK2Node_Event>(Graph, TEXT("ReadAcademicIntegrity"), RF_Transactional);
        Graph->AddNode(Construct, false, false);
        Construct->CreateNewGuid();
        Construct->EventReference.SetExternalMember(TEXT("Construct"), UUserWidget::StaticClass());
        Construct->bOverrideFunction = true;
        Construct->AllocateDefaultPins();
        Construct->NodePosX = 0;
        Construct->NodePosY = 0;

        UK2Node_CallFunction* GetInstance = Call<UGameplayStatics>(Graph, TEXT("GetGameInstance"), 320, 160, Error);
        UK2Node_DynamicCast* Cast = CastTo(Graph, GameInstanceType, TEXT("CastGradingInstance"), 640, 0);
        UK2Node_CallFunction* Apply = Call(Graph, Update, TEXT("ApplyStoredIntegrity"), 1100, 0, Error);
        UK2Node_CallFunction* Fallback = Call(Graph, Update, TEXT("ApplyFullIntegrity"), 1100, 280, Error);
        UK2Node_Self* AppliedSelf = NewObject<UK2Node_Self>(Graph, TEXT("AppliedWidget"), RF_Transactional);
        Graph->AddNode(AppliedSelf, false, false);
        AppliedSelf->CreateNewGuid();
        AppliedSelf->AllocateDefaultPins();
        AppliedSelf->NodePosX = 860;
        AppliedSelf->NodePosY = 180;
        UK2Node_Self* FallbackSelf = NewObject<UK2Node_Self>(Graph, TEXT("FallbackWidget"), RF_Transactional);
        Graph->AddNode(FallbackSelf, false, false);
        FallbackSelf->CreateNewGuid();
        FallbackSelf->AllocateDefaultPins();
        FallbackSelf->NodePosX = 860;
        FallbackSelf->NodePosY = 420;
        if (!GetInstance || !Apply || !Fallback) return false;
        if (UEdGraphPin* Full = Pin(Fallback, TEXT("Integrity"))) Full->DefaultValue = TEXT("100");
        if (UEdGraphPin* None = Pin(Fallback, TEXT("Deducted"))) None->DefaultValue = TEXT("0");

        UK2Node_VariableGet* Integrity = Getter(Graph, TEXT("AcademicIntegrity"), GameInstanceType, 900, -80);
        UK2Node_VariableGet* Deducted = Getter(Graph, TEXT("LastDeduction"), GameInstanceType, 900, 80);
        UEdGraphPin* Instance = CastResult(Cast);
        return Link(Schema, Pin(Construct, TEXT("then")), Pin(Cast, TEXT("execute")), TEXT("Construct cast"), Error)
            && Link(Schema, Pin(GetInstance, TEXT("ReturnValue")), Pin(Cast, TEXT("Object")), TEXT("Game instance"), Error)
            && Link(Schema, Pin(Cast, TEXT("then")), Pin(Apply, TEXT("execute")), TEXT("Apply stored integrity"), Error)
            && Link(Schema, Pin(Cast, TEXT("CastFailed")), Pin(Fallback, TEXT("execute")), TEXT("Fallback integrity"), Error)
            && Link(Schema, Instance, Pin(Integrity, TEXT("self")), TEXT("Stored integrity target"), Error)
            && Link(Schema, Instance, Pin(Deducted, TEXT("self")), TEXT("Stored deduction target"), Error)
            && Link(Schema, Pin(Integrity, TEXT("AcademicIntegrity")), Pin(Apply, TEXT("Integrity")), TEXT("Stored integrity"), Error)
            && Link(Schema, Pin(Deducted, TEXT("LastDeduction")), Pin(Apply, TEXT("Deducted")), TEXT("Stored deduction"), Error)
            && Link(Schema, Pin(AppliedSelf, TEXT("self")), Pin(Apply, TEXT("self")), TEXT("Apply self"), Error)
            && Link(Schema, Pin(FallbackSelf, TEXT("self")), Pin(Fallback, TEXT("self")), TEXT("Fallback self"), Error);
    }

    UK2Node_CallFunction* AddHud(UEdGraph* Graph, UClass* WidgetClass, int32 X, int32 Y, FString& Error)
    {
        const UEdGraphSchema* Schema = Graph->GetSchema();
        UK2Node_CallFunction* Player = Call<UGameplayStatics>(Graph, TEXT("GetPlayerController"), X, Y + 180, Error);
        UK2Node_CallFunction* Create = Call<UWidgetBlueprintLibrary>(Graph, TEXT("Create"), X + 300, Y, Error);
        UK2Node_CallFunction* Show = Call<UUserWidget>(Graph, TEXT("AddToViewport"), X + 640, Y, Error);
        if (!Player || !Create || !Show) return nullptr;
        if (UEdGraphPin* Index = Pin(Player, TEXT("PlayerIndex"))) Index->DefaultValue = TEXT("0");
        if (UEdGraphPin* Type = Pin(Create, TEXT("WidgetType"))) Type->DefaultObject = WidgetClass;
        if (UEdGraphPin* Order = Pin(Show, TEXT("ZOrder"))) Order->DefaultValue = TEXT("20");
        if (!Link(Schema, Pin(Player, TEXT("ReturnValue")), Pin(Create, TEXT("OwningPlayer")), TEXT("HUD owner"), Error)
            || !Link(Schema, Pin(Create, TEXT("then")), Pin(Show, TEXT("execute")), TEXT("Show HUD"), Error)
            || !Link(Schema, Pin(Create, TEXT("ReturnValue")), Pin(Show, TEXT("self")), TEXT("HUD widget"), Error)) return nullptr;
        return Create;
    }

    bool WireGameMode(UBlueprint* GameMode, UClass* GameInstanceType, UClass* WidgetClass, FString& Error)
    {
        UEdGraph* Graph = EventGraph(GameMode);
        if (!Graph)
        {
            Error = TEXT("Game mode has no event graph.");
            return false;
        }
        UK2Node_IfThenElse* ExistingOffice = nullptr;
        UK2Node_CallFunction* ExistingLevel = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node->GetFName() == TEXT("ShowAcademicIntegrity")) ExistingOffice = Cast<UK2Node_IfThenElse>(Node);
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            if (CallNode && CallNode->GetName().StartsWith(TEXT("GetCurrentLevelName"))) ExistingLevel = CallNode;
        }
        if (ExistingOffice)
        {
            if (!ExistingLevel)
            {
                Error = TEXT("The academic integrity graph has no GetCurrentLevelName node.");
                return false;
            }
            UEdGraphPin* LevelThen = Pin(ExistingLevel, TEXT("then"));
            const bool bFeedsOffice = LevelThen && LevelThen->LinkedTo.ContainsByPredicate([ExistingOffice](const UEdGraphPin* Link) { return Link->GetOwningNode() == ExistingOffice; });
            if (bFeedsOffice) return true;
            UEdGraphPin* LevelExec = Pin(ExistingLevel, TEXT("execute"));
            UEdGraphPin* OfficeExec = Pin(ExistingOffice, TEXT("execute"));
            TArray<UEdGraphPin*> Incoming = OfficeExec ? OfficeExec->LinkedTo : TArray<UEdGraphPin*>();
            if (!LevelExec || !OfficeExec || Incoming.Num() == 0)
            {
                Error = TEXT("The academic integrity branch is not connected to BeginPlay. Level: ") + PinsOf(ExistingLevel) + TEXT(" Branch: ") + PinsOf(ExistingOffice);
                return false;
            }
            const UEdGraphSchema* Schema = Graph->GetSchema();
            OfficeExec->BreakAllPinLinks(true);
            bool bRepaired = true;
            for (UEdGraphPin* Source : Incoming) bRepaired &= Link(Schema, Source, LevelExec, TEXT("Level name execution"), Error);
            return bRepaired && Link(Schema, LevelThen, OfficeExec, TEXT("Level name result"), Error);
        }
        UK2Node_Event* Begin = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
            if (Event && Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay")) Begin = Event;
        }
        if (!Begin)
        {
            Error = TEXT("Game mode has no BeginPlay event.");
            return false;
        }
        UEdGraphPin* Tail = Pin(Begin, TEXT("then"));
        TSet<const UEdGraphNode*> Seen;
        while (Tail && Tail->LinkedTo.Num() > 0)
        {
            const UEdGraphNode* Next = Tail->LinkedTo[0]->GetOwningNode();
            if (Seen.Contains(Next)) break;
            Seen.Add(Next);
            UEdGraphPin* NextThen = Next->FindPin(UEdGraphSchema_K2::PN_Then);
            if (!NextThen) break;
            Tail = NextThen;
        }
        const UEdGraphSchema* Schema = Graph->GetSchema();
        const int32 X = Begin->NodePosX + 700;
        const int32 Y = Begin->NodePosY + 500;
        UK2Node_CallFunction* Level = Call<UGameplayStatics>(Graph, TEXT("GetCurrentLevelName"), X, Y + 160, Error);
        UK2Node_CallFunction* IsOffice = Call<UKismetStringLibrary>(Graph, TEXT("EqualEqual_StrStr"), X + 300, Y + 160, Error);
        UK2Node_CallFunction* IsDay2 = Call<UKismetStringLibrary>(Graph, TEXT("EqualEqual_StrStr"), X + 300, Y + 420, Error);
        UK2Node_IfThenElse* Office = Branch(Graph, TEXT("ShowAcademicIntegrity"), X + 560, Y, TEXT("The academic integrity bar is shown in the office and on Day 2."));
        UK2Node_IfThenElse* Day2 = Branch(Graph, TEXT("ShowDay2Integrity"), X + 560, Y + 320, nullptr);
        if (!Level || !IsOffice || !IsDay2) return false;
        if (UEdGraphPin* Name = Pin(IsOffice, TEXT("B"))) Name->DefaultValue = TEXT("MyLevel");
        if (UEdGraphPin* Name = Pin(IsDay2, TEXT("B"))) Name->DefaultValue = TEXT("Day2");

        UK2Node_CallFunction* Instance = Call<UGameplayStatics>(Graph, TEXT("GetGameInstance"), X + 900, Y + 180, Error);
        UK2Node_DynamicCast* Cast = CastTo(Graph, GameInstanceType, TEXT("CastIntegrityInstance"), X + 1180, Y);
        UK2Node_VariableSet* ResetValue = Setter(Graph, TEXT("AcademicIntegrity"), GameInstanceType, X + 1560, Y);
        UK2Node_VariableSet* ResetLoss = Setter(Graph, TEXT("LastDeduction"), GameInstanceType, X + 1860, Y);
        UK2Node_VariableSet* ResetFlag = Setter(Graph, TEXT("bIntegritySettled"), GameInstanceType, X + 2160, Y);
        if (!Instance || !ResetValue || !ResetLoss || !ResetFlag) return false;
        if (UEdGraphPin* Value = Pin(ResetValue, TEXT("AcademicIntegrity"), EGPD_Input)) Value->DefaultValue = TEXT("100");
        if (UEdGraphPin* Value = Pin(ResetLoss, TEXT("LastDeduction"), EGPD_Input)) Value->DefaultValue = TEXT("0");
        if (UEdGraphPin* Value = Pin(ResetFlag, TEXT("bIntegritySettled"), EGPD_Input)) Value->DefaultValue = TEXT("false");
        UEdGraphPin* InstancePin = CastResult(Cast);
        UK2Node_CallFunction* OfficeHud = AddHud(Graph, WidgetClass, X + 2500, Y, Error);
        UK2Node_CallFunction* FailedHud = AddHud(Graph, WidgetClass, X + 1560, Y + 260, Error);
        UK2Node_CallFunction* DayHud = AddHud(Graph, WidgetClass, X + 900, Y + 320, Error);
        if (!OfficeHud || !FailedHud || !DayHud) return false;

        return Link(Schema, Tail, Pin(Level, TEXT("execute")), TEXT("BeginPlay level name"), Error)
            && Link(Schema, Pin(Level, TEXT("then")), Pin(Office, TEXT("execute")), TEXT("BeginPlay integrity"), Error)
            && Link(Schema, Pin(Level, TEXT("ReturnValue")), Pin(IsOffice, TEXT("A")), TEXT("Office level name"), Error)
            && Link(Schema, Pin(Level, TEXT("ReturnValue")), Pin(IsDay2, TEXT("A")), TEXT("Day 2 level name"), Error)
            && Link(Schema, Pin(IsOffice, TEXT("ReturnValue")), Pin(Office, TEXT("Condition")), TEXT("Office condition"), Error)
            && Link(Schema, Pin(Office, TEXT("else")), Pin(Day2, TEXT("execute")), TEXT("Day 2 check"), Error)
            && Link(Schema, Pin(IsDay2, TEXT("ReturnValue")), Pin(Day2, TEXT("Condition")), TEXT("Day 2 condition"), Error)
            && Link(Schema, Pin(Day2, TEXT("then")), Pin(DayHud, TEXT("execute")), TEXT("Day 2 bar"), Error)
            && Link(Schema, Pin(Office, TEXT("then")), Pin(Cast, TEXT("execute")), TEXT("Reset cast"), Error)
            && Link(Schema, Pin(Instance, TEXT("ReturnValue")), Pin(Cast, TEXT("Object")), TEXT("Reset instance"), Error)
            && Link(Schema, Pin(Cast, TEXT("then")), Pin(ResetValue, TEXT("execute")), TEXT("Reset integrity"), Error)
            && Link(Schema, InstancePin, Pin(ResetValue, TEXT("self")), TEXT("Reset value target"), Error)
            && Link(Schema, Pin(ResetValue, TEXT("then")), Pin(ResetLoss, TEXT("execute")), TEXT("Reset deduction"), Error)
            && Link(Schema, InstancePin, Pin(ResetLoss, TEXT("self")), TEXT("Reset loss target"), Error)
            && Link(Schema, Pin(ResetLoss, TEXT("then")), Pin(ResetFlag, TEXT("execute")), TEXT("Reset settled flag"), Error)
            && Link(Schema, InstancePin, Pin(ResetFlag, TEXT("self")), TEXT("Reset flag target"), Error)
            && Link(Schema, Pin(ResetFlag, TEXT("then")), Pin(OfficeHud, TEXT("execute")), TEXT("Office bar"), Error)
            && Link(Schema, Pin(Cast, TEXT("CastFailed")), Pin(FailedHud, TEXT("execute")), TEXT("Office bar without instance"), Error);
    }

    bool BuildSettlement(UBlueprint* Elevator, UDataTable* Students, UScriptStruct* RowStruct, UEdGraph* ForEachMacro, FString& Error)
    {
        if (HasFunction(Elevator, TEXT("SettleAcademicIntegrity"))) return true;
        if (!AddInt(Elevator, TEXT("IntegrityLoss"), TEXT("0"), Error)) return false;
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Elevator, TEXT("SettleAcademicIntegrity"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Elevator, Graph, true, static_cast<UClass*>(nullptr));
        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            Entry = Cast<UK2Node_FunctionEntry>(Node);
            if (Entry) break;
        }
        if (!Entry)
        {
            Error = TEXT("SettleAcademicIntegrity has no function entry.");
            return false;
        }
        const UEdGraphSchema* Schema = Graph->GetSchema();
        UK2Node_VariableSet* Clear = Setter(Graph, TEXT("IntegrityLoss"), nullptr, 280, 0);
        UK2Node_CallFunction* Names = Call<UDataTableFunctionLibrary>(Graph, TEXT("GetDataTableRowNames"), 620, 0, Error);
        UK2Node_MacroInstance* Loop = ForEach(Graph, ForEachMacro, TEXT("EachStudent"), 980, 0, Error);
        if (!Names || !Loop) return false;
        if (UEdGraphPin* Table = Pin(Names, TEXT("Table"))) Table->DefaultObject = Students;
        if (UEdGraphPin* Zero = Pin(Clear, TEXT("IntegrityLoss"), EGPD_Input)) Zero->DefaultValue = TEXT("0");
        if (!Link(Schema, Pin(Entry, TEXT("then")), Pin(Clear, TEXT("execute")), TEXT("Clear integrity loss"), Error)
            || !Link(Schema, Pin(Clear, TEXT("then")), Pin(Names, TEXT("execute")), TEXT("Read student rows"), Error)
            || !Link(Schema, Pin(Names, TEXT("OutRowNames")), Pin(Loop, TEXT("Array")), TEXT("Student row array"), Error)) return false;

        UK2Node_GetDataTableRow* Row = NewObject<UK2Node_GetDataTableRow>(Graph, TEXT("ReadStudentRow"), RF_Transactional);
        Graph->AddNode(Row, false, false);
        Row->CreateNewGuid();
        Row->AllocateDefaultPins();
        Row->NodePosX = 1400;
        Row->NodePosY = 0;
        if (UEdGraphPin* Table = Row->GetDataTablePin())
        {
            Table->DefaultObject = Students;
            Row->PinDefaultValueChanged(Table);
        }
        UK2Node_BreakStruct* Fields = NewObject<UK2Node_BreakStruct>(Graph, TEXT("BreakStudent"), RF_Transactional);
        Graph->AddNode(Fields, false, false);
        Fields->CreateNewGuid();
        Fields->StructType = RowStruct;
        Fields->bMadeAfterOverridePinRemoval = true;
        Fields->AllocateDefaultPins();
        Fields->NodePosX = 1760;
        Fields->NodePosY = 0;

        UK2Node_IfThenElse* HasScore = Branch(Graph, TEXT("HasExamScore"), 2140, 0, TEXT("A blank exam score is not judged."));
        UK2Node_CallFunction* AtLeastZero = Call<UKismetMathLibrary>(Graph, TEXT("GreaterEqual_IntInt"), 2140, 220, Error);
        UK2Node_IfThenElse* HasExpected = Branch(Graph, TEXT("HasExpectedScore"), 2460, 0, TEXT("ExpectedScore outside 0 to 100 is not a marking standard."));
        UK2Node_CallFunction* AtMostHundred = Call<UKismetMathLibrary>(Graph, TEXT("LessEqual_IntInt"), 2460, 220, Error);
        UK2Node_IfThenElse* ExpectedRange = Branch(Graph, TEXT("ExpectedScoreInRange"), 2780, 0, nullptr);
        UK2Node_CallFunction* Difference = Call<UKismetMathLibrary>(Graph, TEXT("Subtract_IntInt"), 2780, 240, Error);
        UK2Node_CallFunction* Absolute = Call<UKismetMathLibrary>(Graph, TEXT("Abs_Int"), 3060, 240, Error);
        UK2Node_CallFunction* Beyond = Call<UKismetMathLibrary>(Graph, TEXT("Greater_IntInt"), 3340, 240, Error);
        UK2Node_IfThenElse* Judge = Branch(Graph, TEXT("JudgeExpectedGap"), 3620, 0, TEXT("More than 15 points from ExpectedScore deducts only the excess. 15 points or fewer deducts nothing."));
        UK2Node_CallFunction* Excess = Call<UKismetMathLibrary>(Graph, TEXT("Subtract_IntInt"), 3940, 180, Error);
        UK2Node_VariableGet* Current = Getter(Graph, TEXT("IntegrityLoss"), nullptr, 3940, 360);
        UK2Node_CallFunction* Total = Call<UKismetMathLibrary>(Graph, TEXT("Add_IntInt"), 4220, 240, Error);
        UK2Node_VariableSet* Store = Setter(Graph, TEXT("IntegrityLoss"), nullptr, 4500, 0);
        if (!AtLeastZero || !AtMostHundred || !Difference || !Absolute || !Beyond || !Excess || !Total) return false;
        if (UEdGraphPin* Limit = Pin(AtLeastZero, TEXT("B"))) Limit->DefaultValue = TEXT("0");
        if (UEdGraphPin* Limit = Pin(AtMostHundred, TEXT("B"))) Limit->DefaultValue = TEXT("100");
        if (UEdGraphPin* Limit = Pin(Beyond, TEXT("B"))) Limit->DefaultValue = TEXT("15");
        if (UEdGraphPin* Limit = Pin(Excess, TEXT("B"))) Limit->DefaultValue = TEXT("15");

        UEdGraphPin* Expected = Prefixed(Fields, TEXT("ExpectedScore"), EGPD_Output);
        UEdGraphPin* Exam = Prefixed(Fields, TEXT("ExamScore"), EGPD_Output);
        UEdGraphPin* HasExam = Prefixed(Fields, TEXT("HasOEA"), EGPD_Output);
        if (!Expected || !Exam || !HasExam)
        {
            Error = TEXT("Student row is missing ExpectedScore, ExamScore or HasOEA. Pins:") + PinsOf(Fields);
            return false;
        }
        return Link(Schema, Pin(Loop, TEXT("LoopBody")), Pin(Row, TEXT("execute")), TEXT("Loop body"), Error)
            && Link(Schema, Pin(Loop, TEXT("Array Element")), Row->GetRowNamePin(), TEXT("Student row name"), Error)
            && Link(Schema, Row->GetThenPin(), Pin(HasScore, TEXT("execute")), TEXT("Row found"), Error)
            && Link(Schema, Row->GetResultPin(), FirstData(Fields, EGPD_Input), TEXT("Break student"), Error)
            && Link(Schema, HasExam, Pin(HasScore, TEXT("Condition")), TEXT("Has exam score"), Error)
            && Link(Schema, Pin(HasScore, TEXT("then")), Pin(HasExpected, TEXT("execute")), TEXT("Check expected score"), Error)
            && Link(Schema, Expected, Pin(AtLeastZero, TEXT("A")), TEXT("Expected lower bound"), Error)
            && Link(Schema, Pin(AtLeastZero, TEXT("ReturnValue")), Pin(HasExpected, TEXT("Condition")), TEXT("Expected exists"), Error)
            && Link(Schema, Pin(HasExpected, TEXT("then")), Pin(ExpectedRange, TEXT("execute")), TEXT("Check expected range"), Error)
            && Link(Schema, Expected, Pin(AtMostHundred, TEXT("A")), TEXT("Expected upper bound"), Error)
            && Link(Schema, Pin(AtMostHundred, TEXT("ReturnValue")), Pin(ExpectedRange, TEXT("Condition")), TEXT("Expected range"), Error)
            && Link(Schema, Pin(ExpectedRange, TEXT("then")), Pin(Judge, TEXT("execute")), TEXT("Judge the gap"), Error)
            && Link(Schema, Exam, Pin(Difference, TEXT("A")), TEXT("Entered exam score"), Error)
            && Link(Schema, Expected, Pin(Difference, TEXT("B")), TEXT("Expected score"), Error)
            && Link(Schema, Pin(Difference, TEXT("ReturnValue")), Pin(Absolute, TEXT("A")), TEXT("Absolute gap"), Error)
            && Link(Schema, Pin(Absolute, TEXT("ReturnValue")), Pin(Beyond, TEXT("A")), TEXT("Gap test"), Error)
            && Link(Schema, Pin(Beyond, TEXT("ReturnValue")), Pin(Judge, TEXT("Condition")), TEXT("Fifteen point condition"), Error)
            && Link(Schema, Pin(Judge, TEXT("then")), Pin(Store, TEXT("execute")), TEXT("Store excess"), Error)
            && Link(Schema, Pin(Absolute, TEXT("ReturnValue")), Pin(Excess, TEXT("A")), TEXT("Excess source"), Error)
            && Link(Schema, Pin(Excess, TEXT("ReturnValue")), Pin(Total, TEXT("B")), TEXT("Excess amount"), Error)
            && Link(Schema, Pin(Current, TEXT("IntegrityLoss")), Pin(Total, TEXT("A")), TEXT("Running loss"), Error)
            && Link(Schema, Pin(Total, TEXT("ReturnValue")), Pin(Store, TEXT("IntegrityLoss"), EGPD_Input), TEXT("Updated loss"), Error);
    }

    UK2Node_CallFunction* CopyOpenLevel(UEdGraph* Graph, UK2Node_CallFunction* Source, const TCHAR* Name, int32 X, int32 Y, FString& Error)
    {
        UK2Node_CallFunction* Copy = Call(Graph, Source->GetTargetFunction(), Name, X, Y, Error);
        if (!Copy) return nullptr;
        for (UEdGraphPin* From : Source->Pins)
        {
            UEdGraphPin* To = Pin(Copy, *From->PinName.ToString(), From->Direction);
            if (!To || To->LinkedTo.Num() > 0) continue;
            To->DefaultValue = From->DefaultValue;
            To->DefaultObject = From->DefaultObject;
            To->DefaultTextValue = From->DefaultTextValue;
        }
        return Copy;
    }

    bool WireElevator(UBlueprint* Elevator, UClass* GameInstanceType, UClass* WidgetClass, UEdGraph* ForEachMacro, FString& Error)
    {
        UEdGraph* Graph = EventGraph(Elevator);
        if (!Graph)
        {
            Error = TEXT("Elevator has no event graph.");
            return false;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node->GetFName() == TEXT("SettleAcademicIntegrityCall")) return true;
        }
        UK2Node_CallFunction* OpenLevel = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            if (CallNode && CallNode->FunctionReference.GetMemberName() == TEXT("OpenLevel")) OpenLevel = CallNode;
        }
        if (!OpenLevel)
        {
            Error = TEXT("Elevator graph has no OpenLevel node.");
            return false;
        }
        UEdGraphPin* OpenExec = Pin(OpenLevel, TEXT("execute"));
        if (!OpenExec || OpenExec->LinkedTo.Num() == 0)
        {
            Error = TEXT("OpenLevel is not connected. Pins:") + PinsOf(OpenLevel);
            return false;
        }
        TArray<UEdGraphPin*> Incoming = OpenExec->LinkedTo;
        const UEdGraphSchema* Schema = Graph->GetSchema();
        OpenExec->BreakAllPinLinks(true);

        UFunction* Settle = Elevator->SkeletonGeneratedClass ? Elevator->SkeletonGeneratedClass->FindFunctionByName(TEXT("SettleAcademicIntegrity")) : nullptr;
        if (!Settle && Elevator->GeneratedClass) Settle = Elevator->GeneratedClass->FindFunctionByName(TEXT("SettleAcademicIntegrity"));
        UFunction* Update = WidgetClass ? WidgetClass->FindFunctionByName(TEXT("UpdateIntegrity")) : nullptr;
        if (!Settle || !Update)
        {
            Error = TEXT("Settlement or widget update function is not compiled yet.");
            return false;
        }
        const int32 X = OpenLevel->NodePosX;
        const int32 Y = OpenLevel->NodePosY + 700;
        UK2Node_CallFunction* Disable = Call<AActor>(Graph, TEXT("DisableInput"), X, Y, Error);
        UK2Node_CallFunction* Instance = Call<UGameplayStatics>(Graph, TEXT("GetGameInstance"), X + 250, Y + 180, Error);
        UK2Node_DynamicCast* Cast = CastTo(Graph, GameInstanceType, TEXT("CastIntegrityOnTravel"), X + 520, Y);
        UK2Node_VariableGet* Settled = Getter(Graph, TEXT("bIntegritySettled"), GameInstanceType, X + 860, Y + 180);
        UK2Node_CallFunction* NotSettled = Call<UKismetMathLibrary>(Graph, TEXT("Not_PreBool"), X + 1120, Y + 180, Error);
        UK2Node_IfThenElse* FirstTravel = Branch(Graph, TEXT("SettleOnce"), X + 1360, Y, TEXT("Academic integrity is settled once, the first time the elevator leaves for Day 2."));
        UK2Node_CallFunction* Calculate = Call(Graph, Settle, TEXT("SettleAcademicIntegrityCall"), X + 1680, Y, Error);
        UK2Node_Self* ElevatorSelf = NewObject<UK2Node_Self>(Graph, TEXT("SettlementSelf"), RF_Transactional);
        Graph->AddNode(ElevatorSelf, false, false);
        ElevatorSelf->CreateNewGuid();
        ElevatorSelf->AllocateDefaultPins();
        ElevatorSelf->NodePosX = X + 1460;
        ElevatorSelf->NodePosY = Y + 180;
        UK2Node_VariableGet* Loss = Getter(Graph, TEXT("IntegrityLoss"), nullptr, X + 1960, Y + 220);
        UK2Node_VariableGet* Current = Getter(Graph, TEXT("AcademicIntegrity"), GameInstanceType, X + 1960, Y + 80);
        UK2Node_CallFunction* Reduced = Call<UKismetMathLibrary>(Graph, TEXT("Subtract_IntInt"), X + 2240, Y + 140, Error);
        UK2Node_CallFunction* Clamped = Call<UKismetMathLibrary>(Graph, TEXT("Clamp"), X + 2500, Y + 140, Error);
        UK2Node_VariableSet* StoreValue = Setter(Graph, TEXT("AcademicIntegrity"), GameInstanceType, X + 2780, Y);
        UK2Node_VariableSet* StoreLoss = Setter(Graph, TEXT("LastDeduction"), GameInstanceType, X + 3100, Y);
        UK2Node_VariableSet* StoreFlag = Setter(Graph, TEXT("bIntegritySettled"), GameInstanceType, X + 3420, Y);
        UK2Node_CallFunction* Widgets = Call<UWidgetBlueprintLibrary>(Graph, TEXT("GetAllWidgetsOfClass"), X + 3740, Y, Error);
        UK2Node_MacroInstance* Loop = ForEach(Graph, ForEachMacro, TEXT("EachIntegrityBar"), X + 4080, Y, Error);
        if (!Disable || !Instance || !NotSettled || !Calculate || !Reduced || !Clamped || !StoreValue || !StoreLoss || !StoreFlag || !Widgets || !Loop) return false;
        if (UEdGraphPin* Min = Pin(Clamped, TEXT("Min"))) Min->DefaultValue = TEXT("0");
        if (UEdGraphPin* Max = Pin(Clamped, TEXT("Max"))) Max->DefaultValue = TEXT("100");
        if (UEdGraphPin* Flag = Pin(StoreFlag, TEXT("bIntegritySettled"), EGPD_Input)) Flag->DefaultValue = TEXT("true");
        if (UEdGraphPin* Type = Pin(Widgets, TEXT("WidgetClass"))) Type->DefaultObject = WidgetClass;
        UEdGraphPin* InstancePin = CastResult(Cast);

        if (!Link(Schema, Pin(Widgets, TEXT("FoundWidgets")), Pin(Loop, TEXT("Array")), TEXT("Integrity widgets"), Error)) return false;
        UK2Node_DynamicCast* WidgetCast = CastTo(Graph, WidgetClass, TEXT("CastIntegrityWidget"), X + 4460, Y);
        UK2Node_CallFunction* Refresh = Call(Graph, Update, TEXT("RefreshIntegrityBar"), X + 4820, Y, Error);
        UK2Node_CallFunction* Wait = Call<UKismetSystemLibrary>(Graph, TEXT("Delay"), X + 5160, Y, Error);
        UK2Node_CallFunction* AlreadyOpen = CopyOpenLevel(Graph, OpenLevel, TEXT("OpenDay2Immediately"), X + 1680, Y + 360, Error);
        UK2Node_CallFunction* FailedOpen = CopyOpenLevel(Graph, OpenLevel, TEXT("OpenDay2WithoutIntegrity"), X + 900, Y + 420, Error);
        if (!Refresh || !Wait || !AlreadyOpen || !FailedOpen) return false;
        if (UEdGraphPin* Duration = Pin(Wait, TEXT("Duration"))) Duration->DefaultValue = TEXT("3.0");

        bool bIncoming = true;
        for (UEdGraphPin* Source : Incoming) bIncoming &= Link(Schema, Source, Pin(Disable, TEXT("execute")), TEXT("Elevator key to settlement"), Error);
        return bIncoming
            && Link(Schema, Pin(Disable, TEXT("then")), Pin(Cast, TEXT("execute")), TEXT("Read integrity"), Error)
            && Link(Schema, Pin(Instance, TEXT("ReturnValue")), Pin(Cast, TEXT("Object")), TEXT("Travel instance"), Error)
            && Link(Schema, Pin(Cast, TEXT("CastFailed")), Pin(FailedOpen, TEXT("execute")), TEXT("Travel without integrity"), Error)
            && Link(Schema, Pin(Cast, TEXT("then")), Pin(FirstTravel, TEXT("execute")), TEXT("Settlement gate"), Error)
            && Link(Schema, InstancePin, Pin(Settled, TEXT("self")), TEXT("Settled target"), Error)
            && Link(Schema, Pin(Settled, TEXT("bIntegritySettled")), Pin(NotSettled, TEXT("A")), TEXT("Settled value"), Error)
            && Link(Schema, Pin(NotSettled, TEXT("ReturnValue")), Pin(FirstTravel, TEXT("Condition")), TEXT("First travel"), Error)
            && Link(Schema, Pin(FirstTravel, TEXT("else")), Pin(AlreadyOpen, TEXT("execute")), TEXT("Already settled"), Error)
            && Link(Schema, Pin(FirstTravel, TEXT("then")), Pin(Calculate, TEXT("execute")), TEXT("Calculate integrity"), Error)
            && Link(Schema, Pin(ElevatorSelf, TEXT("self")), Pin(Calculate, TEXT("self")), TEXT("Settlement self"), Error)
            && Link(Schema, Pin(Calculate, TEXT("then")), Pin(StoreValue, TEXT("execute")), TEXT("Store remaining integrity"), Error)
            && Link(Schema, Pin(Loss, TEXT("IntegrityLoss")), Pin(Reduced, TEXT("B")), TEXT("Calculated loss"), Error)
            && Link(Schema, InstancePin, Pin(Current, TEXT("self")), TEXT("Current integrity target"), Error)
            && Link(Schema, Pin(Current, TEXT("AcademicIntegrity")), Pin(Reduced, TEXT("A")), TEXT("Current integrity"), Error)
            && Link(Schema, Pin(Reduced, TEXT("ReturnValue")), Pin(Clamped, TEXT("Value")), TEXT("Reduced integrity"), Error)
            && Link(Schema, Pin(Clamped, TEXT("ReturnValue")), Pin(StoreValue, TEXT("AcademicIntegrity"), EGPD_Input), TEXT("Clamped integrity"), Error)
            && Link(Schema, InstancePin, Pin(StoreValue, TEXT("self")), TEXT("Store value target"), Error)
            && Link(Schema, Pin(StoreValue, TEXT("then")), Pin(StoreLoss, TEXT("execute")), TEXT("Store deducted amount"), Error)
            && Link(Schema, Pin(Loss, TEXT("IntegrityLoss")), Pin(StoreLoss, TEXT("LastDeduction"), EGPD_Input), TEXT("Deducted amount"), Error)
            && Link(Schema, InstancePin, Pin(StoreLoss, TEXT("self")), TEXT("Store loss target"), Error)
            && Link(Schema, Pin(StoreLoss, TEXT("then")), Pin(StoreFlag, TEXT("execute")), TEXT("Mark settled"), Error)
            && Link(Schema, InstancePin, Pin(StoreFlag, TEXT("self")), TEXT("Store flag target"), Error)
            && Link(Schema, Pin(StoreFlag, TEXT("then")), Pin(Widgets, TEXT("execute")), TEXT("Find integrity bars"), Error)
            && Link(Schema, Pin(Loop, TEXT("LoopBody")), Pin(WidgetCast, TEXT("execute")), TEXT("Cast integrity bar"), Error)
            && Link(Schema, Pin(Loop, TEXT("Array Element")), Pin(WidgetCast, TEXT("Object")), TEXT("Integrity bar object"), Error)
            && Link(Schema, Pin(WidgetCast, TEXT("then")), Pin(Refresh, TEXT("execute")), TEXT("Refresh integrity bar"), Error)
            && Link(Schema, CastResult(WidgetCast), Pin(Refresh, TEXT("self")), TEXT("Refresh target"), Error)
            && Link(Schema, Pin(StoreValue, TEXT("Output_Get")), Pin(Refresh, TEXT("Integrity")), TEXT("Refresh remaining"), Error)
            && Link(Schema, Pin(StoreLoss, TEXT("Output_Get")), Pin(Refresh, TEXT("Deducted")), TEXT("Refresh deducted"), Error)
            && Link(Schema, Pin(Loop, TEXT("Completed")), Pin(Wait, TEXT("execute")), TEXT("Show the result"), Error)
            && Link(Schema, Pin(Wait, TEXT("then")), OpenExec, TEXT("Open Day 2 after the result"), Error);
    }

    bool UpdateIni(FString& Error)
    {
        const FString Path = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path))
        {
            Error = TEXT("Cannot read DefaultEngine.ini.");
            return false;
        }
        const FString Replacement = FString(TEXT("GameInstanceClass=")) + GameInstanceClass;
        if (Text.Contains(Replacement)) return true;
        const FString Original = TEXT("GameInstanceClass=/Script/Engine.GameInstance");
        if (!Text.Contains(Original))
        {
            Error = TEXT("GameInstanceClass is no longer the engine default, so it was left unchanged.");
            return false;
        }
        Text.ReplaceInline(*Original, *Replacement, ESearchCase::CaseSensitive);
        if (!FFileHelper::SaveStringToFile(Text, *Path))
        {
            Error = TEXT("Cannot write DefaultEngine.ini.");
            return false;
        }
        return true;
    }
}

FString UGradingSetupLibrary::InstallAcademicIntegrity()
{
    using namespace AcademicIntegrity;
    FString Error;
    UDataTable* Students = LoadObject<UDataTable>(nullptr, StudentsPath);
    UBlueprint* Macros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
    UEdGraph* ForEachMacro = nullptr;
    if (Macros)
    {
        for (UEdGraph* Graph : Macros->MacroGraphs)
        {
            if (Graph && Graph->GetFName() == TEXT("ForEachLoop")) ForEachMacro = Graph;
        }
    }
    if (!Students || !Students->GetRowStruct() || !ForEachMacro)
    {
        return TEXT("ERROR: DT_Students or the ForEachLoop macro is unavailable.");
    }

    UBlueprint* Instance = LoadObject<UBlueprint>(nullptr, GameInstancePath);
    const bool bNewInstance = Instance == nullptr;
    if (!Instance)
    {
        Instance = MakeBlueprint(UGameInstance::StaticClass(), GameInstancePackage, TEXT("BP_GradingGameInstance"), UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), Error);
        if (!Instance) return TEXT("ERROR: ") + Error;
    }
    if (!AddInt(Instance, TEXT("AcademicIntegrity"), TEXT("100"), Error) || !AddInt(Instance, TEXT("LastDeduction"), TEXT("0"), Error) || !AddBool(Instance, TEXT("bIntegritySettled"), Error))
    {
        return TEXT("ERROR: ") + Error;
    }
    if (!Compile(Instance, Error) || !Save(Instance, Error)) return TEXT("ERROR: ") + Error;

    UWidgetBlueprint* Widget = LoadObject<UWidgetBlueprint>(nullptr, WidgetPath);
    if (!Widget)
    {
        UBlueprint* Created = MakeBlueprint(UUserWidget::StaticClass(), WidgetPackage, TEXT("WBP_AcademicIntegrity"), UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), Error);
        Widget = Cast<UWidgetBlueprint>(Created);
        if (!Widget) return TEXT("ERROR: ") + Error;
    }
    if (!BuildWidgetTree(Widget, Error) || !Compile(Widget, Error)) return TEXT("ERROR: ") + Error;
    if (!BuildUpdateFunction(Widget, Error) || !Compile(Widget, Error)) return TEXT("ERROR: ") + Error;
    if (!BuildWidgetConstruct(Widget, Generated(Instance), Error) || !Compile(Widget, Error) || !Save(Widget, Error)) return TEXT("ERROR: ") + Error;

    UBlueprint* GameMode = LoadObject<UBlueprint>(nullptr, GameModePath);
    UBlueprint* Elevator = LoadObject<UBlueprint>(nullptr, ElevatorPath);
    if (!GameMode || !Elevator) return TEXT("ERROR: The elevator or game mode Blueprint is missing.");
    if (!Backup(GameMode, Error) || !Backup(Elevator, Error)) return TEXT("ERROR: ") + Error;
    if (!WireGameMode(GameMode, Generated(Instance), Generated(Widget), Error) || !Compile(GameMode, Error) || !Save(GameMode, Error)) return TEXT("ERROR: ") + Error;

    if (!BuildSettlement(Elevator, Students, const_cast<UScriptStruct*>(Students->GetRowStruct()), ForEachMacro, Error)) return TEXT("ERROR: ") + Error;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Elevator);
    if (!Compile(Elevator, Error)) return TEXT("ERROR: ") + Error;
    if (!WireElevator(Elevator, Generated(Instance), Generated(Widget), ForEachMacro, Error) || !Compile(Elevator, Error) || !Save(Elevator, Error)) return TEXT("ERROR: ") + Error;
    if (!UpdateIni(Error)) return TEXT("ERROR: ") + Error;
    return bNewInstance
        ? TEXT("OK: Academic integrity Blueprint bar installed. Restart the Unreal Editor before playing.")
        : TEXT("OK: Academic integrity Blueprint bar is present. Restart the Unreal Editor before playing.");
}

UInstallAcademicIntegrityCommandlet::UInstallAcademicIntegrityCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UInstallAcademicIntegrityCommandlet::Main(const FString& Params)
{
    const FString Result = UGradingSetupLibrary::InstallAcademicIntegrity();
    UE_LOG(LogTemp, Display, TEXT("%s"), *Result);
    return Result.StartsWith(TEXT("OK:")) ? 0 : 1;
}
