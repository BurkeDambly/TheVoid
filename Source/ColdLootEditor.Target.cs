using UnrealBuildTool;

public class ColdLootEditorTarget : TargetRules
{
    public ColdLootEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("ColdLoot");
    }
}
