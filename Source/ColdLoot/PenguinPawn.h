#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Pawn.h"
#include "PenguinPawn.generated.h"

class ASlideParkTerrain;
class AColdzoneMapTerrain;
class UAnimSequence;
class UCameraComponent;
class UCapsuleComponent;
class UColdLootWeaponDefinition;
class UMaterialInterface;
class USkeletalMeshComponent;
class USoundBase;
class USpringArmComponent;
class UStaticMeshComponent;

UENUM()
enum class EPenguinAction : uint8
{
    Slide, Jump, AimPressed, AimReleased, FirePressed, FireReleased, ReturnToBase
};

USTRUCT()
struct FPenguinNetworkPose
{
    GENERATED_BODY()

    UPROPERTY()
    FVector_NetQuantize10 Position = FVector::ZeroVector;
    UPROPERTY()
    FRotator BodyRotation = FRotator::ZeroRotator;
    UPROPERTY()
    FRotator View = FRotator::ZeroRotator;
    UPROPERTY()
    float Speed = 0.0f;
    UPROPERTY()
    float SprintBlend = 0.0f;
    UPROPERTY()
    float SlideTime = 0.0f;
    UPROPERTY()
    float SwimThrust = 0.0f;
    UPROPERTY()
    float SwimDepth = 0.0f;
    UPROPERTY()
    float FireBlend = 0.0f;
    UPROPERTY()
    uint8 Flags = 0;
    UPROPERTY()
    bool bHoldingWaterEntryPose = false;
};

USTRUCT()
struct FPenguinCombatState
{
    GENERATED_BODY()

    UPROPERTY()
    uint8 Health = 100;
    UPROPERTY()
    FVector_NetQuantize10 KnockoutOrigin = FVector::ZeroVector;
    UPROPERTY()
    FRotator KnockoutRotation = FRotator::ZeroRotator;
    UPROPERTY()
    FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
};

UCLASS()
class APenguinPawn : public APawn
{
    GENERATED_BODY()

public:
    APenguinPawn();
    virtual void PreInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    UPROPERTY(EditAnywhere, Category="Penguin|Weapon")
    TObjectPtr<UColdLootWeaponDefinition> WeaponDefinition;

    UPROPERTY(EditAnywhere, Category="Penguin|Locomotion", meta=(ClampMin="10", Units="cm"))
    float WalkDistancePerCycle = 182.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Locomotion", meta=(ClampMin="10", Units="cm"))
    float SprintDistancePerCycle = 450.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0", Units="cm/s"))
    float StationarySlidePush = 1850.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="1", Units="cm/s^2"))
    float FlatDeceleration = 92.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="1", Units="cm/s^2"))
    float FastSlideDeceleration = 22.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0"))
    float SlopeGravity = 1400.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="100"))
    float SlideSpeedLimit = 4200.0f;

    UPROPERTY(EditAnywhere, Category="Penguin|Slide", meta=(ClampMin="0"))
    float SlideSteering = 130.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Penguin")
    TObjectPtr<USkeletalMeshComponent> PenguinMesh;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> WalkAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> SprintAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> WalkWaddleAnimation;

    UPROPERTY(EditAnywhere, Category="Penguin|Animation")
    TObjectPtr<UAnimSequence> SprintWaddleAnimation;

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
    bool IsAiming() const;
    const AColdzoneMapTerrain* IslandTerrain() const;
    bool IsSwimming() const { return bSwimming; }
    double GetSwimDepth() const { return SwimDepth; }
    float ReticleGap() const;
    static constexpr int32 MaxHealth = 100;
    int32 GetHealth() const { return CombatState.Health; }
    bool IsKnockedOut() const { return GetHealth() == 0; }

