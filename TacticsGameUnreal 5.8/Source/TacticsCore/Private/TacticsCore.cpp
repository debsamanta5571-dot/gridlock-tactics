// Gridlock Tactics.

#include "TacticsCore.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "tactics/board/board_layout.hpp"
#include "tactics/core.hpp"

class FTacticsCoreModule final : public IModuleInterface
{
public:
	void StartupModule() override
	{
		// Link-time / smoke reference to shared tactics code (headers live under cpp_core/include).
		tactics::GameState Probe("tactics_core_module");
		(void)Probe.board_width();
	}
	void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FTacticsCoreModule, TacticsCore);
