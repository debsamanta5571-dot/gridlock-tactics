#include "TacticsMatchSubsystem.h"

#include "tactics/bot/bot_action.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/bot/bot_step.hpp"
#include "tactics/bot/legal_action_generator.hpp"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "Math/UnrealMathUtility.h"

namespace
{
FString BotUtf8ToFString(const std::string& Utf8)
{
	return FString(UTF8_TO_TCHAR(Utf8.c_str()));
}

bool IsBotCardPlayAction(const tactics::bot::BotAction& Action)
{
	using tactics::bot::BotActionKind;
	switch (Action.kind) {
	case BotActionKind::Deploy:
	case BotActionKind::DeployReserve:
	case BotActionKind::CastSpell:
	case BotActionKind::CastSpellReserve:
	case BotActionKind::ActivateAbility:
		return true;
	default:
		return false;
	}
}

bool IsBotMoveAction(const tactics::bot::BotAction& Action)
{
	using tactics::bot::BotActionKind;
	switch (Action.kind) {
	case BotActionKind::MovePreview:
	case BotActionKind::MoveConfirm:
	case BotActionKind::MoveCancel:
	case BotActionKind::MoveRotate:
		return true;
	default:
		return false;
	}
}

bool IsBotManeuverPhase(const tactics::TurnPhase Phase)
{
	return Phase == tactics::TurnPhase::Main || Phase == tactics::TurnPhase::SecondMain
		|| Phase == tactics::TurnPhase::AttackDeclaration || Phase == tactics::TurnPhase::BonusAttackDeclaration;
}
}  // namespace

void UTacticsMatchSubsystem::SetBotAutoPlayEnabled(const bool bEnabled)
{
	bBotAutoPlayEnabled = bEnabled;
	if (bBotAutoPlayEnabled) {
		EnsureBotAutoPlayTicker();
	} else {
		StopBotAutoPlayTicker();
	}
}

bool UTacticsMatchSubsystem::IsBotSeatEnabled(const int32 Seat) const
{
	return BotControlledSeats.Contains(Seat);
}

void UTacticsMatchSubsystem::SetBotSeatEnabled(const int32 Seat, const bool bEnabled)
{
	if (Seat <= 0) {
		return;
	}
	if (bEnabled) {
		BotControlledSeats.Add(Seat);
	} else {
		BotControlledSeats.Remove(Seat);
	}
	if (bBotAutoPlayEnabled && !BotControlledSeats.IsEmpty()) {
		EnsureBotAutoPlayTicker();
	}
}

void UTacticsMatchSubsystem::SetBotPolicyName(const FString& PolicyName)
{
	BotPolicyName = PolicyName.IsEmpty() ? TEXT("mcts") : PolicyName;
	RebuildBotPolicyInstance();
}

void UTacticsMatchSubsystem::SetBotDifficulty(const FString& Difficulty)
{
	const FString Key = Difficulty.TrimStartAndEnd().ToLower();
	BotMctsConfig = tactics::bot::MctsConfig{};
	if (Key == TEXT("easy")) {
		BotDifficulty = TEXT("easy");
		BotPolicyName = TEXT("random");
	} else if (Key == TEXT("hard")) {
		BotDifficulty = TEXT("hard");
		BotPolicyName = TEXT("mcts");
		BotMctsConfig.max_simulations = 24;
		BotMctsConfig.light_max_simulations = 16;
		BotMctsConfig.max_playout_turns = 2;
		BotMctsConfig.max_playout_steps = 64;
		BotMctsConfig.max_branching = 28;
	} else {
		BotDifficulty = TEXT("normal");
		BotPolicyName = TEXT("mcts");
	}
	RebuildBotPolicyInstance();
}

void UTacticsMatchSubsystem::ResetBotRuntimeState()
{
	BotSession = tactics::bot::BotSession{};
	if (Game) {
		if (const auto cp = Game->turn_manager.current_player()) {
			BotSession.controlled_player = *cp;
		}
	}
	RebuildBotPolicyInstance();
	BotRng.seed(static_cast<std::mt19937::result_type>(FPlatformTime::Cycles64()));
	BotNextStepEarliestTime = 0.0;
	BotMovePacingPhase = tactics::TurnPhase::Energy;
	BotMovePacingActingSeat = 0;
	BotMoveActionsThisPhase = 0;
	LastBotActionLog.Reset();
}

void UTacticsMatchSubsystem::RebuildBotPolicyInstance()
{
	tactics::bot::BotPolicyOptions options;
	options.mcts = BotMctsConfig;
	const FTCHARToUTF8 PolicyUtf8(*BotPolicyName);
	BotPolicy = tactics::bot::make_bot_policy(std::string(PolicyUtf8.Get(), PolicyUtf8.Length()), options);
}

bool UTacticsMatchSubsystem::ShouldRunBotAutoStep() const
{
	if (!Game || !bBotAutoPlayEnabled || BotControlledSeats.IsEmpty() || !BotPolicy) {
		return false;
	}
	if (tactics::bot::is_match_over(*Game)) {
		return false;
	}
	if (FPlatformTime::Seconds() < BotNextStepEarliestTime) {
		return false;
	}
	if (ActiveOpponentPlayPresentation.IsSet()) {
		return false;
	}

	if (Game->is_combat_visualization_paused()) {
		return false;
	}

	const std::optional<int> acting = tactics::bot::bot_acting_seat(*Game);
	if (!acting.has_value()) {
		return false;
	}
	return BotControlledSeats.Contains(*acting);
}

