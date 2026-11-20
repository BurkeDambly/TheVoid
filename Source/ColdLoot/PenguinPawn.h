#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PenguinPawn.generated.h"

class ASlideParkTerrain;
class AColdzoneMapTerrain;
class UAnimSequence;
class UCameraComponent;
class UColdLootWeaponDefinition;
class UMaterialInterface;
class USkeletalMeshComponent;
class USoundBase;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class APenguinPawn : public APawn
{
    GENERATED_BODY()

    enum class EForm : uint8 { Normal, Penguin, EditorPenguin };

public:
    APenguinPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    UPROPERTY(EditAnywhere, Category="Penguin|Weapon")
    TObjectPtr<UColdLootWeaponDefinition> WeaponDefinition;

    UPROPERTY(EditAnywhere, Category="Penguin|Locomotion", meta=(ClampMin="10", Units="cm"))
    float WalkDistancePerCycle = 91.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Locomotion", meta=(ClampMin="10", Units="cm"))
    float SprintDistancePerCycle = 450.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0", Units="cm/s"))
    float StationarySlidePush = 280.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="1", Units="cm/s^2"))
    float FlatDeceleration = 320.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="1", Units="cm/s^2"))
    float FastSlideDeceleration = 85.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0"))
    float SlopeGravity = 1400.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="100"))
    float SlideSpeedLimit = 1320.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="0", Units="cm/s^2"))
    float FormDownhillGravity = 2650.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="100", Units="cm/s"))
    float FormSlideSpeedLimit = 1650.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="1", ClampMax="5"))
    float FormSlideSpeedMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="0.05", ClampMax="1"))
    float FormSlideFrictionMultiplier = 0.50f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="0", ClampMax="1"))
    float FormUphillBrakingMultiplier = 0.34f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="0.1", ClampMax="1"))
    float FormAirGravityMultiplier = 0.55f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form", meta=(ClampMin="1"))
    float FormSlideSteeringMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form")
    TSoftObjectPtr<UMaterialInterface> PenguinFormMaterial{FSoftObjectPath(
        TEXT("/Game/Penguin/Mesh/M_PenguinForm.M_PenguinForm"))};

    UPROPERTY(EditAnywhere, Category="Penguin|Penguin Form")
    TSoftObjectPtr<UMaterialInterface> EditorPenguinFormMaterial{FSoftObjectPath(
        TEXT("/Game/Penguin/Mesh/M_EditorPenguinForm.M_EditorPenguinForm"))};

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0"))
    float SlideSteering = 52.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Penguin")
    TObjectPtr<USkeletalMeshComponent> PenguinMesh;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> WalkAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> SprintAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> SlideAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> SwimAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation", meta=(ClampMin="0", ClampMax="2"))
    float UpperBodyJiggle = 1.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    float MeshYawOffset = 0.0f;

    float Speed() const { return GroundSpeed; }
    float Grade() const { return CurrentGrade; }
    float SlideDistance() const { return DistanceThisSlide; }
    float SlidePeakSpeed() const { return PeakSlideSpeed; }
    const TCHAR* MotionLabel() const;
    bool HasWeapon() const;
    bool CanFire() const;
    bool IsPenguinFormActive() const { return ActiveForm != EForm::Normal; }
    bool IsEditorPenguinFormActive() const { return ActiveForm == EForm::EditorPenguin; }
    bool IsAiming() const;
    const AColdzoneMapTerrain* IslandTerrain() const;
    bool IsSwimming() const { return bSwimming; }
    double GetSwimDepth() const { return SwimDepth; }
    float ReticleGap() const;
    FVector GetBoreReticlePoint() const;

