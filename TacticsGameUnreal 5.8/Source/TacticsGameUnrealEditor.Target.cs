// Gridlock Tactics.

using UnrealBuildTool;
using System.Collections.Generic;

public class TacticsGameUnrealEditorTarget : TargetRules
{
	public TacticsGameUnrealEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bUseUnityBuild = true;

		ExtraModuleNames.AddRange( new string[] { "TacticsGameUnreal" } );
	}
}
