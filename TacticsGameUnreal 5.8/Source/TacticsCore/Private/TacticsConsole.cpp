// Gridlock Tactics.
// Editor / game console commands for the shared tactics CLI (lives in TacticsCore so symbols stay in one DLL).

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "HAL/IConsoleManager.h"
#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/common/match_defaults.hpp"
#include "tactics/core.hpp"

#include <memory>
#include <sstream>
#include <string>

static TUniquePtr<tactics::GameState> GMatchState;
static int GControlledPlayer = 1;
static std::shared_ptr<tactics::Unit> GSelectedUnit;

static void LogUtf8Multiline(const std::string& Utf8)
{
    const FString Lines(UTF8_TO_TCHAR(Utf8.c_str()));
    TArray<FString> Split;
    Lines.ParseIntoArrayLines(Split);
    for (const FString& L : Split) {
        UE_LOG(LogTemp, Log, TEXT("%s"), *L);
    }
}

static FAutoConsoleCommand GCmdTacticsInit(
    TEXT("Tactics.Init"),
    TEXT("Create the demo 8x12 match (two seats P1-P2, demo territories/hands). Run once before Tactics.Exec."),
    FConsoleCommandDelegate::CreateLambda([]() {
        GMatchState = MakeUnique<tactics::GameState>("unreal_console");
        for (int i = 1; i <= tactics::kDefaultDemoSeatCount; ++i) {
            GMatchState->add_player(i, std::string("P") + std::to_string(i));
        }
        GMatchState->start_game();
        tactics::master_cli_seed_demo_state(*GMatchState);
        GControlledPlayer = 1;
        GSelectedUnit.reset();
        UE_LOG(LogTemp, Log, TEXT("Tactics.Init ok. Use Tactics.Help then Tactics.Exec <command> (same tokens as tactics_master_cli)."));
    }));

static FAutoConsoleCommand GCmdTacticsHelp(
    TEXT("Tactics.Help"),
    TEXT("Print the master CLI command list (needs Tactics.Init)."),
    FConsoleCommandDelegate::CreateLambda([]() {
        if (!GMatchState) {
            UE_LOG(LogTemp, Warning, TEXT("Tactics.Help: run Tactics.Init first."));
            return;
        }
        std::ostringstream Oss;
        tactics::master_cli_print_help(*GMatchState, Oss);
        LogUtf8Multiline(Oss.str());
    }));

static FAutoConsoleCommand GCmdTacticsExec(
    TEXT("Tactics.Exec"),
    TEXT("Run one master_cli line, e.g. Tactics.Exec zoneskip   or   Tactics.Exec deploy 1 1 1"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
        if (!GMatchState) {
            UE_LOG(LogTemp, Warning, TEXT("Tactics.Exec: run Tactics.Init first."));
            return;
        }
        if (Args.Num() == 0) {
            UE_LOG(LogTemp, Warning, TEXT("Tactics.Exec: pass a command (same as master_cli), e.g. Tactics.Exec hand"));
            return;
        }
        const FString Joined = FString::Join(Args, TEXT(" "));
        const FTCHARToUTF8 Utf8(*Joined);
        const std::string Line(Utf8.Get(), Utf8.Length());
        std::ostringstream Oss;
        const bool Quit = tactics::dispatch_master_cli_line(*GMatchState, GControlledPlayer, GSelectedUnit, Line, Oss, {});
        if (!Oss.str().empty()) {
            LogUtf8Multiline(Oss.str());
        }
        if (Quit) {
            UE_LOG(LogTemp, Log, TEXT("Tactics.Exec: quit (session kept; run Tactics.Init to reset)."));
        }
    }));
