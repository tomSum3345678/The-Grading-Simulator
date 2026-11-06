#include "Modules/ModuleManager.h"
#include "GradingSetupLibrary.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/DataTable.h"

class FGradingSetupEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (!GIsEditor || IsRunningCommandlet()) return;
        Ticker = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGradingSetupEditorModule::Tick), 1.f);
    }

    virtual void ShutdownModule() override
    {
        if (Ticker.IsValid()) FTicker::GetCoreTicker().RemoveTicker(Ticker);
        if (WatchedTable.IsValid()) WatchedTable->OnDataTableChanged().Remove(TableChanged);
    }

private:
    FDelegateHandle Ticker;
    FString LastMessage;
    FString LastNormalizationMessage;
    TWeakObjectPtr<UDataTable> WatchedTable;
    FDelegateHandle TableChanged;
    bool bNormalizeRequested = false;
    bool bNormalizing = false;

    void OnStudentTableChanged()
    {
        if (!bNormalizing) bNormalizeRequested = true;
    }

    void WatchTable()
    {
        UDataTable* Students = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTable/DT_Students.DT_Students"));
        if (Students == WatchedTable.Get()) return;
        if (WatchedTable.IsValid()) WatchedTable->OnDataTableChanged().Remove(TableChanged);
        WatchedTable = Students;
        if (Students)
        {
            TableChanged = Students->OnDataTableChanged().AddRaw(this, &FGradingSetupEditorModule::OnStudentTableChanged);
            bNormalizeRequested = true;
        }
    }

    bool Tick(float DeltaTime)
    {
        if (!GEditor || GEditor->PlayWorld || IsRunningGame()) return true;
        WatchTable();
        const FString Message = UGradingSetupLibrary::SyncSavedGrades();
        if (Message != LastMessage && (Message.StartsWith(TEXT("OK:")) || Message.StartsWith(TEXT("ERROR:")) || Message.StartsWith(TEXT("WAIT:"))))
        {
            UE_LOG(LogTemp, Display, TEXT("Professor grade synchronization: %s"), *Message);
        }
        LastMessage = Message;
        // Property-editor callbacks can still hold a pointer into the old row. Delay
        // key changes until this ticker, then refresh the row list as one operation.
        if (bNormalizeRequested && WatchedTable.IsValid())
        {
            bNormalizeRequested = false;
            bNormalizing = true;
            const FString Normalized = UGradingSetupLibrary::NormalizeStudentRows();
            bNormalizing = false;
            if (Normalized != LastNormalizationMessage && (Normalized.StartsWith(TEXT("OK:")) || Normalized.StartsWith(TEXT("ERROR:"))))
            {
                UE_LOG(LogTemp, Display, TEXT("Professor student row normalization: %s"), *Normalized);
            }
            LastNormalizationMessage = Normalized;
        }
        return true;
    }
};

IMPLEMENT_MODULE(FGradingSetupEditorModule, GradingSetupEditor)
