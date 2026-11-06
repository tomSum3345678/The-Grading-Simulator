#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerInput.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GradingOptionsSettings.generated.h"

class USoundClass;
class USoundMix;

UENUM(BlueprintType)
enum class EGradingWindowMode : uint8
{
    Fullscreen,
    Borderless,
    Windowed
};

USTRUCT(BlueprintType)
struct GRADINGRUNTIME_API FGradingOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MasterVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SFXVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MusicVolume = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bMusicEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MouseSensitivity = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TextScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ExamZoom = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EGradingWindowMode WindowMode = EGradingWindowMode::Borderless;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 QualityPreset = 2;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGradingOptionsChanged, const FGradingOptions&);

/** Options use GameUserSettings.ini; grade SaveGame slots are never touched. */
UCLASS(Config=GameUserSettings, BlueprintType)
class GRADINGRUNTIME_API UGradingUserSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure) static UGradingUserSettings* GetGradingSettings();
    UFUNCTION(BlueprintPure) FGradingOptions GetOptions() const { return Options; }
    UFUNCTION(BlueprintCallable) void SetOptions(const FGradingOptions& Values);
    UFUNCTION(BlueprintCallable) void ResetOptions();
    static FGradingOptions SanitizeOptions(const FGradingOptions& Values);
    virtual void SetToDefaults() override;
    virtual void LoadSettings(bool bForceReload = false) override;
    virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;

    FOnGradingOptionsChanged OnOptionsChanged;

private:
    friend class FGradingOptionsConfigTest;
    UPROPERTY(Config) FGradingOptions Options;
    void ApplyDisplayOptions();
};

/** Reapplies options to new worlds and newly created local player input objects. */
UCLASS()
class GRADINGRUNTIME_API UGradingOptionsSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override;

private:
    struct FMouseBaseline
    {
        FInputAxisProperties X;
        FInputAxisProperties Y;
        float AppliedMultiplier = -1.f;
    };
    TMap<TWeakObjectPtr<UPlayerInput>, FMouseBaseline> MouseBaselines;
    FDelegateHandle SettingsHandle;
    FDelegateHandle WorldInitializedHandle;
    FDelegateHandle ActorsInitializedHandle;
    float TimeUntilRefresh = 0.f;
    bool bNeedsAudioRefresh = true;
    TWeakObjectPtr<UWorld> AudioWorld;
    UPROPERTY() UGradingUserSettings* Settings = nullptr;
    UPROPERTY() USoundClass* MasterClass = nullptr;
    UPROPERTY() USoundClass* SFXClass = nullptr;
    UPROPERTY() USoundClass* MusicClass = nullptr;
    UPROPERTY() USoundMix* OptionsMix = nullptr;

    void HandleSettingsChanged(const FGradingOptions& Values);
    void HandleWorldReady(UWorld* World);
    void RefreshWorld();
    void ApplyMouse(UWorld* World, const FGradingOptions& Values);
    void ApplyAudio(UWorld* World, const FGradingOptions& Values);
};