private:
    friend class FPenguinAnimProxy;
    friend class FPenguinMovementTest;
    friend class FPenguinWeaponTest;
    friend class FIcebergWeaponTest;

    void EquipWeapon();
    void StartAim();
    void StopAim() { bAimHeld = false; }
    void StartFire();
    void StopFire() { bFireHeld = false; }
    bool WantsToFire() const { return bFireHeld || ShotRequestTime > 0.0f; }
    void UpdateWeapon(float DeltaSeconds);
    void FireFromUpdatedPose();
    void UpdateRecoil(float DeltaSeconds);
    void AddShotRecoil();
    void UpdateCameraRecoil();
    void FireBullet(const FTransform& Muzzle, const FVector& GripLocation);
    FVector GetWeaponAimDirection() const;
    bool IsSprintFiring() const { return bSprinting && WantsToFire() && CanFire(); }
    void RollSprintSpread();
    bool TraceMuzzleObstruction(const FVector& GripLocation, const FVector& MuzzleLocation, FHitResult& Hit) const;
    void Step(float Seconds, const FVector& Wish);
    void StepSwimming(float Seconds);
    void EnterWater(FVector Position, double SurfaceZ, const FVector& EntryVelocity);
    void UpdateUnderwaterView(float DeltaSeconds);
    float GetGaitCycle(bool bSprint) const;
    float GetGaitSpeed(bool bSprint) const;
    void AdvanceGait(float Seconds);
    void AdvanceSlidePose(float Seconds);
    void StepSlideFlight(float Seconds);
    bool TrySlideLaunch(float Seconds, const FVector& Start, const FVector& Travel, double Rise);
    void ToggleSlide();
    void TogglePenguinForm();
    void ToggleIslandMap();
    void ToggleEditorPenguinForm();
    void ToggleGraphicsMenu();
#if WITH_EDITOR
    void PreviousIslandZone();
    void NextIslandZone();
    void VisitIslandZone();
    void CaptureIslandView();
#endif
    void SetForm(EForm Form);
    float GetSlideSpeedScale() const;
    float GetSlideSpeedLimit() const;
    void StandUpFromSlide();
    void MoveForward(float Value) { ForwardInput = Value; }
    void MoveRight(float Value) { RightInput = Value; }
    void StartSprint() { bSprintHeld = true; }
    void StopSprint() { bSprintHeld = false; }
    void LookYaw(float Value);
    void LookPitch(float Value);
    FRotator GetLookYaw() const;
    void ResetFlat();
    void ResetDownhill();
    void ResetUphill();
    void ResetRamp();
    void ResetAt(double X, double Y = 0.0);

    UPROPERTY()
    TObjectPtr<ASlideParkTerrain> Terrain;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> BodyPivot;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> WeaponGrip;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> WeaponReceiver;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> WeaponBolt;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> WeaponDrum;

    UPROPERTY(Transient)
    TObjectPtr<USoundBase> EquippedFiringSound;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> PenguinFormGlow;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> EditorPenguinFormGlow;

    FVector HipSocketOffset = FVector::ZeroVector;
    float HipArmLength = 508.2f;
    float HipFieldOfView = 78.0f;
    float AimBlend = 0.0f;
    FVector2D CameraKick = FVector2D::ZeroVector;
    FVector2D RecoilOffset = FVector2D::ZeroVector;
    FVector2D RecoilGoal = FVector2D::ZeroVector;
    float RecoilRecoveryDelay = 0.0f;
    int32 RecoilShotIndex = 0;
    FVector2D SprintSpread = FVector2D::ZeroVector;
    float SprintFireBlend = 0.0f;
    float WeaponHoldTime = 0.0f;
    float ShotCooldown = 0.0f;
    float ShotRequestTime = 0.0f;
    bool bFireHeld = false;
    bool bFireAfterPose = false;
    bool bAimHeld = false;

    FVector Heading = FVector::ForwardVector;
    FVector Facing = FVector::ForwardVector;
    float ForwardInput = 0.0f;
    float RightInput = 0.0f;
    float GroundSpeed = 0.0f;
    double SlideVerticalSpeed = 0.0;
    double SlideAirGravity = 0.0;
    float SlideFlightSpeedScale = 1.0f;
    float NextSlideLaunchDistance = 0.0f;
    float CurrentGrade = 0.0f;
    float DistanceThisSlide = 0.0f;
    float PeakSlideSpeed = 0.0f;
    float SlidePoseTime = 0.0f;
    float GaitPhase = 0.0f;
    float SprintBlend = 0.0f;
    bool bSprintHeld = false;
    bool bSprinting = false;
    bool bSliding = false;
    bool bSlideAirborne = false;
    bool bCanLaunchOnUphill = true;
    EForm ActiveForm = EForm::Normal;
    bool bSprintEntry = false;
    bool bRecovering = false;
    bool bSwimming = false;
    bool bWaterBreaching = false;
    FVector SwimVelocity = FVector::ZeroVector;
    FVector WaterCameraOffset = FVector::ZeroVector;
    FRotator SwimHeading = FRotator::ZeroRotator;
    float SwimThrust = 0.0f;
    double SwimDepth = 0.0;
};