private:
    friend class FPenguinAnimProxy;
    friend class FPenguinMovementTest;
    friend class FPenguinWeaponTest;
    friend class FIcebergWeaponTest;
    friend class FIcebergNetworkTest;
    friend class FPenguinWaddleVisibilityTest;
    friend class FColdLootMultiplayerPlaytest;
    friend class AColdLootHUD;

    void EquipWeapon();
    void StartAim();
    void StopAim();
    void StartFire();
    void StopFire();
    bool WantsToFire() const { return bFireHeld || ShotRequestTime > 0.0f; }
    void UpdateWeapon(float DeltaSeconds);
    void FinishWeaponPose();
    void AlignBodyToCrosshair(FTransform& Muzzle, FVector& GripLocation);
    void UpdateRecoil(float DeltaSeconds);
    void AddShotRecoil();
    void UpdateCameraRecoil();
    void FireBullet(const FTransform& Muzzle, const FVector& GripLocation);
    void SpawnBullet(const FVector& Origin, const FVector& Direction, float Speed);
    void UpdateAimCamera(float DeltaSeconds);
    void UpdateWeaponVisibility();
    void CopyPlaytestAppearance();
    void TickNetworkClient(float DeltaSeconds);
    void PublishNetworkPose();
    void SetRagdollActive(bool bActive);

    UFUNCTION()
    void OnRep_CombatState();

    UFUNCTION(Server, Unreliable)
    void ServerUpdateInput(float Forward, float Right, FRotator View, bool bSprint, bool bBrake);
    UFUNCTION(Server, Reliable)
    void ServerAction(EPenguinAction Action);
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastShot(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, float Speed, bool bSpawnTracer);

    UPROPERTY(Replicated)
    FPenguinNetworkPose NetworkPose;
    UPROPERTY(ReplicatedUsing=OnRep_CombatState)
    FPenguinCombatState CombatState;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCapsuleComponent> BulletHitbox;
    FVector CameraMountLocation = FVector::ZeroVector;
    FTransform StandingMeshTransform;
    bool bRagdollActive = false;
    float InputSendTime = 0.0f;
    double LastRemoteInputTime = 0.0;
    bool bReceivedNetworkPose = false;
    FVector GetWeaponAimDirection() const;
    bool IsSprintFiring() const { return bSprinting && WantsToFire() && CanFire(); }
    void RollSprintSpread();
    bool TraceMuzzleObstruction(const FVector& GripLocation, const FTransform& Muzzle, FHitResult& Hit) const;
    void Step(float Seconds, const FVector& Wish);
    void StepSwimming(float Seconds);
    void EnterWater(FVector Position, double SurfaceZ, const FVector& EntryVelocity);
    bool IsHoldingWaterEntryPose() const;
    void UpdateUnderwaterView(float DeltaSeconds);
    float GetGaitCycle(bool bSprint) const;
    float GetGaitSpeed(bool bSprint) const;
    void AdvanceGait(float Seconds);
    void AdvanceSlidePose(float Seconds);
    void StepSlideFlight(float Seconds);
    bool TrySlideLaunch(float Seconds, const FVector& Start, const FVector& Travel, double Rise);
    void ToggleSlide();
    void StartSlideBrake() { bSlideBrakeHeld = true; }
    void StopSlideBrake() { bSlideBrakeHeld = false; }
    float GetSlideBrakeInput() const;
    void Jump();
    void InterruptSlideForWeapon();
    void ToggleIslandMap();
    void ToggleGraphicsMenu();
    void ToggleTuningMenu();
#if WITH_EDITOR
    void PreviousIslandZone();
    void NextIslandZone();
    void VisitIslandZone();
    void CaptureIslandView();
#endif
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
    bool bAimAfterPose = false;
    bool bAimHeld = false;

    FVector Heading = FVector::ForwardVector;
    FVector Facing = FVector::ForwardVector;
    float ForwardInput = 0.0f;
    float RightInput = 0.0f;
    float GroundSpeed = 0.0f;
    double SlideVerticalSpeed = 0.0;
    float NextSlideLaunchDistance = 0.0f;
    float SlideGroundLockRemaining = 0.0f;
    float DivePitch = 0.0f;
    float CurrentGrade = 0.0f;
    float DistanceThisSlide = 0.0f;
    float PeakSlideSpeed = 0.0f;
    float SlidePoseTime = 0.0f;
    float GaitPhase = 0.0f;
    float SprintBlend = 0.0f;
    float TurnLean = 0.0f;
    bool bSprintHeld = false;
    bool bSprinting = false;
    bool bSliding = false;
    bool bSlideBrakeHeld = false;
    bool bSlideAirborne = false;
    bool bDiving = false;
    int32 WaterSkips = 0;
    bool bSprintEntry = false;
    bool bRecovering = false;
    bool bSwimming = false;
    bool bWaterBreaching = false;
    FVector SwimVelocity = FVector::ZeroVector;
    FVector WaterCameraOffset = FVector::ZeroVector;
    FRotator SwimHeading = FRotator::ZeroRotator;
    float SwimThrust = 0.0f;
    float SwimEntryTime = 0.0f;
    double SwimDepth = 0.0;
};
