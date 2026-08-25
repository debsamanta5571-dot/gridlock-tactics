#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticsMatchSubsystem.h"  // FCombatUnitSnapshot, FCombatEncounter
#include "BattleVisualizationActor.generated.h"

class UCameraComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Self-contained 3D battle visualization stage.
 * Spawned at a world position far from the match board when combat is presented;
 * the active PlayerController targets this actor's camera.  Call PlayEncounter()
 * to populate the stage from an FCombatEncounter.
 *
 * We dynamically build our *own* board grid the same size as the live tactics board
 * (BoardCols × BoardRows checkerboard tiles, centred on the actor root) and place every
 * unit's portrait on the cell matching its real (GridX, GridY) - one cohesive battlefield,
 * a faithful in-scale snapshot of the board state.  The two active combatants stand on
 * their own cells too (rendered taller so they read as the focus) and play the
 * attack→counter choreography *on the board*, lunging toward each other across their cells
 * - there is no separate front-and-centre arena.
 *
 * Portrait planes and board tiles are dynamically created (NewObject + RegisterComponent)
 * and replaced each encounter so the layout matches the live board.  Card art is applied
 * via an unlit/emissive dynamic material so it renders regardless of scene lighting.
 *
 * Layout (all relative to actor root):
 *   Camera     looking +Y, ~-41° pitch, framing the whole board
 *   Board grid centred on (0,0): cell (GridX,GridY) → (cellX, centreY + (row-mid)*cellD, ~-6)
 *   Bystanders placed on the grid at their real (GridX, GridY), shorter portraits
 *   Attacker / Defender on their real cells, taller portraits, lunging toward each other
 *   FloorMesh  (0,    0, -10)  flat XY plane, large, white
 */
UCLASS()
class TACTICSGAMEUNREAL_API ABattleVisualizationActor : public AActor
{
	GENERATED_BODY()

public:
	ABattleVisualizationActor();

	/**
	 * Build the stage from Encounter: place the attacker / defender duel up front,
	 * arrange the remaining units in a back row, and (if bIsPostResolution) play the
	 * attack→counter choreography.  Reusable: called repeatedly per Continue click.
	 */
	void PlayEncounter(const FCombatEncounter& Encounter);

	/**
	 * Configure this actor as ONE side of the split combat view: it renders a single
	 * fighter (the attacker when bAttackerSide=true, else the defender) on its own board
	 * into this actor's render target, which the UI displays side-by-side with the other
	 * side's render target.  Two of these actors (spawned far apart) make the two screens.
	 */
	void ConfigureSide(const FCombatEncounter& Encounter, bool bAttackerSide);

	/** The attacker camera's render target (left UI screen). */
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** The defender camera's render target (right UI screen). */
	UTextureRenderTarget2D* GetRenderTargetB() const { return RenderTargetB; }

	/**
	 * Seed the camera glide START pose from the PREVIOUS battle view's final framing.
	 * The host UI destroys + respawns this actor for every encounter view (pre-roll matchup,
	 * then resolved outcome, then the next combat), so cross-view continuity can only live in
	 * the GameInstance.  It captures the last framing (GetCamFramingA/B) and feeds it back here
	 * before PlayEncounter so the camera travels from the previous battle's pose to the new one
	 * instead of restarting from a synthetic offset.  Must be called BEFORE PlayEncounter.
	 */
	void SetExternalGlideOrigin(const FVector& PosA, const FVector& PosB);

	/**
	 * Seed the camera glide START orientation from the PREVIOUS battle view's final rotation, so the
	 * camera SLERPs from the old orientation to this encounter's (e.g. a vertical→horizontal duel
	 * swings smoothly instead of snapping its yaw).  Same lifecycle as SetExternalGlideOrigin: the
	 * GameInstance captures GetCamRotA/B and feeds it back here BEFORE PlayEncounter.
	 */
	void SetExternalGlideRotation(const FRotator& RotA, const FRotator& RotB);

	/** The attacker camera's final (settled) orientation for the current encounter. */
	FRotator GetCamRotA() const { return CamRotTgt[0]; }

