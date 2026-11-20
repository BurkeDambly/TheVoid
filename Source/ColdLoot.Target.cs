using UnrealBuildTool;

public class ColdLootTarget : TargetRules
{
    public ColdLootTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("ColdLoot");
    }
}
