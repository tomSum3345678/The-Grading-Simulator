#include "GradingOptionsSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingOptionsRangesTest, "GradingRuntime.Options.RangesAndDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingOptionsRangesTest::RunTest(const FString& Parameters)
{
    const FGradingOptions Defaults;
    TestEqual(TEXT("Master defaults to full volume"), Defaults.MasterVolume, 1.f);
    TestEqual(TEXT("SFX defaults to full volume"), Defaults.SFXVolume, 1.f);
    TestEqual(TEXT("Music defaults to 70 percent"), Defaults.MusicVolume, .7f);
    TestTrue(TEXT("Music is enabled by default"), Defaults.bMusicEnabled);
    TestEqual(TEXT("Mouse sensitivity defaults to one"), Defaults.MouseSensitivity, 1.f);
    TestEqual(TEXT("Text defaults to normal size"), Defaults.TextScale, 1.f);
    TestEqual(TEXT("Exam defaults to normal zoom"), Defaults.ExamZoom, 1.f);
    TestEqual(TEXT("Quality defaults to High"), Defaults.QualityPreset, 2);
    TestTrue(TEXT("Default window mode is borderless"), Defaults.WindowMode == EGradingWindowMode::Borderless);

    FGradingOptions Invalid;
    Invalid.MasterVolume = -1.f;
    Invalid.SFXVolume = 2.f;
    Invalid.MusicVolume = std::numeric_limits<float>::quiet_NaN();
    Invalid.MouseSensitivity = 0.f;
    Invalid.TextScale = 3.f;
    Invalid.ExamZoom = 0.f;
    Invalid.QualityPreset = 10;
    Invalid.WindowMode = static_cast<EGradingWindowMode>(255);
    FGradingOptions Safe = UGradingUserSettings::SanitizeOptions(Invalid);
    TestEqual(TEXT("Master lower bound"), Safe.MasterVolume, 0.f);
    TestEqual(TEXT("SFX upper bound"), Safe.SFXVolume, 1.f);
    TestEqual(TEXT("Invalid music value uses default"), Safe.MusicVolume, .7f);
    TestEqual(TEXT("Sensitivity lower bound"), Safe.MouseSensitivity, .2f);
    TestEqual(TEXT("Text upper bound"), Safe.TextScale, 1.4f);
    TestEqual(TEXT("Exam lower bound"), Safe.ExamZoom, .75f);
    TestEqual(TEXT("Quality upper bound"), Safe.QualityPreset, 2);
    TestTrue(TEXT("Invalid window mode uses borderless"), Safe.WindowMode == EGradingWindowMode::Borderless);
    Invalid.MouseSensitivity = 8.f;
    Invalid.TextScale = 0.f;
    Invalid.ExamZoom = 5.f;
    Invalid.QualityPreset = -1;
    Invalid.MusicVolume = .42f;
    Invalid.bMusicEnabled = false;
    Safe = UGradingUserSettings::SanitizeOptions(Invalid);
    TestEqual(TEXT("Sensitivity upper bound"), Safe.MouseSensitivity, 3.f);
    TestEqual(TEXT("Text lower bound"), Safe.TextScale, .8f);
    TestEqual(TEXT("Exam upper bound"), Safe.ExamZoom, 2.f);
    TestEqual(TEXT("Quality lower bound"), Safe.QualityPreset, 0);
    TestFalse(TEXT("Music toggle remains disabled"), Safe.bMusicEnabled);
    TestEqual(TEXT("Disabling music preserves its volume"), Safe.MusicVolume, .42f);
    Safe.bMusicEnabled = true;
    TestEqual(TEXT("Reenabling music restores the selected volume"), UGradingUserSettings::SanitizeOptions(Safe).MusicVolume, .42f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradingOptionsConfigTest, "GradingRuntime.Options.IsolatedConfigRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGradingOptionsConfigTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("GradingOptions-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini")));
    struct FConfigCleanup
    {
        FString Path;
        explicit FConfigCleanup(const FString& Value) : Path(Value) {}
        ~FConfigCleanup()
        {
            if (GConfig) GConfig->UnloadFile(Path);
            IFileManager::Get().Delete(*Path, false, true, true);
        }
    } Cleanup(Filename);

    // Do not call SetOptions/ApplySettings here: tests must not change the player's real preferences or display.
    UGradingUserSettings* Original = NewObject<UGradingUserSettings>();
    Original->Options.MasterVolume = .51f;
    Original->Options.SFXVolume = .63f;
    Original->Options.MusicVolume = .42f;
    Original->Options.bMusicEnabled = false;
    Original->Options.MouseSensitivity = 1.8f;
    Original->Options.TextScale = 1.2f;
    Original->Options.ExamZoom = 1.7f;
    Original->Options.WindowMode = EGradingWindowMode::Windowed;
    Original->Options.QualityPreset = 1;
    Original->SaveConfig(CPF_Config, *Filename);
    if (!TestTrue(TEXT("Isolated options config is written"), IFileManager::Get().FileExists(*Filename))) return false;
    UGradingUserSettings* Reloaded = NewObject<UGradingUserSettings>();
    Reloaded->LoadConfig(UGradingUserSettings::StaticClass(), *Filename);
    const FGradingOptions Actual = Reloaded->GetOptions();
    TestEqual(TEXT("Master volume persists"), Actual.MasterVolume, .51f);
    TestEqual(TEXT("SFX volume persists"), Actual.SFXVolume, .63f);
    TestEqual(TEXT("Stored music volume persists while disabled"), Actual.MusicVolume, .42f);
    TestFalse(TEXT("Music enabled state persists"), Actual.bMusicEnabled);
    TestEqual(TEXT("Sensitivity persists"), Actual.MouseSensitivity, 1.8f);
    TestEqual(TEXT("Text scale persists"), Actual.TextScale, 1.2f);
    TestEqual(TEXT("Exam zoom persists"), Actual.ExamZoom, 1.7f);
    TestTrue(TEXT("Window mode persists"), Actual.WindowMode == EGradingWindowMode::Windowed);
    TestEqual(TEXT("Quality persists"), Actual.QualityPreset, 1);
    return true;
}
#endif
