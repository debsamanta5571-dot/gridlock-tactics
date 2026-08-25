// Gridlock Tactics.
//
// Pulls in the shared tactics rules C++ from ../../../cpp_core (same sources as cpp_core/CMakeLists.txt
// add_library(tactics_core ...)). When you add/remove .cpp files there, add/remove the matching
// CppCoreStub_*.cpp shim under Private/ (each shim is one #include of the real .cpp), e.g. CppCoreStub_ability_catalog.cpp.

using System.IO;
using UnrealBuildTool;

public class TacticsCore : ModuleRules
{
	public TacticsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = Path.Combine(ModuleDirectory, "Private", "TacticsCorePrivatePCH.h");
		// Unity enabled. CppCoreStub_*.cpp each #include one cpp_core .cpp; those sources use anonymous
		// namespaces that collide if merged. Keep one stub per unity blob via a tiny byte budget + stub padding
		// (see scripts/generate_cpp_core_stubs.py). TacticsGameUnreal merges many Slate files per unity TU.
		NumIncludedBytesPerUnityCPPOverride = 64;
		bUseRTTI = true; // tactics_core uses std::dynamic_pointer_cast (MSVC /GR)
		bEnableExceptions = true; // cpp_core + subsystem code uses try/catch (MSVC /EHsc)
		CppStandard = CppStandardVersion.Cpp20;

		// CoreUObject + Engine: UTacticsMatchSubsystem (UGameInstanceSubsystem), FString / logging.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		string CoreRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "cpp_core"));
		PublicIncludePaths.Add(Path.Combine(CoreRoot, "include"));
		PublicIncludePaths.Add(Path.Combine(CoreRoot, "third_party"));
		PrivateIncludePaths.Add(Path.Combine(CoreRoot, "src"));
	}
}
