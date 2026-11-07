using UnrealBuildTool;

public class GradingSetupEditor : ModuleRules
{
    public GradingSetupEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "BlueprintGraph", "Kismet", "KismetCompiler", "UMG", "UMGEditor", "GradingRuntime", "AssetRegistry", "Json", "Slate", "SlateCore" });
    }
}
