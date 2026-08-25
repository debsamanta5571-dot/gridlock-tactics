#include "Tactics3DBoardViewPawn.h"

#include "Tactics3DBoardGameMode.h"
#include "Tactics3DWorldBoardActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "TimerManager.h"

ATactics3DBoardViewPawn::ATactics3DBoardViewPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(Root);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 2200.f;
	// Fixed top-down rig; grid X/Y vs screen is FTacticsBoardViewMapping (do not rotate yaw to fix the map).
	SpringArm->SetRelativeRotation(FRotator(-89.5f, 0.f, 0.f));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

bool ATactics3DBoardViewPawn::AlignViewToBoard(const ATactics3DWorldBoardActor* Board)
{
	if (!Board || !Board->HasVisualGridBounds()) {
		return false;
	}

	const FVector Center = Board->GetBoardWorldCenter();
	// Aim slightly below board center so the duel map sits higher on screen (clears bottom hand UI).
	const FVector ViewNudge = Board->GetActorTransform().TransformVectorNoScale(FVector(-240.f, 0.f, 0.f));
	SetActorLocation(Center + ViewNudge + FVector(0.f, 0.f, 20.f));
	SpringArm->SetRelativeRotation(FRotator(-89.5f, 0.f, 0.f));
	SpringArm->TargetArmLength = Board->GetSuggestedTopDownArmLength();
	return true;
}

void ATactics3DBoardViewPawn::ScheduleAlignRetries()
{
	if (AlignRetryCount >= MaxAlignRetries) {
		return;
	}
	++AlignRetryCount;

	UWorld* W = GetWorld();
	if (!W) {
		return;
	}

	ATactics3DBoardGameMode* GM = Cast<ATactics3DBoardGameMode>(W->GetAuthGameMode());
	if (GM && GM->WorldBoardActor && AlignViewToBoard(GM->WorldBoardActor)) {
		return;
	}

	W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ATactics3DBoardViewPawn::ScheduleAlignRetries));
}

void ATactics3DBoardViewPawn::BeginPlay()
{
	Super::BeginPlay();
	AlignRetryCount = 0;
	ScheduleAlignRetries();
}

float ATactics3DBoardViewPawn::GetPanSpeedScale() const
{
	if (!SpringArm) {
		return 1.f;
	}
	return SpringArm->TargetArmLength / DefaultArmLength;
}

void ATactics3DBoardViewPawn::AdjustZoom(const float WheelDelta)
{
	if (!SpringArm || FMath::IsNearlyZero(WheelDelta)) {
		return;
	}
	SpringArm->TargetArmLength =
		FMath::Clamp(SpringArm->TargetArmLength + WheelDelta * ZoomStepPerWheelNotch, MinArmLength, MaxArmLength);
}

void ATactics3DBoardViewPawn::PanView(const FVector2D Delta, const float DeltaSeconds)
{
	if (Delta.IsNearlyZero() || DeltaSeconds <= 0.f) {
		return;
	}
	const float Speed = PanSpeedUnitsPerSecond * GetPanSpeedScale();
	const FVector WorldDelta(Delta.Y * Speed * DeltaSeconds, Delta.X * Speed * DeltaSeconds, 0.f);
	AddActorWorldOffset(WorldDelta, false);
}

void ATactics3DBoardViewPawn::PanViewByScreenPixels(const FVector2D PixelDelta)
{
	if (PixelDelta.IsNearlyZero()) {
		return;
	}
	const float Scale = GetPanSpeedScale() * MousePanPixelsToWorldScale;
	// Screen Y grows downward; invert so drag-up pans the view up (same axes as WASD PanView).
	const FVector WorldDelta(-PixelDelta.Y * Scale, -PixelDelta.X * Scale, 0.f);
	AddActorWorldOffset(WorldDelta, false);
}