	/** The defender camera's final (settled) orientation for the current encounter. */
	FRotator GetCamRotB() const { return CamRotTgt[1]; }

	/**
	 * Gate the attack→counter choreography for the NEXT PlayEncounter.  The GameInstance dedupes
	 * by fight so a re-presented result screen (the same resolved encounter shown more than once
	 * via a UI re-entry) doesn't replay the lunge - it shows the settled result statically instead.
	 * Must be called BEFORE PlayEncounter; resets to allowed once consumed.
	 */
	void SetChoreographyAllowed(bool bAllowed) { bChoreographyAllowed = bAllowed; }

	/** The attacker camera's final (settled) framing pose for the current encounter. */
	FVector GetCamFramingA() const { return CamPosTgt[0]; }

	/** The defender camera's final (settled) framing pose for the current encounter. */
	FVector GetCamFramingB() const { return CamPosTgt[1]; }

	/** Spawns a sun and sky so scene captures are not a black void. */
	virtual void BeginPlay() override;
	/** Advances camera glides and attack/counter choreography. */
	virtual void Tick(float DeltaSeconds) override;

	/** World position used when spawning the stage (far from match board). */
	static constexpr float kStageWorldX = 30000.f;
	/** Spacing between the two side actors so their scene captures never overlap. */
	static constexpr float kSideStageSpacing = 20000.f;

protected:
	/** Battle-stage camera; PlayerController targets this via SetViewTarget. */
	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UCameraComponent> BattleCamera;

	/** Attacker-tracking camera: renders the shared field framed on the attacker. */
	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	/** Defender-tracking camera: renders the SAME shared field framed on the defender. */
	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<USceneCaptureComponent2D> SceneCaptureB;

	/** Off-screen render target the attacker camera draws into (created at runtime). */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** Off-screen render target the defender camera draws into (created at runtime). */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTargetB;

	/** True when this actor is the attacker's screen, false for the defender's. */
	bool bIsAttackerSide{true};

	/** True once ConfigureSide() has set this actor up as a single-fighter side screen. */
	bool bSideMode{false};

	/** Local fill light - stage is far from scene lights so needs its own illumination. */
	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	/** Per-unit portrait planes, dynamically created and replaced each encounter. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> UnitPortraits;

	/** Dynamic materials parallel to UnitPortraits (one MID per portrait). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> UnitMIDs;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> AtkNameLabel;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> DefNameLabel;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> AtkRoleLabel;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> DefRoleLabel;

	/** Floating "-N" damage numbers shown over the struck combatant at each impact. */
	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> AtkDamageLabel;

	UPROPERTY(VisibleAnywhere, Category = "BattleViz")
	TObjectPtr<UTextRenderComponent> DefDamageLabel;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FloorMID;

	/** Dynamically-built board tiles for both panels (each BoardCols × BoardRows). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BoardTiles;

	/** Shared tints for the two checkerboard colours. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoardTileLightMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoardTileDarkMID;

	/** Vertical divider bar that splits the attacker / defender panels at the seam. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SeamDivider;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SeamDividerMID;

private:
	/**
	 * Load the card-art texture for ArtId and apply it to the portrait plane Comp
	 * via an unlit/emissive dynamic material.  Scales the plane to HeightCm tall
	 * while preserving the texture's aspect ratio.  Pass 0.f to use the default
	 * active-combatant height.
	 */
	void ApplyPortraitArt(UStaticMeshComponent* Comp, TObjectPtr<UMaterialInstanceDynamic>& MID,
	                      const FString& ArtId, float HeightCm = 0.f);

	/** Apply the white platform material to the floor mesh. */
	void ApplyStageFloorStyle();

	/** Destroy all existing portrait components and clear parallel arrays. */
	void DestroyAllUnitPortraits();

	/**
	 * Build one panel's checkerboard grid (Cols × Rows) centred on CenterX, sized to match
	 * the live tactics board.  PanelId disambiguates the two panels' component names.  That
	 * fighter's team portraits are then placed on the cells matching their real (GridX,
	 * GridY).  Both panels are rebuilt each encounter.
	 */
	void BuildBackgroundBoard(int32 Cols, int32 Rows, int32 PanelId, float CenterX);

