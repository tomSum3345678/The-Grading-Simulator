#include "GradingOptionsSettings.h"

#include "GradingPresentation.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace
{
    float SafeRange(float Value, float Minimum, float Maximum, float Default)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Default;
    }

    FInputAxisProperties ReadMouseAxis(UPlayerInput* Input, const FKey& Key)
    {
        FInputAxisProperties Result;
        if (!Input->GetAxisProperties(Key, Result))
        {
            Result.Sensitivity = .07f;
            FInputAxisConfigEntry Entry;
            Entry.AxisKeyName = Key.GetFName();
            Entry.AxisProperties = Result;
            Input->AxisConfig.Add(Entry);
            Input->SetAxisProperties(Key, Result);
        }
        return Result;
    }
}

UGradingUserSettings* UGradingUserSettings::GetGradingSettings()
{
    return GEngine ? Cast<UGradingUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

FGradingOptions UGradingUserSettings::SanitizeOptions(const FGradingOptions& Values)
{
    FGradingOptions Safe = Values;
    Safe.MasterVolume = SafeRange(Safe.MasterVolume, 0.f, 1.f, 1.f);
    Safe.SFXVolume = SafeRange(Safe.SFXVolume, 0.f, 1.f, 1.f);
    Safe.MusicVolume = SafeRange(Safe.MusicVolume, 0.f, 1.f, .7f);
    Safe.MouseSensitivity = SafeRange(Safe.MouseSensitivity, .2f, 3.f, 1.f);
    Safe.TextScale = SafeRange(Safe.TextScale, .8f, 1.4f, 1.f);
    Safe.ExamZoom = SafeRange(Safe.ExamZoom, .75f, 2.f, 1.f);
    Safe.QualityPreset = FMath::Clamp(Safe.QualityPreset, 0, 2);
    if (static_cast<uint8>(Safe.WindowMode) > static_cast<uint8>(EGradingWindowMode::Windowed))
        Safe.WindowMode = EGradingWindowMode::Borderless;
    return Safe;
}

void UGradingUserSettings::SetToDefaults()
{
    Super::SetToDefaults();
    Options = FGradingOptions();
    ApplyDisplayOptions();
}

void UGradingUserSettings::LoadSettings(bool bForceReload)
{
    Super::LoadSettings(bForceReload);
    Options = SanitizeOptions(Options);
}

void UGradingUserSettings::ApplyDisplayOptions()
{
    EWindowMode::Type Mode = EWindowMode::WindowedFullscreen;
    if (Options.WindowMode == EGradingWindowMode::Fullscreen) Mode = EWindowMode::Fullscreen;
    else if (Options.WindowMode == EGradingWindowMode::Windowed) Mode = EWindowMode::Windowed;
    SetFullscreenMode(Mode);
    SetOverallScalabilityLevel(Options.QualityPreset);
}

void UGradingUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
    Options = SanitizeOptions(Options);
    ApplyDisplayOptions();
    Super::ApplySettings(bCheckForCommandLineOverrides);
    OnOptionsChanged.Broadcast(Options);
}

void UGradingUserSettings::SetOptions(const FGradingOptions& Values)
{
    const FGradingOptions Safe = SanitizeOptions(Values);
    const bool bDisplayChanged = Safe.WindowMode != Options.WindowMode || Safe.QualityPreset != Options.QualityPreset;
    Options = Safe;
    if (bDisplayChanged) ApplySettings(false);
    else
    {
        SaveSettings();
        OnOptionsChanged.Broadcast(Options);
    }
}

void UGradingUserSettings::ResetOptions()
{
    // Do not call the base reset: preserve resolution, unrelated engine settings, accounts and grades.
    SetOptions(FGradingOptions());
}

void UGradingOptionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Settings = UGradingUserSettings::GetGradingSettings();
    MasterClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Settings/SC_Master.SC_Master"));
    SFXClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Settings/SC_SFX.SC_SFX"));
    MusicClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Settings/SC_BGM.SC_BGM"));
    if (USoundMix* AuthoredMix = LoadObject<USoundMix>(nullptr, TEXT("/Game/Settings/SM_UserSettings.SM_UserSettings")))
    {
        // SoundMix also has a global fade, independent of each class override.
        // Keep the startup mix immediate without changing the authored asset.
        OptionsMix = DuplicateObject<USoundMix>(AuthoredMix, this);
        OptionsMix->SetFlags(RF_Transient);
        OptionsMix->InitialDelay = 0.f;
        OptionsMix->FadeInTime = 0.f;
    }
    WorldInitializedHandle = FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this,
        [this](UWorld* World, const UWorld::InitializationValues) { HandleWorldReady(World); });
    // PIE may attach its audio device after InitWorld. This second notification
    // still precedes BeginPlay, including music started by the level Blueprint.
    ActorsInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddWeakLambda(this,
        [this](const UWorld::FActorsInitializedParams& Params) { HandleWorldReady(Params.World); });
    if (Settings)
    {
        SettingsHandle = Settings->OnOptionsChanged.AddUObject(this, &UGradingOptionsSubsystem::HandleSettingsChanged);
        Settings->ApplySettings(false);
    }
}

