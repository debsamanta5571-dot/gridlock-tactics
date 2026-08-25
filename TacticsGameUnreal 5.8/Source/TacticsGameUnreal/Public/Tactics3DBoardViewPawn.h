#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Tactics3DBoardViewPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/** Top-down spring arm camera aimed at the 3D tactics grid. */
UCLASS()
class TACTICSGAMEUNREAL_API ATactics3DBoardViewPawn : public APawn
{
	GENERATED_BODY()

public:
	ATactics3DBoardViewPawn();

	/** Starts aligning the camera to the board once the grid exists. */
	virtual void BeginPlay() override;

	/** Top-down rig centered on the board (returns false until grid bounds exist). */
	bool AlignViewToBoard(const class ATactics3DWorldBoardActor* Board);

	/** Mouse wheel: negative delta zooms in, positive zooms out. */
	void AdjustZoom(float WheelDelta);
	/** Pan the camera rig in world XY (screen-right / screen-up). */
	void PanView(FVector2D Delta, float DeltaSeconds);
	/** Middle-mouse drag: pixel delta -> world offset (grab-the-map feel). */
	void PanViewByScreenPixels(FVector2D PixelDelta);

protected:
	/** Retries camera alignment until the board reports valid bounds. */
	void ScheduleAlignRetries();

	UPROPERTY()
	int32 AlignRetryCount{0};

	static constexpr int32 MaxAlignRetries = 12;
	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Tactics3D")
	TObjectPtr<UCameraComponent> Camera;

	static constexpr float DefaultArmLength = 2200.f;
	static constexpr float MinArmLength = 900.f;
	static constexpr float MaxArmLength = 4800.f;
	static constexpr float ZoomStepPerWheelNotch = 140.f;
	static constexpr float PanSpeedUnitsPerSecond = 920.f;
	static constexpr float MousePanPixelsToWorldScale = 1.35f;

	/** Pan speed relative to the current zoom (arm length). */
	float GetPanSpeedScale() const;
};
