using UnrealBuildTool;

public class ColdLoot : ModuleRules
{
    public ColdLoot(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore" });
    }
}