void UGradingOptionsSubsystem::Deinitialize()
{
    FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializedHandle);
    FWorldDelegates::OnWorldInitializedActors.Remove(ActorsInitializedHandle);
    WorldInitializedHandle.Reset();
    ActorsInitializedHandle.Reset();
    if (Settings) Settings->OnOptionsChanged.Remove(SettingsHandle);
    for (const TPair<TWeakObjectPtr<UPlayerInput>, FMouseBaseline>& Pair : MouseBaselines)
    {
        if (UPlayerInput* Input = Pair.Key.Get())
        {
            Input->SetAxisProperties(EKeys::MouseX, Pair.Value.X);
            Input->SetAxisProperties(EKeys::MouseY, Pair.Value.Y);
        }
    }
    MouseBaselines.Empty();
    Settings = nullptr;
    Super::Deinitialize();
}

bool UGradingOptionsSubsystem::IsTickable() const
{
    return !HasAnyFlags(RF_ClassDefaultObject) && Settings != nullptr && GetGameInstance() != nullptr;
}

UWorld* UGradingOptionsSubsystem::GetTickableGameObjectWorld() const
{
    return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

TStatId UGradingOptionsSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UGradingOptionsSubsystem, STATGROUP_Tickables);
}

void UGradingOptionsSubsystem::Tick(float DeltaTime)
{
    TimeUntilRefresh -= DeltaTime;
    if (TimeUntilRefresh <= 0.f)
    {
        TimeUntilRefresh = .25f;
        RefreshWorld();
    }
}

void UGradingOptionsSubsystem::HandleSettingsChanged(const FGradingOptions& Values)
{
    bNeedsAudioRefresh = true;
    TimeUntilRefresh = 0.f;
    RefreshWorld();
}

void UGradingOptionsSubsystem::HandleWorldReady(UWorld* World)
{
    if (!World || !Settings || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance()) return;
    if (bNeedsAudioRefresh || AudioWorld.Get() != World) ApplyAudio(World, Settings->GetOptions());
    TimeUntilRefresh = 0.f;
}

void UGradingOptionsSubsystem::RefreshWorld()
{
    UWorld* World = GetTickableGameObjectWorld();
    if (!World || !Settings || !World->IsGameWorld()) return;
    const FGradingOptions Values = Settings->GetOptions();
    ApplyMouse(World, Values);
    if (bNeedsAudioRefresh || AudioWorld.Get() != World) ApplyAudio(World, Values);
    UGradingOptionsSettingsLibrary::RefreshWorldWidgets(World, Values.TextScale, Values.ExamZoom);
}

void UGradingOptionsSubsystem::ApplyMouse(UWorld* World, const FGradingOptions& Values)
{
    for (auto It = MouseBaselines.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Controller = It->Get();
        if (!Controller || !Controller->IsLocalController() || !Controller->PlayerInput) continue;
        UPlayerInput* Input = Controller->PlayerInput;
        FMouseBaseline* Baseline = MouseBaselines.Find(Input);
        if (!Baseline)
        {
            FMouseBaseline NewBaseline;
            NewBaseline.X = ReadMouseAxis(Input, EKeys::MouseX);
            NewBaseline.Y = ReadMouseAxis(Input, EKeys::MouseY);
            Baseline = &MouseBaselines.Add(Input, NewBaseline);
        }
        if (FMath::IsNearlyEqual(Baseline->AppliedMultiplier, Values.MouseSensitivity)) continue;
        FInputAxisProperties X = Baseline->X;
        FInputAxisProperties Y = Baseline->Y;
        X.Sensitivity *= Values.MouseSensitivity;
        Y.Sensitivity *= Values.MouseSensitivity;
        Input->SetAxisProperties(EKeys::MouseX, X);
        Input->SetAxisProperties(EKeys::MouseY, Y);
        Baseline->AppliedMultiplier = Values.MouseSensitivity;
    }
}

void UGradingOptionsSubsystem::ApplyAudio(UWorld* World, const FGradingOptions& Values)
{
    if (!OptionsMix || !MasterClass || !SFXClass || !MusicClass || !World->GetAudioDeviceRaw()) return;
    // Initial settings must already be effective when the first sound starts.
    const float FadeTime = AudioWorld.Get() == World ? .05f : 0.f;
    UGameplayStatics::SetBaseSoundMix(World, OptionsMix);
    UGameplayStatics::SetSoundMixClassOverride(World, OptionsMix, MasterClass, Values.MasterVolume, 1.f, FadeTime, true);
    UGameplayStatics::SetSoundMixClassOverride(World, OptionsMix, SFXClass, Values.SFXVolume, 1.f, FadeTime, true);
    UGameplayStatics::SetSoundMixClassOverride(World, OptionsMix, MusicClass, Values.bMusicEnabled ? Values.MusicVolume : 0.f, 1.f, FadeTime, true);
    AudioWorld = World;
    bNeedsAudioRefresh = false;
}