void UTacticsMatchSubsystem::ScheduleBotStepAfter(const double DelaySec)
{
	BotNextStepEarliestTime = FPlatformTime::Seconds() + FMath::Max(0.0, DelaySec);
}

void UTacticsMatchSubsystem::SyncBotMovePacingForCurrentPhase()
{
	if (!Game) {
		BotMovePacingPhase = tactics::TurnPhase::Energy;
		BotMovePacingActingSeat = 0;
		BotMoveActionsThisPhase = 0;
		return;
	}

	const tactics::TurnPhase Phase = Game->turn_manager.current_phase;
	const int32 ActingSeat = tactics::bot::bot_acting_seat(*Game).value_or(0);
	if (Phase != BotMovePacingPhase || ActingSeat != BotMovePacingActingSeat) {
		BotMovePacingPhase = Phase;
		BotMovePacingActingSeat = ActingSeat;
		BotMoveActionsThisPhase = 0;
	}
}

bool UTacticsMatchSubsystem::ShouldSkipBotMoveActionsForPacing() const
{
	if (!Game) {
		return false;
	}
	if (!IsBotManeuverPhase(Game->turn_manager.current_phase)) {
		return false;
	}
	return BotMoveActionsThisPhase >= BotMaxMoveActionsPerManeuverPhase;
}

void UTacticsMatchSubsystem::NoteBotMoveActionExecuted()
{
	if (!Game || !IsBotManeuverPhase(Game->turn_manager.current_phase)) {
		return;
	}
	++BotMoveActionsThisPhase;
}

bool UTacticsMatchSubsystem::TryExecuteBotStep(FString& OutMessage)
{
	if (!Game || !BotPolicy) {
		OutMessage = TEXT("No match or bot policy.");
		return false;
	}
	if (!ShouldRunBotAutoStep()) {
		OutMessage = TEXT("Bot: not acting now.");
		return false;
	}

	const int32 ViewingPlayerId = GetLocalViewingPlayerId();
	const size_t QueueSizeBefore = Game->phase_action_queue().size();

	SyncBotMovePacingForCurrentPhase();
	tactics::bot::LegalActionGenLimits Limits;
	if (ShouldSkipBotMoveActionsForPacing()) {
		Limits.skip_move_actions = true;
	}

	const tactics::bot::BotStepResult Step = tactics::bot::step_bot_once(*Game, *BotPolicy, BotSession, BotRng, Limits);

	if (!Step.attempted) {
		OutMessage = TEXT("Bot: nothing to do.");
		return false;
	}
	if (!Step.ok) {
		LastBotActionLog = BotUtf8ToFString(Step.message);
		OutMessage = LastBotActionLog;
		UE_LOG(LogTemp, Warning, TEXT("Bot step failed: %s"), *OutMessage);
		return false;
	}

	const std::string Detail = tactics::bot::format_bot_action_detail(*Game, Step.executed);
	const FString KindName = BotUtf8ToFString(tactics::bot::bot_action_kind_name(Step.executed.kind));
	const FString DetailSuffix = Detail.empty()
		? FString()
		: FString::Printf(TEXT(" | %s"), *BotUtf8ToFString(Detail));
	LastBotActionLog = FString::Printf(TEXT("P%d %s%s"), Step.executed.player_id, *KindName, *DetailSuffix);

	if (IsBotCardPlayAction(Step.executed)) {
		PresentOpponentPlayForAuthorityCommand(Step.executed.player_id, ViewingPlayerId, QueueSizeBefore);
	}

	OutMessage = LastBotActionLog;
	UE_LOG(LogTemp, Log, TEXT("Bot: %s"), *OutMessage);

	if (IsBotMoveAction(Step.executed)) {
		NoteBotMoveActionExecuted();
		ScheduleBotStepAfter(BotMoveStepIntervalSec);
	} else {
		ScheduleBotStepAfter(BotStepIntervalSec);
	}

	BroadcastRefresh();
	return true;
}

void UTacticsMatchSubsystem::EnsureBotAutoPlayTicker()
{
	if (!bBotAutoPlayEnabled || BotControlledSeats.IsEmpty()) {
		return;
	}
	if (BotAutoPlayTickerHandle.IsValid()) {
		return;
	}
	BotAutoPlayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickBotAutoPlay), 0.05f);
}

void UTacticsMatchSubsystem::StopBotAutoPlayTicker()
{
	if (BotAutoPlayTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(BotAutoPlayTickerHandle);
		BotAutoPlayTickerHandle.Reset();
	}
}

bool UTacticsMatchSubsystem::TickBotAutoPlay(float /*DeltaTime*/)
{
	if (!bBotAutoPlayEnabled || BotControlledSeats.IsEmpty()) {
		StopBotAutoPlayTicker();
		return false;
	}
	if (Game && tactics::bot::is_match_over(*Game)) {
		StopBotAutoPlayTicker();
		return false;
	}
	if (ShouldRunBotAutoStep()) {
		FString Msg;
		TryExecuteBotStep(Msg);
	}
	return true;
}