	/** Destroy all board-tile components for both panels. */
	void DestroyBackgroundBoard();

	/** Build / refresh the vertical seam divider between the two panels. */
	void BuildSeamDivider();

	/** Create (once) and size the off-screen render target, and bind it to the capture. */
	void EnsureRenderTarget();

	/** Create (once) and bind BOTH off-screen render targets (attacker + defender cameras). */
	void EnsureRenderTargets();

	/**
	 * Set both cameras' target poses to frame the attacker / defender of Encounter on the
	 * shared field.  On the first encounter the cameras snap; afterwards they smoothly glide
	 * (eased) from their current pose to the new one, so advancing the queue pans the view to
	 * the next battle.  Choreography is deferred until the glide completes.
	 */
	void FrameCamerasForEncounter(bool bIsPostResolution);

	/**
	 * Billboard every unit portrait (and the floating labels) to face a camera at the given yaw,
	 * upright.  Called with the TARGET yaw when the view is settled, and with the interpolated yaw
	 * every frame DURING a glide so the sprites stay face-on while the camera swings orientation.
	 */
	void ApplyPortraitFacing(float CamYawDeg);

	/** Advance the per-frame camera glide and, on arrival, kick off any deferred choreography. */
	void UpdateCameraTransition(float DeltaSeconds);

	// ── Persistent arena (built ONCE, then the camera travels across it) ─────────
	// The whole battlefield - every unit on its TRUE board cell - is built a single time and
	// kept for the entire encounter queue.  Each Continue resolves that encounter's two
	// duelists (by EntityId) among the persistent units and glides the cameras over to them,
	// so the bystanders / board act as landmarks that make the camera travel visible.
	bool    bArenaBuilt{false};           // arena scenery + unit portraits already created
	int32   ArenaMinX{0};                 // board origin captured at build time (raw min cell X)
	int32   ArenaMinY{0};                 // board origin captured at build time (raw min cell Y)
	int32   ArenaCols{1};                 // board columns at build time
	int32   ArenaRows{1};                 // board rows at build time

	/** Map a raw board cell (GridX,GridY) to its world position in the persistent arena. */
	FVector ArenaCellToWorld(int32 GridX, int32 GridY, float StandZ) const;

	// ── Shared-field grid + dual-camera glide state ──────────────────────────────
	int32   FieldCols{1};                 // board columns (for centring the shared grid)
	int32   FieldRows{1};                 // board rows
	bool    bHasFramedOnce{false};        // first encounter snaps; later ones glide
	bool    bCamTransitioning{false};     // a camera glide is in progress
	bool    bPendingChoreography{false};  // start the lunge sequence when the glide ends
	float   CamGlideElapsed{0.f};         // seconds into the current glide
	float   CamGlideDuration{0.75f};      // length of the current glide (scaled by distance)
	FVector CamPosCur[2]{};               // [0]=attacker cam, [1]=defender cam - current pos
	FVector CamPosTgt[2]{};               // glide target positions
	FRotator CamRotCur[2]{};              // glide START orientation (slerped FROM)
	FRotator CamRotTgt[2]{};              // glide TARGET orientation (slerped TO)

	// Per-encounter camera basis (recomputed in FrameCamerasForEncounter).  Both cameras share the
	// same orientation: their RIGHT vector runs along the attacker→defender line so every duel reads
	// left→right on screen (the opponent is pushed off to the side instead of sitting behind the
	// fighter).  EncFaceQuat billboards the plane portraits so they always face the camera.
	FVector EncCamRight{-1.f, 0.f, 0.f};   // screen-horizontal axis = the lunge axis (toward the seam)
	FVector EncCamForward{0.f, 1.f, 0.f};  // camera look direction (perpendicular to the duel line)
	FQuat   EncFaceQuat{FQuat::Identity};  // portrait billboard rotation (plane faces the camera)

