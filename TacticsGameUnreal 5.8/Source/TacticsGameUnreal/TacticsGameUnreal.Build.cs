// Gridlock Tactics.

using System.IO;
using UnrealBuildTool;

public class TacticsGameUnreal : ModuleRules
{
	public TacticsGameUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = Path.Combine(ModuleDirectory, "Private", "TacticsGameUnrealPrivatePCH.h");
		// Unity build enabled (UE default). File-local helpers live in anonymous namespaces; cross-file
		// collisions (e.g. kPanelBg, LoadMaterialFromCandidates) are prefixed per translation unit.
		// tactics::Unit/Entity shared_ptr casts use std::dynamic_pointer_cast (MSVC /GR)
		bUseRTTI = true;
		bEnableExceptions = true; // WebSocket/CLI paths use try/catch (MSVC /EHsc)
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "TacticsCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "WebSockets", "Sockets", "Networking", "Json", "JsonUtilities", "Paper2D", "ImageWrapper" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