	// Cross-view continuity (see SetExternalGlideOrigin): the GameInstance seeds the previous
	// battle view's final framing here so the camera glides FROM it TO this encounter's framing.
	bool    bHasExternalGlideOrigin{false};
	FVector ExternalGlideOrigin[2]{};
	bool    bHasExternalGlideRot{false};
	FRotator ExternalGlideRot[2]{};

	// When false, the next encounter's attack→counter lunge is suppressed (the result is shown
	// settled instead).  Set by the GameInstance to dedupe a re-presented result screen.
	bool    bChoreographyAllowed{true};

	/**
	 * Spawn one portrait for Unit at HomeLoc with the given height, append it to
	 * UnitPortraits / UnitMIDs, and return its index.  The caller is responsible for
	 * appending the matching UnitEntityIds / UnitHomeLocations entries.
	 */
	int32 SpawnUnitPortrait(const FCombatUnitSnapshot& Unit, const FVector& HomeLoc, float HeightCm);

	/**
	 * Build the persistent arena ONCE: checkerboard ground + sky, and every unit in
	 * Encounter.AllBattlefieldUnits placed on its TRUE board cell.  Subsequent encounters reuse
	 * the same scene (see PlayEncounter) rather than rebuilding, so the camera can travel across
	 * it.  Captures ArenaMinX/Y/Cols/Rows and fills UnitEntityIds / UnitHomeLocations.
	 */
	void SetupBattlefield(const FCombatEncounter& Encounter);

	/** Find a persistent portrait's index by EntityId (-1 if absent). */
	int32 FindUnitIndexByEntityId(const FString& EntityId) const;

	/** Choreography phases for the attack→counter sequence (advanced from Tick). */
	enum class EBattlePhase : uint8
	{
		Idle,         // nothing playing
		Intro,        // brief hold before the first strike
		AtkLungeOut,  // attacker leaps toward the defender
		AtkImpact,    // defender takes damage, recoils; damage number rises
		AtkReturn,    // attacker slides back home
		Gap,          // brief pause before the counter
		CtrLungeOut,  // defender leaps toward the attacker
		CtrImpact,    // attacker takes counter damage, recoils
		CtrReturn,    // defender slides back home
		Settle        // defeated combatants topple, then return to Idle
	};

	EBattlePhase Phase{EBattlePhase::Idle};
	float PhaseTime{0.f};

	/** Per-screen choreography: advance the single fighter (Me) through the shared phase
	 *  timeline.  Both side actors run identical phase transitions so they stay in sync;
	 *  each only animates its own fighter (attacker lunges in the Atk* phases / takes the
	 *  counter in CtrImpact; defender lunges in the Ctr* phases / takes the hit in AtkImpact). */
	void TickSide(float DeltaSeconds);

	// ── Per-screen (single fighter) choreography inputs ──────────────────────────
	int32   MeUnitIdx{-1};                      // index into UnitPortraits for this side's fighter
	FVector MeHomeLoc{FVector::ZeroVector};     // rest position of this side's fighter
	float   MeLungeReach{0.f};                  // distance this fighter lunges toward the opponent
	FVector MeLungeDir{1.f, 0.f, 0.f};          // unit XY direction from this fighter toward the opponent

	// ── Per-encounter choreography inputs ────────────────────────────────────────
	int32 AtkUnitIdx{-1};         // index into UnitPortraits for the attacker
	int32 DefUnitIdx{-1};         // index into UnitPortraits for the defender
	FVector AtkHomeLoc{FVector::ZeroVector};
	FVector DefHomeLoc{FVector::ZeroVector};
	FVector LungeDir{-1.f, 0.f, 0.f};  // unit XY direction the attacker travels toward the defender
	float LungeReach{0.f};             // distance lunged (clamped to the gap between the cells)
	int32 AnimAttackDamage{0};
	int32 AnimCounterDamage{0};
	bool bAnimAttackWasCrit{false};
	bool bAnimCounterWasCrit{false};
	bool bAnimDefenderDies{false};
	bool bAnimAttackerDies{false};

	// ── Parallel arrays for spawned portrait components ───────────────────────────
	TArray<FString>  UnitEntityIds;      // entity id for each portrait
	TArray<FVector>  UnitHomeLocations;  // rest position for each portrait
};
