#include "PenguinPawn.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ColdLootBullet.h"
#include "ColdLootBulletImpact.h"
#include "ColdLootGameMode.h"
#include "ColdLootTuning.h"
#include "ColdLootWeaponDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IcebergNetworkTerrain.h"
#include "ColdzoneMapTerrain.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "PenguinAnimInstance.h"
#include "PenguinCameraArm.h"
#include "SlideParkTerrain.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
using Tune = EColdLootTuning;
// Source frames 1-18 tip onto the belly. Frames 18-36 are the glide.
constexpr float BellyPose = 17.0f / 30.0f;
constexpr float RecoverPose = 35.0f / 30.0f;
constexpr float SlideStopSpeed = 65.0f;
constexpr float SlideEntryBoost = 1750.0f;
constexpr float SprintSlideEntryBoost = 2393.0f;
constexpr float SlideCruiseSpeed = 2200.0f;
constexpr float SlideBrakeDeceleration = 1800.0f;
constexpr float MaxTurnLean = 20.0f;
constexpr float SlideGroundLockSeconds = 0.20f;
constexpr float WalkTurnRate = 900.0f;
constexpr float SprintTurnRate = 450.0f;
constexpr float WaterEntryPoseSeconds = 0.40f;
constexpr double SwimSpeedMultiplier = 2.0;
constexpr double SwimAcceleration = 3350.0 * SwimSpeedMultiplier;
constexpr double SwimBreachSpeed = 805.0 * SwimSpeedMultiplier;
constexpr double SwimBreachRise = 550.0 * SwimSpeedMultiplier;
constexpr double SwimSeabedClearance = 38.0;
constexpr double AirGravity = 1176.0;
constexpr double JumpLift = 390.0;
constexpr double SlideJumpLift = 490.0;
constexpr float DivePitchRate = 240.0f;
constexpr float AirControlSpeed = 480.0f;
constexpr float AirControlAcceleration = 1100.0f;
constexpr double SkipMinSpeed = 900.0;
constexpr double SkipMaxFallRatio = 0.27;
constexpr double SlideLaunchMinSpeed = 660.0;
constexpr double SlideCrestLookAhead = 65.0;
constexpr double RolloverLift = 85.0;
constexpr float SlideLandingRunout = 400.0f;
constexpr double CrosshairTraceDistance = 26000.0;
constexpr float WeaponHoldSeconds = 2.0f;
constexpr float RecoilRecoverySeconds = 0.13f;
constexpr double RecoilStrength = 0.60;
// Horizontal positions in degrees. Successive shots walk across the curve, then repeat the weave.
constexpr double RecoilYawPath[] = {0.0, 0.22, 0.62, 1.02, 1.16, 0.80, 0.20, -0.60,
    -1.12, -0.92, -0.30, 0.50, 1.04, 0.72, 0.18, 0.0};
constexpr float HipCameraDistanceScale = 1.10f;
constexpr float AimCameraArmLength = 387.2f * 1.50f;
const FName MuzzleSocket(TEXT("Muzzle"));

double SwimDraft(const FRotator& Heading)
{
    // The paddle pose lifts the beak ahead of the pawn origin. Follow its height as the body pitches.
    const FVector BeakInPaddlePose(168.0, 0.0, 47.0);
    return FMath::Max(30.0, Heading.RotateVector(BeakInPaddlePose).Z - 4.0);
}

AColdLootHUD* GetIslandHUD(const APenguinPawn& Pawn)
{
    const auto* Controller = Cast<APlayerController>(Pawn.GetController());
    return Pawn.IslandTerrain() && Controller ? Cast<AColdLootHUD>(Controller->GetHUD()) : nullptr;
}

double ApplyRecoilCorrection(double Input, double& Offset, double& Goal)
{
    if (Input * Offset < 0.0 && Input * Goal < 0.0)
    {
        // Apply opposing mouse movement to the kick first, so recovery does not undo the correction.
        const double Correction = FMath::Sign(Input) * FMath::Min3(FMath::Abs(Input), FMath::Abs(Offset), FMath::Abs(Goal));
        Offset += Correction;
        Goal += Correction;
        Input -= Correction;
    }
    return Input;
}
}

APenguinPawn::APenguinPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessPlayer = EAutoReceiveInput::Player0;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GroundOrigin")));

    BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
    BodyPivot->SetupAttachment(RootComponent);
    BulletHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BulletHitbox"));
    BulletHitbox->SetupAttachment(BodyPivot);
    BulletHitbox->InitCapsuleSize(36.0f, 69.0f);
    BulletHitbox->SetRelativeLocation(FVector(0, 0, 68));
    BulletHitbox->SetCollisionObjectType(ECC_Pawn);
    BulletHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
    BulletHitbox->SetCollisionResponseToChannel(AColdLootBullet::CollisionChannel, ECR_Block);
    BulletHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BulletHitbox->SetGenerateOverlapEvents(false);
    PenguinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PenguinMesh"));
    PenguinMesh->SetupAttachment(BodyPivot);
    PenguinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PenguinMesh->SetAnimInstanceClass(UPenguinAnimInstance::StaticClass());
    PenguinMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    WeaponGrip = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponGrip"));
    WeaponGrip->SetupAttachment(PenguinMesh, TEXT("Flipper_R"));
    WeaponReceiver = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponReceiver"));
    WeaponReceiver->SetupAttachment(WeaponGrip);
    WeaponBolt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponBolt"));
    WeaponBolt->SetupAttachment(WeaponReceiver);
    WeaponDrum = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponDrum"));
    WeaponDrum->SetupAttachment(WeaponReceiver);
    for (UStaticMeshComponent* Part : {WeaponReceiver.Get(), WeaponBolt.Get(), WeaponDrum.Get()})
    {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    static ConstructorHelpers::FObjectFinderOptional<UColdLootWeaponDefinition> StarterWeapon(
        TEXT("/Game/Weapons/Starter/DA_StarterWeapon.DA_StarterWeapon"));
    WeaponDefinition = StarterWeapon.Get();
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> WalkWaddle(
        TEXT("/Game/Penguin/Animations/A_Walk_Shuffle_Waddle.A_Walk_Shuffle_Waddle"));
    static ConstructorHelpers::FObjectFinderOptional<UAnimSequence> SprintWaddle(
        TEXT("/Game/Penguin/Animations/A_Sprint_Spring_Waddle.A_Sprint_Spring_Waddle"));
    WalkWaddleAnimation = WalkWaddle.Get();
    SprintWaddleAnimation = SprintWaddle.Get();

    CameraArm = CreateDefaultSubobject<UPenguinCameraArm>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(RootComponent);
    CameraArm->SetRelativeLocation(FVector(0, 0, 150));
    CameraArm->SetRelativeRotation(FRotator(-9, 0, 0));
    CameraArm->TargetArmLength = 508.2f;
    CameraArm->SocketOffset = FVector(0, 148, 12);
    CameraArm->bEnableCameraLag = true;
    CameraArm->CameraLagSpeed = 11;
    CameraArm->CameraLagMaxDistance = 100;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    Camera->SetFieldOfView(78);
    Camera->PostProcessBlendWeight = 0.0f;
    Camera->PostProcessSettings.bOverride_ColorSaturation = true;
    Camera->PostProcessSettings.ColorSaturation = FVector4(.71, .90, 1.02, 1.0);
    Camera->PostProcessSettings.bOverride_ColorGain = true;
    Camera->PostProcessSettings.ColorGain = FVector4(.63, .82, .92, 1.0);
    Camera->PostProcessSettings.bOverride_ColorOffset = true;
    Camera->PostProcessSettings.ColorOffset = FVector4(-.014, .016, .031, 0.0);
}

void APenguinPawn::PreInitializeComponents()
{
    if (GetNetMode() != NM_Standalone)
    {
        AutoPossessPlayer = EAutoReceiveInput::Disabled;
        AutoPossessAI = EAutoPossessAI::Disabled;
        if (HasAuthority() && !IsNetStartupActor())
        {
            SetReplicates(true);
            SetNetUpdateFrequency(30.0f);
        }
    }
    Super::PreInitializeComponents();
}

void APenguinPawn::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_Standalone)
    {
        if (IsNetStartupActor())
        {
            SetActorHiddenInGame(true);
            SetActorTickEnabled(false);
            SetActorEnableCollision(false);
            return;
        }
        CopyPlaytestAppearance();
    }
    for (TActorIterator<ASlideParkTerrain> It(GetWorld()); It; ++It)
    {
        Terrain = *It;
        break;
    }

    if (!Terrain)
    {
        UE_LOG(LogTemp, Error, TEXT("Penguin requires a Slide Park Terrain actor."));
        return;
    }

    CameraArm->AddTickPrerequisiteActor(this);
    PenguinMesh->AddTickPrerequisiteActor(this);
    PenguinMesh->AddTickPrerequisiteComponent(CameraArm);
    PenguinMesh->SetAnimInstanceClass(UPenguinAnimInstance::StaticClass());
    PenguinMesh->RegisterOnBoneTransformsFinalizedDelegate(
        FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &APenguinPawn::FinishWeaponPose));
    PenguinMesh->SetRelativeRotation(FRotator(0, MeshYawOffset, 0));
    StandingMeshTransform = PenguinMesh->GetRelativeTransform();
    CameraMountLocation = CameraArm->GetRelativeLocation();
    HipArmLength = CameraArm->TargetArmLength * HipCameraDistanceScale;
    CameraArm->TargetArmLength = HipArmLength;
    HipSocketOffset = CameraArm->SocketOffset;
    HipFieldOfView = Camera->FieldOfView;
    EquipWeapon();
    const auto* Network = Cast<AIcebergNetworkTerrain>(Terrain);
    if (Network && Network->UnderwaterFog)
    {
        Network->UnderwaterFog->EnsureIsComplete();
        Camera->PostProcessSettings.AddBlendable(Network->UnderwaterFog.Get(), 1.0f);
    }
    if (GetNetMode() == NM_Standalone)
    {
        ResetFlat();
    }
    else if (HasAuthority())
    {
        FVector Position = GetActorLocation();
        Position.Z = Terrain->HeightAt(Position.X, Position.Y) + 1.0;
        SetActorLocation(Position);
        PublishNetworkPose();
    }
    OnRep_CombatState();
}

void APenguinPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &APenguinPawn::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &APenguinPawn::MoveRight);
    Input->BindAxis(TEXT("LookYaw"), this, &APenguinPawn::LookYaw);
    Input->BindAxis(TEXT("LookPitch"), this, &APenguinPawn::LookPitch);
    Input->BindAction(TEXT("Sprint"), IE_Pressed, this, &APenguinPawn::StartSprint);
    Input->BindAction(TEXT("Sprint"), IE_Released, this, &APenguinPawn::StopSprint);
    Input->BindAction(TEXT("Slide"), IE_Pressed, this, &APenguinPawn::ToggleSlide);
    Input->BindAction(TEXT("SlideBrake"), IE_Pressed, this, &APenguinPawn::StartSlideBrake).bConsumeInput = false;
    Input->BindAction(TEXT("SlideBrake"), IE_Released, this, &APenguinPawn::StopSlideBrake).bConsumeInput = false;
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &APenguinPawn::Jump);
    Input->BindKey(EKeys::M, IE_Pressed, this, &APenguinPawn::ToggleIslandMap);
    Input->BindKey(EKeys::F2, IE_Pressed, this, &APenguinPawn::ToggleGraphicsMenu).bExecuteWhenPaused = true;
    Input->BindKey(EKeys::F3, IE_Pressed, this, &APenguinPawn::ToggleTuningMenu).bExecuteWhenPaused = true;
#if WITH_EDITOR
    Input->BindKey(EKeys::Up, IE_Pressed, this, &APenguinPawn::PreviousIslandZone);
    Input->BindKey(EKeys::Down, IE_Pressed, this, &APenguinPawn::NextIslandZone);
    Input->BindKey(EKeys::Enter, IE_Pressed, this, &APenguinPawn::VisitIslandZone);
    Input->BindKey(EKeys::F10, IE_Pressed, this, &APenguinPawn::CaptureIslandView);
#endif
    Input->BindAction(TEXT("Fire"), IE_Pressed, this, &APenguinPawn::StartFire);
    Input->BindAction(TEXT("Fire"), IE_Released, this, &APenguinPawn::StopFire);
    Input->BindAction(TEXT("Aim"), IE_Pressed, this, &APenguinPawn::StartAim);
    Input->BindAction(TEXT("Aim"), IE_Released, this, &APenguinPawn::StopAim);
    Input->BindAction(TEXT("Flat"), IE_Pressed, this, &APenguinPawn::ResetFlat);
    Input->BindAction(TEXT("Downhill"), IE_Pressed, this, &APenguinPawn::ResetDownhill);
    Input->BindAction(TEXT("Uphill"), IE_Pressed, this, &APenguinPawn::ResetUphill);
    Input->BindAction(TEXT("Ramp"), IE_Pressed, this, &APenguinPawn::ResetRamp);
}

void APenguinPawn::LookYaw(float Value)
{
    const double Yaw = ApplyRecoilCorrection(Value, RecoilOffset.X, RecoilGoal.X);
    CameraArm->AddRelativeRotation(FRotator(0, Yaw, 0));
}

void APenguinPawn::LookPitch(float Value)
{
    FRotator Orbit = CameraArm->GetRelativeRotation();
    const double Pitch = ApplyRecoilCorrection(Value, RecoilOffset.Y, RecoilGoal.Y);
    Orbit.Pitch = (bSwimming || bWaterBreaching) ? FMath::Clamp(Orbit.Pitch + Pitch, -76.0, 61.0)
        : FMath::Clamp(Orbit.Pitch + Pitch, -55.0, 8.0);
    CameraArm->SetRelativeRotation(Orbit);
}

FRotator APenguinPawn::GetLookYaw() const
{
    return FRotator(0, CameraArm->GetRelativeRotation().Yaw, 0);
}

void APenguinPawn::EquipWeapon()
{
    if (!WeaponDefinition)
    {
        return;
    }
    WeaponGrip->SetRelativeTransform(WeaponDefinition->GripOnFlipper);
    WeaponReceiver->SetStaticMesh(WeaponDefinition->Receiver);
    WeaponReceiver->SetRelativeTransform(WeaponDefinition->ReceiverOnGrip);
    WeaponBolt->SetStaticMesh(WeaponDefinition->Bolt);
    WeaponBolt->SetRelativeTransform(WeaponDefinition->BoltOnReceiver);
    WeaponDrum->SetStaticMesh(WeaponDefinition->Drum);
    WeaponDrum->SetRelativeTransform(WeaponDefinition->DrumOnReceiver);
    EquippedFiringSound = WeaponDefinition->FiringSound.LoadSynchronous();
}

bool APenguinPawn::IsAiming() const
{
    return !IsKnockedOut() && WeaponDefinition && WeaponReceiver->GetStaticMesh() && bAimHeld
        && !bSliding && !bWaterBreaching && !bRecovering && !bSwimming;
}

bool APenguinPawn::HasWeapon() const
{
    return WeaponDefinition && WeaponReceiver->DoesSocketExist(MuzzleSocket);
}

bool APenguinPawn::CanFire() const
{
    return !IsKnockedOut() && HasWeapon() && !bSliding && !bWaterBreaching && !bRecovering && !bSwimming;
}

float APenguinPawn::ReticleGap() const
{
    return FMath::Lerp(bSprinting ? 16.0f : 10.0f, 4.0f, AimBlend);
}

bool APenguinPawn::TraceMuzzleObstruction(const FVector& GripLocation, const FTransform& Muzzle, FHitResult& Hit) const
{
    const FVector Origin = Muzzle.GetLocation();
    const FVector Bore = Muzzle.GetRotation().GetForwardVector();
    const double BarrelDepth = FMath::Max(0.0, FVector::DotProduct(Origin - GripLocation, Bore));
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(MuzzleClearance), true, this);
    return GetWorld()->SweepSingleByChannel(Hit, Origin - Bore * BarrelDepth, Origin, FQuat::Identity,
        AColdLootBullet::CollisionChannel, FCollisionShape::MakeSphere(AColdLootBullet::CollisionRadius), Query);
}

FVector APenguinPawn::GetWeaponAimDirection() const
{
    const FQuat Look = CameraArm->GetTargetRotation().Quaternion() * Camera->GetRelativeRotation().Quaternion();
    return Look.RotateVector(FVector(1.0, SprintSpread.X, SprintSpread.Y)).GetSafeNormal();
}

void APenguinPawn::RollSprintSpread()
{
    const float SpreadScale = FColdLootTuning::ForWorld(GetWorld()).Get(Tune::SprintSpread);
    const double Angle = FMath::FRandRange(0.0, UE_TWO_PI);
    const double Radius = FMath::Tan(FMath::DegreesToRadians(FMath::FRandRange(1.0, 3.25) * SpreadScale));
    SprintSpread = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
}

void APenguinPawn::StartAim()
{
    if (!HasAuthority())
    {
        bAimHeld = true;
        ServerAction(EPenguinAction::AimPressed);
        return;
    }
    bAimHeld = true;
    InterruptSlideForWeapon();
    if (IsAiming())
    {
        bSprinting = false;
    }
}

void APenguinPawn::StopAim()
{
    bAimHeld = false;
    if (!HasAuthority())
    {
        ServerAction(EPenguinAction::AimReleased);
    }
}

void APenguinPawn::StartFire()
{
    if (!HasAuthority())
    {
        ServerAction(EPenguinAction::FirePressed);
        return;
    }
    bFireHeld = true;
    InterruptSlideForWeapon();
    if (CanFire() && ShotCooldown <= 0.0f)
    {
        ShotRequestTime = 0.25f;
    }
}

void APenguinPawn::StopFire()
{
    bFireHeld = false;
    if (!HasAuthority())
    {
        ServerAction(EPenguinAction::FireReleased);
    }
}

void APenguinPawn::UpdateCameraRecoil()
{
    Camera->SetRelativeRotation(FRotator(RecoilOffset.Y + CameraKick.Y, RecoilOffset.X + CameraKick.X,
        -0.35 * CameraKick.X));
}

void APenguinPawn::UpdateRecoil(float DeltaSeconds)
{
    RecoilRecoveryDelay = FMath::Max(0.0f, RecoilRecoveryDelay - DeltaSeconds);
    if (RecoilRecoveryDelay <= 0.0f)
    {
        RecoilGoal *= FMath::Exp(-7.0f * DeltaSeconds);
        RecoilShotIndex = 0;
    }
    RecoilOffset = FMath::Lerp(RecoilOffset, RecoilGoal, 1.0 - FMath::Exp(-36.0 * DeltaSeconds));
    CameraKick *= FMath::Exp(-28.0f * DeltaSeconds);
}

void APenguinPawn::AddShotRecoil()
{
    constexpr int32 WeaveLength = UE_ARRAY_COUNT(RecoilYawPath) - 1;
    const int32 Step = RecoilShotIndex % WeaveLength;
    const double Strength = RecoilStrength * FColdLootTuning::ForWorld(GetWorld()).Get(Tune::Recoil);
    const double Scale = Strength * (bSprinting ? 1.60 : FMath::Lerp(1.20, 1.0, AimBlend));
    const double Rise = FMath::Lerp(0.58, 0.30, FMath::Min(RecoilShotIndex / 15.0, 1.0));
    RecoilGoal.X = FMath::Clamp(RecoilGoal.X + (RecoilYawPath[Step + 1] - RecoilYawPath[Step]) * Scale,
        -3.0 * Scale, 3.0 * Scale);
    RecoilGoal.Y = FMath::Min(RecoilGoal.Y + Rise * Scale, 9.0 * Scale);
    ++RecoilShotIndex;
    if (RecoilShotIndex == WeaveLength * 2)
    {
        RecoilShotIndex = WeaveLength;
    }
    RecoilRecoveryDelay = RecoilRecoverySeconds;

    const double Yaw = FMath::FRandRange(0.025, 0.075) * (FMath::RandBool() ? 1.0 : -1.0);
    const double Pitch = FMath::FRandRange(-0.025, 0.085);
    const double KickScale = Strength * 1.44 * (bSprinting ? 2.6 : 1.0);
    const double Limit = 0.13 * KickScale;
    CameraKick.X = FMath::Clamp(CameraKick.X + Yaw * KickScale, -Limit, Limit);
    CameraKick.Y = FMath::Clamp(CameraKick.Y + Pitch * KickScale, -Limit, Limit);
}

void APenguinPawn::UpdateWeapon(float DeltaSeconds)
{
    bFireAfterPose = false;
    if (!WeaponDefinition)
    {
        return;
    }

    const bool bSprintFire = IsSprintFiring();
    WeaponHoldTime = FMath::Max(0.0f, WeaponHoldTime - DeltaSeconds);
    const bool bKeepWeaponRaised = CanFire() && (bSprintFire || WeaponHoldTime > KINDA_SMALL_NUMBER);
    SprintFireBlend = FMath::FInterpConstantTo(SprintFireBlend, bKeepWeaponRaised ? 1.0f : 0.0f, DeltaSeconds, 15.0f);
    if (!bSprintFire)
    {
        SprintSpread = FVector2D::ZeroVector;
    }
    else if (SprintSpread.IsNearlyZero())
    {
        RollSprintSpread();
    }
    UpdateAimCamera(DeltaSeconds);
    UpdateWeaponVisibility();
    const FVector BoltRest = WeaponDefinition->BoltOnReceiver.GetLocation();
    WeaponBolt->SetRelativeLocation(BoltRest - FVector(CameraKick.Size() * 16.0f, 0, 0));

    ShotCooldown = ShotCooldown > 0.0f ? ShotCooldown - FMath::Min(DeltaSeconds, 0.2f) : 0.0f;
    if (!WantsToFire() || !CanFire())
    {
        ShotRequestTime = 0.0f;
        return;
    }

    ShotRequestTime -= DeltaSeconds;
    bFireAfterPose = ShotCooldown <= KINDA_SMALL_NUMBER && (!bSprintFire || SprintFireBlend >= 0.99f);
}

void APenguinPawn::UpdateAimCamera(float DeltaSeconds)
{
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    AimBlend = FMath::Lerp(AimBlend, IsAiming() ? 1.0f : 0.0f, 1.0f - FMath::Exp(-15.0f * DeltaSeconds));
    CameraArm->TargetArmLength = FMath::Lerp(HipArmLength * Tuning.Get(Tune::HipCamera),
        AimCameraArmLength * Tuning.Get(Tune::ADSCamera), AimBlend);
    FVector Shoulder = FMath::Lerp(HipSocketOffset, FVector(0, 196, 1), AimBlend);
    Shoulder.Y *= Tuning.Get(Tune::ShoulderOffset);
    CameraArm->SocketOffset = Shoulder;
    Camera->SetFieldOfView(FMath::Lerp(HipFieldOfView, 60.0f * Tuning.Get(Tune::AimFOV), AimBlend));
    UpdateRecoil(DeltaSeconds);
    UpdateCameraRecoil();
}

void APenguinPawn::UpdateWeaponVisibility()
{
    WeaponGrip->SetVisibility(!IsKnockedOut() && !bSwimming && !bSliding && !bWaterBreaching && !bRecovering
        && (!bSprinting || SprintBlend < 0.35f || SprintFireBlend > 0.3f), true);
}

void APenguinPawn::AlignBodyToCrosshair(FTransform& Muzzle, FVector& GripLocation)
{
    const FVector CameraOrigin = Camera->GetComponentLocation();
    const FVector Aim = GetWeaponAimDirection();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(CrosshairSurface), true, this);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, CameraOrigin, CameraOrigin + Aim * CrosshairTraceDistance,
        AColdLootBullet::CollisionChannel, Query))
    {
        return;
    }

    const FVector Pivot = BodyPivot->GetComponentLocation();
    const FVector ToSurface = Hit.ImpactPoint - Pivot;
    if (FVector::DotProduct(ToSurface, Aim) <= 0.0)
    {
        return;
    }

    // Find the forward point on the current bore at the target's radius from the body pivot.
    // Rotating that point onto the chosen surface carries the entire weapon hold with the body.
    const FVector Bore = Muzzle.GetRotation().GetForwardVector();
    const FVector BoreOffset = FVector::VectorPlaneProject(Muzzle.GetLocation() - Pivot, Bore);
    const double ForwardDistanceSquared = ToSurface.SizeSquared() - BoreOffset.SizeSquared();
    if (ForwardDistanceSquared <= 0.0)
    {
        return;
    }
    const FVector Current = BoreOffset + Bore * FMath::Sqrt(ForwardDistanceSquared);
    const FQuat Turn = FQuat::FindBetweenVectors(Current, ToSurface);
    BodyPivot->SetWorldRotation(Turn * BodyPivot->GetComponentQuat());
    Facing = BodyPivot->GetForwardVector().GetSafeNormal2D();
    Muzzle.SetLocation(Pivot + Turn.RotateVector(Muzzle.GetLocation() - Pivot));
    Muzzle.SetRotation(Turn * Muzzle.GetRotation());
    GripLocation = Pivot + Turn.RotateVector(GripLocation - Pivot);
}

void APenguinPawn::FinishWeaponPose()
{
    if (!bAimAfterPose && !bFireAfterPose)
    {
        return;
    }

    // The flipper pose is final here. Its attached components update after this callback.
    const FTransform Grip = WeaponGrip->GetRelativeTransform()
        * PenguinMesh->GetSocketTransform(WeaponGrip->GetAttachSocketName());
    FTransform Muzzle = WeaponReceiver->GetSocketTransform(MuzzleSocket, RTS_Component)
        * WeaponReceiver->GetRelativeTransform() * Grip;
    FVector GripLocation = Grip.GetLocation();
    if (bAimAfterPose)
    {
        bAimAfterPose = false;
        AlignBodyToCrosshair(Muzzle, GripLocation);
    }
    if (!bFireAfterPose)
    {
        return;
    }
    bFireAfterPose = false;
    const bool bSprintFire = IsSprintFiring();
    const float RateScale = FColdLootTuning::ForWorld(GetWorld()).Get(Tune::FireRate);
    const float Interval = 1.0f / FMath::Clamp(WeaponDefinition->RoundsPerSecond * RateScale, 1.0f, 30.0f);
    do
    {
        FireBullet(Muzzle, GripLocation);
        ShotCooldown += Interval;
        ShotRequestTime = 0.0f;
    } while (bFireHeld && ShotCooldown <= KINDA_SMALL_NUMBER);
    if (bSprintFire)
    {
        RollSprintSpread();
    }
}

void APenguinPawn::FireBullet(const FTransform& Muzzle, const FVector& GripLocation)
{
    WeaponHoldTime = WeaponHoldSeconds;
    const FVector Origin = Muzzle.GetLocation();
    const FVector Direction = Muzzle.GetRotation().GetForwardVector();
    constexpr float SinglePlayerSpeedMultiplier = 2.5f;
    const float ModeSpeed = GetNetMode() == NM_Standalone ? SinglePlayerSpeedMultiplier : 1.0f;
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    const float Speed = WeaponDefinition->BulletSpeed * ModeSpeed * Tuning.Get(Tune::BulletSpeed);

    // A wall between the grip and muzzle must stop the shot before it spawns.
    FHitResult Obstruction;
    const bool bMuzzleBlocked = TraceMuzzleObstruction(GripLocation, Muzzle, Obstruction);
    if (!bMuzzleBlocked)
    {
        SpawnBullet(Origin, Direction, Speed);
    }
    else
    {
        AColdLootBulletImpact::Play(*GetWorld(), Obstruction, WeaponDefinition->TracerMaterial);
        const float DamageScale = Cast<APenguinPawn>(Obstruction.GetActor()) ? 1.0f
            : Tuning.Get(Tune::TargetDamage);
        UGameplayStatics::ApplyPointDamage(Obstruction.GetActor(), WeaponDefinition->Damage * DamageScale, Direction,
            Obstruction, GetController(), this, nullptr);
    }

    if (GroundSpeed <= 2.0f || IsAiming())
    {
        Facing = BodyPivot->GetForwardVector().GetSafeNormal2D();
    }
    MulticastShot(Origin, Direction, Speed, !bMuzzleBlocked);
    AddShotRecoil();
}

void APenguinPawn::SpawnBullet(const FVector& Origin, const FVector& Direction, float Speed)
{
    FActorSpawnParameters Parameters;
    Parameters.Owner = this;
    Parameters.Instigator = this;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (AColdLootBullet* Bullet = GetWorld()->SpawnActor<AColdLootBullet>(
        Origin, Direction.Rotation(), Parameters))
    {
        Bullet->Launch(Direction, Speed, WeaponDefinition->Damage,
            WeaponDefinition->TracerMaterial);
    }
}

void APenguinPawn::MulticastShot_Implementation(FVector_NetQuantize Origin,
    FVector_NetQuantizeNormal Direction, float Speed, bool bSpawnTracer)
{
    if (!HasAuthority() && WeaponDefinition && bSpawnTracer)
    {
        SpawnBullet(Origin, Direction, Speed);
    }
    if (!IsLocallyControlled())
    {
        return;
    }
    if (EquippedFiringSound)
    {
        UGameplayStatics::PlaySound2D(this, EquippedFiringSound,
            FColdLootTuning::ForWorld(GetWorld()).Get(Tune::GunfireVolume),
            FMath::FRandRange(0.97f, 1.03f));
    }
    if (!HasAuthority())
    {
        AddShotRecoil();
    }
}

void APenguinPawn::ResetFlat()
{
    if (HasAuthority())
    {
        ResetAt(-450);
    }
    else
    {
        ServerAction(EPenguinAction::ReturnToBase);
        CameraArm->SetRelativeRotation(FRotator(-9, 0, 0));
    }
}
void APenguinPawn::ResetDownhill() { if (HasAuthority()) ResetAt(1050); }
void APenguinPawn::ResetUphill() { if (HasAuthority()) ResetAt(4700); }
void APenguinPawn::ResetRamp() { if (HasAuthority()) ResetAt(-230, -520); }

void APenguinPawn::ResetAt(double X, double Y)
{
    if (!Terrain)
    {
        return;
    }

    if (IsKnockedOut())
    {
        CombatState = FPenguinCombatState();
        OnRep_CombatState();
        ForceNetUpdate();
    }
    FVector Position = Terrain->GetActorLocation() + FVector(X, Y, 0);
    Position.Z = Terrain->HeightAt(Position.X, Position.Y) + 1.0;
    SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    Heading = Facing = FVector::ForwardVector;
    BodyPivot->SetWorldRotation(FRotator::ZeroRotator);
    CameraArm->SetRelativeRotation(FRotator(-9, 0, 0));
    GroundSpeed = CurrentGrade = DistanceThisSlide = PeakSlideSpeed = SlidePoseTime = 0;
    GaitPhase = SprintBlend = TurnLean = 0.0f;
    bSliding = bRecovering = bSprintEntry = bSprinting = false;
    bSlideAirborne = bDiving = false;
    bSwimming = bWaterBreaching = false;
    SwimVelocity = FVector::ZeroVector;
    SwimHeading = FRotator::ZeroRotator;
    SwimThrust = 0.0f;
    SwimEntryTime = 0.0f;
    SwimDepth = 0.0;
    Camera->SetRelativeLocation(Camera->GetRelativeLocation() - WaterCameraOffset);
    WaterCameraOffset = FVector::ZeroVector;
    Camera->PostProcessBlendWeight = 0.0f;
    WaterSkips = 0;
    SlideVerticalSpeed = 0.0;
    NextSlideLaunchDistance = 0.0f;
    SlideGroundLockRemaining = 0.0f;
    DivePitch = 0.0f;
    AimBlend = ShotCooldown = ShotRequestTime = 0.0f;
    bFireAfterPose = bAimAfterPose = false;
    CameraKick = SprintSpread = FVector2D::ZeroVector;
    RecoilOffset = RecoilGoal = FVector2D::ZeroVector;
    RecoilRecoveryDelay = 0.0f;
    RecoilShotIndex = 0;
    SprintFireBlend = WeaponHoldTime = 0.0f;
}

float APenguinPawn::GetGaitCycle(bool bSprint) const
{
    const UAnimSequence* Gait = bSprint ? SprintAnimation : WalkAnimation;
    return Gait ? FMath::Max(Gait->GetPlayLength(), 0.01f) : 0.8f;
}

float APenguinPawn::GetGaitSpeed(bool bSprint) const
{
    const float SpeedScale = FColdLootTuning::ForWorld(GetWorld()).Get(bSprint ? Tune::SprintSpeed : Tune::WalkSpeed);
    return (bSprint ? SprintDistancePerCycle : WalkDistancePerCycle) * SpeedScale / GetGaitCycle(bSprint);
}

void APenguinPawn::AdvanceGait(float Seconds)
{
    constexpr float BlendInSeconds = 0.31f;
    constexpr float BlendOutSeconds = 0.28f;
    SprintBlend = FMath::FInterpConstantTo(SprintBlend, bSprinting ? 1.0f : 0.0f, Seconds,
        1.0f / (bSprinting ? BlendInSeconds : BlendOutSeconds));

    const float Weight = FMath::SmoothStep(0.0f, 1.0f, SprintBlend);
    const float FullSpeed = FMath::Lerp(GetGaitSpeed(false), GetGaitSpeed(true), Weight);
    const float Cycle = FMath::Lerp(GetGaitCycle(false), GetGaitCycle(true), Weight);
    const float Cadence = FMath::Min(GroundSpeed / FullSpeed, 1.0f) / Cycle;
    GaitPhase = GroundSpeed > 1.0f ? FMath::Fmod(GaitPhase + Seconds * Cadence, 1.0f) : 0.0f;
}

void APenguinPawn::ToggleSlide()
{
    if (IsKnockedOut())
    {
        return;
    }
    if (!HasAuthority())
    {
        ServerAction(EPenguinAction::Slide);
        return;
    }
    if (!Terrain || bSwimming || IsAiming() || (HasWeapon() && WantsToFire()))
    {
        return;
    }
    if (bSlideAirborne)
    {
        if (!bDiving)
        {
            DivePitch = BodyPivot->GetForwardVector().Rotation().Pitch;
            if (!bSliding)
            {
                SlidePoseTime = 0.0f;
            }
            bDiving = bSliding = true;
            bWaterBreaching = bRecovering = bSprinting = bSprintEntry = false;
            Facing = Heading;
        }
        return;
    }
    if (bSliding)
    {
        StandUpFromSlide();
        return;
    }
    if (bRecovering)
    {
        return;
    }
    const bool bFromRest = GroundSpeed <= 1.0f;
    if (bFromRest)
    {
        Heading = Facing;
    }

    bSprintEntry = bSprintHeld && bSprinting && GroundSpeed >= GetGaitSpeed(true) * 0.60f;
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    const float EntryBoost = bSprintEntry ? SprintSlideEntryBoost * Tuning.Get(Tune::SlideSprintBoost)
        : SlideEntryBoost * Tuning.Get(Tune::SlideWalkingBoost);
    const float Cruise = SlideCruiseSpeed * Tuning.Get(Tune::SlideCruise);
    const float LaunchSpeed = bFromRest ? StationarySlidePush * Tuning.Get(Tune::SlideStandingBoost)
        : FMath::Max(Cruise, GroundSpeed + EntryBoost);
    const float Limit = SlideSpeedLimit * Tuning.Get(Tune::SlideMaximum);
    const float BoostedSpeed = FMath::Clamp(LaunchSpeed, GroundSpeed, FMath::Max(GroundSpeed, Limit));
    GroundSpeed = FMath::Lerp(BoostedSpeed, GroundSpeed, GetSlideBrakeInput());
    PeakSlideSpeed = GroundSpeed;
    DistanceThisSlide = SlidePoseTime = 0;
    NextSlideLaunchDistance = 0.0f;
    SlideGroundLockRemaining = SlideGroundLockSeconds * Tuning.Get(Tune::SlideGroundLock);
    bSliding = true;
}

float APenguinPawn::GetSlideBrakeInput() const
{
    return bSlideBrakeHeld ? 1.0f : FMath::Clamp(-ForwardInput, 0.0f, 1.0f);
}

void APenguinPawn::Jump()
{
    if (IsKnockedOut())
    {
        return;
    }
    if (!HasAuthority())
    {
        ServerAction(EPenguinAction::Jump);
        return;
    }
    if (!Terrain || bSlideAirborne || bSwimming
        || (bSliding && SlideGroundLockRemaining > 0.0f))
    {
        return;
    }

    const FVector Position = GetActorLocation();
    const FVector Normal = Terrain->NormalAt(Position.X, Position.Y);
    const double Rise = -FVector::DotProduct(Normal, Heading) / Normal.Z;
    const FVector Travel = (Heading + FVector::UpVector * Rise).GetSafeNormal();
    const float LiftScale = FColdLootTuning::ForWorld(GetWorld()).Get(bSliding ? Tune::SlideJump : Tune::Jump);
    SlideVerticalSpeed = (bSliding ? SlideJumpLift : JumpLift) * LiftScale + FMath::Max(0.0, Travel.Z * GroundSpeed);
    GroundSpeed *= Travel.Size2D();
    bSlideAirborne = true;
}

void APenguinPawn::InterruptSlideForWeapon()
{
    if (HasWeapon())
    {
        bSliding = bDiving = bRecovering = false;
    }
}

const AColdzoneMapTerrain* APenguinPawn::IslandTerrain() const
{
    return Cast<AColdzoneMapTerrain>(Terrain.Get());
}

void APenguinPawn::ToggleIslandMap()
{
    if (auto* HUD = GetIslandHUD(*this))
    {
        HUD->ToggleIslandMap();
    }
}

void APenguinPawn::ToggleGraphicsMenu()
{
    const auto* Player = Cast<APlayerController>(GetController());
    if (auto* HUD = Player ? Cast<AColdLootHUD>(Player->GetHUD()) : nullptr)
    {
        if (!HUD->IsGraphicsMenuOpen())
        {
            StopAim();
            StopFire();
            ShotRequestTime = 0.0f;
        }
        HUD->ToggleGraphicsMenu();
    }
}

void APenguinPawn::ToggleTuningMenu()
{
    const auto* Player = Cast<APlayerController>(GetController());
    if (auto* HUD = Player ? Cast<AColdLootHUD>(Player->GetHUD()) : nullptr)
    {
        HUD->ToggleTuningMenu();
    }
}

#if WITH_EDITOR
void APenguinPawn::PreviousIslandZone()
{
    if (auto* HUD = GetIslandHUD(*this); HUD && HUD->IsIslandMapOpen())
    {
        HUD->SelectIslandZone(-1, IslandTerrain()->Zones().Num());
    }
}

void APenguinPawn::NextIslandZone()
{
    if (auto* HUD = GetIslandHUD(*this); HUD && HUD->IsIslandMapOpen())
    {
        HUD->SelectIslandZone(1, IslandTerrain()->Zones().Num());
    }
}

void APenguinPawn::VisitIslandZone()
{
    if (auto* HUD = GetIslandHUD(*this); HUD && HUD->IsIslandMapOpen())
    {
        const FVector Spot = IslandTerrain()->ZoneLocation(HUD->IslandZoneSelection());
        const FVector Origin = IslandTerrain()->GetActorLocation();
        ResetAt(Spot.X - Origin.X, Spot.Y - Origin.Y);
        HUD->ToggleIslandMap();
    }
}

void APenguinPawn::CaptureIslandView()
{
    if (IslandTerrain())
    {
        const FString Image = FPaths::ScreenShotDir() / TEXT("Coldzone_ZoneReference.png");
        FScreenshotRequest::RequestScreenshot(Image, false, true);
    }
}
#endif

void APenguinPawn::StandUpFromSlide()
{
    const float MoveInput = FMath::Min(1.0, FVector2D(ForwardInput, RightInput).Size());
    if (!bSlideAirborne)
    {
        GroundSpeed = FMath::Min(GroundSpeed, MoveInput * GetGaitSpeed(bSprintHeld && !bAimHeld));
    }
    bSliding = false;
    bRecovering = SlideAnimation && SlidePoseTime > 0.0f;
    if (SlidePoseTime >= BellyPose)
    {
        SlidePoseTime = RecoverPose;
    }
}

void APenguinPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Terrain)
    {
        return;
    }

    if (IsKnockedOut())
    {
        if (IsLocallyControlled() && bRagdollActive)
        {
            CameraArm->SetWorldLocation(PenguinMesh->Bounds.Origin + FVector(0, 0, 85));
        }
        return;
    }
    if (!HasAuthority())
    {
        TickNetworkClient(DeltaSeconds);
        return;
    }
    const bool bNetworkGame = GetNetMode() != NM_Standalone;
    if (bNetworkGame && !IsLocallyControlled() && GetWorld()->GetTimeSeconds() - LastRemoteInputTime > 0.5)
    {
        ForwardInput = RightInput = 0.0f;
        bSprintHeld = bSlideBrakeHeld = bAimHeld = bFireHeld = false;
    }

    bAimAfterPose = false;
    const FRotator ViewYaw = GetLookYaw();
    const FVector MoveInput(ForwardInput, RightInput, 0);
    const FVector Wish = ViewYaw.RotateVector(MoveInput).GetClampedToMaxSize(1.0);
    const double StartYaw = Heading.Rotation().Yaw;
    // Substep the slope and steering so the run behaves consistently at different frame rates.
    for (float Remaining = FMath::Min(DeltaSeconds, 0.2f); Remaining > KINDA_SMALL_NUMBER;)
    {
        const float StepSeconds = FMath::Min(Remaining, 1.0f / 120.0f);
        Step(StepSeconds, Wish);
        Remaining -= StepSeconds;
    }

    const FVector Position = GetActorLocation();
    UpdateUnderwaterView(DeltaSeconds);
    if (bSwimming)
    {
        if (!IsHoldingWaterEntryPose())
        {
            BodyPivot->SetWorldRotation(FMath::RInterpTo(BodyPivot->GetComponentRotation(), SwimHeading,
                DeltaSeconds, 12.0));
        }
        UpdateWeapon(DeltaSeconds);
        if (bNetworkGame) PublishNetworkPose();
        return;
    }
    if (bWaterBreaching)
    {
        SwimHeading = FMath::RInterpTo(SwimHeading,
            (Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed).Rotation(), DeltaSeconds, 9.0);
        BodyPivot->SetWorldRotation(SwimHeading);
        UpdateWeapon(DeltaSeconds);
        if (bNetworkGame) PublishNetworkPose();
        return;
    }
    if (bDiving)
    {
        BodyPivot->SetWorldRotation(FRotator(DivePitch, Heading.Rotation().Yaw, 0.0));
        UpdateWeapon(DeltaSeconds);
        if (bNetworkGame) PublishNetworkPose();
        return;
    }
    FVector SurfaceUp = Terrain->NormalAt(Position.X, Position.Y);
    if (bSlideAirborne)
    {
        const double PosePitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(SlideVerticalSpeed, GroundSpeed)),
            -20.0, 23.0);
        SurfaceUp = bSliding
            ? FRotator(PosePitch, Heading.Rotation().Yaw, 0.0).RotateVector(FVector::UpVector)
            : FVector::UpVector;
    }
    float BellyWeight = 0.0f;
    if (bSliding || bRecovering)
    {
        BellyWeight = SlidePoseTime < RecoverPose ? FMath::Min(SlidePoseTime / BellyPose, 1.0f)
            : FMath::Clamp(1.0f - (SlidePoseTime - RecoverPose) / 0.60f, 0.0f, 1.0f);
    }
    UpdateWeapon(DeltaSeconds);
    const bool bMoving = GroundSpeed > 2.0f || !Wish.IsNearlyZero();
    const bool bAimFacing = CanFire() && (IsAiming() || WantsToFire() || WeaponHoldTime > 0.0f || !bMoving);
    // Align the raised weapon. The swinging sprint flipper would otherwise pull the body into the ground.
    bAimAfterPose = bAimFacing && (SprintBlend <= 0.10f || SprintFireBlend >= 0.99f);
    const float SprintSlopeWeight = bSliding || bRecovering ? 0.0f : SprintBlend;
    const float SlopeWeight = bAimFacing ? SprintSlopeWeight
        : FMath::Max(SprintSlopeWeight, 0.28f + 0.72f * BellyWeight);
    const FVector Up = FMath::Lerp(FVector::UpVector, SurfaceUp, SlopeWeight).GetSafeNormal();
    if (bAimFacing)
    {
        Facing = FRotator(0.0, GetWeaponAimDirection().Rotation().Yaw, 0.0).Vector();
    }
    else if (bMoving || bSliding)
    {
        Facing = Heading;
    }
    const double TurnDelta = FMath::FindDeltaAngleDegrees(StartYaw, Heading.Rotation().Yaw);
    const float Steering = bSliding ? FMath::Clamp(RightInput, -1.0f, 1.0f) : FMath::Clamp(TurnDelta / 3.0, -1.0, 1.0);
    const float LeanScale = FColdLootTuning::ForWorld(GetWorld()).Get(Tune::TurnLean);
    const float LeanTarget = bAimFacing || bSlideAirborne ? 0.0f : MaxTurnLean * Steering * LeanScale;
    TurnLean = bAimFacing ? 0.0f : FMath::FInterpTo(TurnLean, LeanTarget, DeltaSeconds, 14.0f);
    const FQuat GroundRotation = FRotationMatrix::MakeFromZX(Up, Facing).ToQuat();
    // Bank around the forward axis of the ground origin, between the penguin's feet.
    const FQuat Bank(FVector::ForwardVector, FMath::DegreesToRadians(-TurnLean));
    BodyPivot->SetWorldRotation(GroundRotation * Bank);
    if (bNetworkGame) PublishNetworkPose();
}

void APenguinPawn::AdvanceSlidePose(float Seconds)
{
    if (GroundSpeed <= 0.0f && SlidePoseTime >= BellyPose)
    {
        return;
    }
    SlidePoseTime += Seconds * (bSprintEntry ? 1.9f : 1.65f);
    if (SlidePoseTime >= RecoverPose)
    {
        SlidePoseTime = BellyPose + FMath::Fmod(SlidePoseTime - BellyPose, RecoverPose - BellyPose);
    }
}

void APenguinPawn::StepSlideFlight(float Seconds)
{
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    const double Gravity = AirGravity * Tuning.Get(Tune::Gravity);
    if (bDiving)
    {
        DivePitch = FMath::FInterpConstantTo(DivePitch, -90.0f, Seconds, DivePitchRate * Tuning.Get(Tune::DiveRate));
    }
    const FVector Start = GetActorLocation();
    const FVector Desired = Start + Heading * GroundSpeed * Seconds
        + FVector::UpVector * (SlideVerticalSpeed - 0.5 * Gravity * Seconds) * Seconds;
    SlideVerticalSpeed -= Gravity * Seconds;
    FVector End = Terrain->KeepInside(Desired);
    if (const auto Water = Terrain->WaterSurfaceAt(End.X, End.Y); Water && SlideVerticalSpeed <= 0.0
        && End.Z <= Water.GetValue() - (bWaterBreaching ? SwimDraft(SwimHeading) : 0.0))
    {
        if (!bDiving && !bWaterBreaching && WaterSkips < Tuning.Get(Tune::SkipCount)
            && GroundSpeed >= SkipMinSpeed * Tuning.Get(Tune::SkipSpeed)
            && -SlideVerticalSpeed <= GroundSpeed * SkipMaxFallRatio * Tuning.Get(Tune::SkipAngle))
        {
            ++WaterSkips;
            GroundSpeed *= 0.86f;
            SlideVerticalSpeed = FMath::Clamp(-SlideVerticalSpeed * 0.48, 115.0, 220.0) * Tuning.Get(Tune::SkipBounce);
            End.Z = Water.GetValue() + 10.0;
            SetActorLocation(End);
        }
        else
        {
            EnterWater(End, Water.GetValue(), Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed);
        }
        return;
    }
    const double Floor = Terrain->HeightAt(End.X, End.Y) + 1.0;
    const bool bAtEdge = !FVector2D(End).Equals(FVector2D(Desired), 0.1f);
    if (bAtEdge)
    {
        GroundSpeed = 0.0f;
    }
    if (End.Z <= Floor)
    {
        const FVector Normal = Terrain->NormalAt(End.X, End.Y);
        const double Rise = -FVector::DotProduct(Normal, Heading) / Normal.Z;
        const FVector Tangent = (Heading + FVector::UpVector * Rise).GetSafeNormal();
        const FVector Velocity = Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed;
        const double LandingSpeed = FVector::DotProduct(Velocity, Tangent);
        GroundSpeed = FMath::Clamp(LandingSpeed, 0.0,
            static_cast<double>(SlideSpeedLimit * Tuning.Get(Tune::SlideMaximum)));
        End.Z = Floor;
        if (bWaterBreaching)
        {
            FRotator Orbit = CameraArm->GetRelativeRotation();
            Orbit.Pitch = FMath::Clamp(Orbit.Pitch, -55.0, 8.0);
            CameraArm->SetRelativeRotation(Orbit);
        }
        bSlideAirborne = bDiving = bWaterBreaching = false;
        WaterSkips = 0;
        SlideVerticalSpeed = 0.0;
        NextSlideLaunchDistance = DistanceThisSlide + FVector::Distance(Start, End) + SlideLandingRunout;
        if (!bSliding)
        {
            const float MoveInput = FMath::Min(1.0, FVector2D(ForwardInput, RightInput).Size());
            GroundSpeed = FMath::Min(GroundSpeed, MoveInput * GetGaitSpeed(bSprintHeld && !bAimHeld));
        }
    }
    SetActorLocation(End);
    if (bSliding)
    {
        AdvanceSlidePose(Seconds);
        DistanceThisSlide += FVector::Distance(Start, End);
        PeakSlideSpeed = FMath::Max(PeakSlideSpeed, GroundSpeed);
        if (!bSlideAirborne && GroundSpeed <= SlideStopSpeed)
        {
            GroundSpeed = 0.0f;
        }
    }
}

bool APenguinPawn::TrySlideLaunch(float Seconds, const FVector& Start, const FVector& Travel, double Rise)
{
    if (SlideGroundLockRemaining > 0.0f || GroundSpeed < SlideLaunchMinSpeed
        || DistanceThisSlide < NextSlideLaunchDistance)
    {
        return false;
    }

    const FVector Ahead = Start + Heading * SlideCrestLookAhead;
    const FVector AheadNormal = Terrain->NormalAt(Ahead.X, Ahead.Y);
    const double AheadRise = -FVector::DotProduct(AheadNormal, Heading) / AheadNormal.Z;
    const double CrestCurvature = (Rise - AheadRise) / (SlideCrestLookAhead * FMath::Pow(1.0 + Rise * Rise, 1.5));
    // A fast slide can lift off where the surface drops away.
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    if (CrestCurvature * FMath::Square(GroundSpeed) <= AirGravity * 1.15 * Tuning.Get(Tune::Gravity))
    {
        return false;
    }

    bSlideAirborne = true;
    SlideVerticalSpeed = Travel.Z * GroundSpeed + RolloverLift * Tuning.Get(Tune::CrestLift);
    GroundSpeed *= Travel.Size2D();
    StepSlideFlight(Seconds);
    return true;
}

void APenguinPawn::Step(float Seconds, const FVector& Wish)
{
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    if (bSwimming)
    {
        StepSwimming(Seconds);
        return;
    }
    if (bRecovering)
    {
        // Reverse an interrupted dive from its exact pose. Once on the belly, play the authored stand-up.
        if (SlidePoseTime < BellyPose)
        {
            SlidePoseTime -= Seconds * 2.4f;
            bRecovering = SlidePoseTime > 0.0f;
        }
        else
        {
            SlidePoseTime += Seconds * 2.1f;
            bRecovering = SlidePoseTime < SlideAnimation->GetPlayLength();
        }
    }

    if (bSlideAirborne)
    {
        if (!Wish.IsNearlyZero())
        {
            const FVector Velocity = Heading * GroundSpeed;
            const FVector Target = Wish * FMath::Max(GroundSpeed, AirControlSpeed * Tuning.Get(Tune::AirSpeed));
            const float Acceleration = AirControlAcceleration * Tuning.Get(Tune::AirSteering);
            const FVector Adjusted = Velocity + (Target - Velocity).GetClampedToMaxSize(Acceleration * Seconds);
            GroundSpeed = Adjusted.Size2D();
            if (GroundSpeed > KINDA_SMALL_NUMBER)
            {
                Heading = Adjusted / GroundSpeed;
            }
        }
        StepSlideFlight(Seconds);
        return;
    }

    const bool bSprintMove = bSprintHeld && !IsAiming() && !Wish.IsNearlyZero();
    if (bSliding)
    {
        SlideGroundLockRemaining = FMath::Max(0.0f, SlideGroundLockRemaining - Seconds);
        const double SteerRate = SlideSteering * Tuning.Get(Tune::SlideSteering);
        const double Yaw = Heading.Rotation().Yaw + FMath::Clamp(RightInput, -1.0f, 1.0f) * SteerRate * Seconds;
        Heading = Facing = FRotator(0, Yaw, 0).Vector();
    }
    else if (!Wish.IsNearlyZero())
    {
        const double TurnRate = bSprintMove ? SprintTurnRate * Tuning.Get(Tune::SprintTurn)
            : WalkTurnRate * Tuning.Get(Tune::WalkTurn);
        const double TargetYaw = Wish.Rotation().Yaw;
        const double Yaw = GroundSpeed <= SlideStopSpeed || IsAiming() ? TargetYaw
            : FMath::FixedTurn(Heading.Rotation().Yaw, TargetYaw, TurnRate * Seconds);
        Heading = FRotator(0, Yaw, 0).Vector();
    }

    const FVector Start = GetActorLocation();
    const FVector Normal = Terrain->NormalAt(Start.X, Start.Y);
    // Intersect the vertical heading plane with the terrain tangent plane.
    const double Rise = -FVector::DotProduct(Normal, Heading) / Normal.Z;
    const FVector Travel = (Heading + FVector::UpVector * Rise).GetSafeNormal();
    CurrentGrade = FMath::RadiansToDegrees(FMath::Atan(Rise));
    const float PreviousSpeed = GroundSpeed;
    float MoveSeconds = Seconds;
    bool bStoppedSlide = false;
    if (bSliding)
    {
        const float GlideWeight = FMath::SmoothStep(460.0f, 1260.0f, PreviousSpeed);
        const float Friction = FMath::Lerp(FlatDeceleration * Tuning.Get(Tune::SlideFriction),
            FastSlideDeceleration * Tuning.Get(Tune::SlideFastFriction), GlideWeight) * Normal.Z;
        // The entry boost settles into a glide on flatter ground. Downhill grades can build speed again.
        const float Cruise = SlideCruiseSpeed * Tuning.Get(Tune::SlideCruise);
        const float BoostBraking = FMath::Max(0.0f, PreviousSpeed - Cruise) * 1.8f
            * FMath::SmoothStep(-0.12, 0.0, Travel.Z) * Tuning.Get(Tune::SlideBoostDecay);
        const float BrakeInput = GetSlideBrakeInput();
        const float Acceleration = -SlopeGravity * Tuning.Get(Tune::SlideSlope) * Travel.Z - Friction - BoostBraking
            - BrakeInput * SlideBrakeDeceleration * Tuning.Get(Tune::SlideBrake);
        GroundSpeed = FMath::Clamp(PreviousSpeed + Acceleration * Seconds, 0.0f,
            SlideSpeedLimit * Tuning.Get(Tune::SlideMaximum));
        if (GroundSpeed <= SlideStopSpeed && Acceleration <= 0.0f)
        {
            bStoppedSlide = true;
            MoveSeconds = PreviousSpeed > SlideStopSpeed
                ? Seconds * (PreviousSpeed - SlideStopSpeed) / (PreviousSpeed - GroundSpeed)
                : 0.0f;
            GroundSpeed = PreviousSpeed > SlideStopSpeed ? SlideStopSpeed : 0.0f;
        }
        AdvanceSlidePose(Seconds);
        PeakSlideSpeed = FMath::Max(PeakSlideSpeed, GroundSpeed);
        if (TrySlideLaunch(Seconds, Start, Travel, Rise))
        {
            return;
        }
    }
    else
    {
        bSprinting = bSprintMove;
        const float TargetSpeed = Wish.Size() * GetGaitSpeed(bSprinting);
        const float Rate = TargetSpeed > GroundSpeed ? 3100.0f * Tuning.Get(Tune::GroundAcceleration)
            : 4000.0f * Tuning.Get(Tune::GroundBraking);
        GroundSpeed = FMath::FInterpConstantTo(GroundSpeed, TargetSpeed, Seconds, Rate);
        AdvanceGait(Seconds);
    }

    const FVector Desired = Start + Travel * ((PreviousSpeed + GroundSpeed) * 0.5f * MoveSeconds);
    FVector End = Terrain->KeepInside(Desired);
    if (const auto Water = Terrain->WaterSurfaceAt(End.X, End.Y); Water)
    {
        if (bSliding && SlideGroundLockRemaining <= 0.0f)
        {
            bSlideAirborne = true;
            SlideVerticalSpeed = Travel.Z * GroundSpeed;
            GroundSpeed *= Travel.Size2D();
            StepSlideFlight(Seconds);
        }
        else
        {
            EnterWater(End, Water.GetValue(), Heading * GroundSpeed);
        }
        return;
    }
    End.Z = Terrain->HeightAt(End.X, End.Y) + 1.0;
    SetActorLocation(End);

    if (bSliding)
    {
        DistanceThisSlide += FVector::Distance(Start, End);
        if (bStoppedSlide || !FVector2D(End).Equals(FVector2D(Desired), 0.1f))
        {
            GroundSpeed = 0.0f;
        }
    }
}

void APenguinPawn::EnterWater(FVector Position, double SurfaceZ, const FVector& EntryVelocity)
{
    bSwimming = true;
    bSprinting = bSliding = bSlideAirborne = bDiving = bWaterBreaching = bRecovering = bSprintEntry = false;
    bAimHeld = bFireHeld = false;
    ShotRequestTime = 0.0f;
    SlideVerticalSpeed = CurrentGrade = 0.0f;
    SlideGroundLockRemaining = 0.0f;
    SwimThrust = 0.0f;
    const double EntrySpeed = EntryVelocity.Size();
    SwimVelocity = EntryVelocity;
    SwimEntryTime = 0.0f;
    SwimHeading = EntrySpeed > 60.0 ? SwimVelocity.Rotation() : GetLookYaw();
    const double Bed = Terrain->HeightAt(Position.X, Position.Y);
    const double SurfaceDraft = SurfaceZ - SwimDraft(SwimHeading);
    Position.Z = FMath::Clamp(Position.Z, FMath::Min(Bed + SwimSeabedClearance, SurfaceDraft), SurfaceDraft);
    SwimDepth = SurfaceZ - Position.Z;
    GroundSpeed = SwimVelocity.Size();
    SetActorLocation(Position);
}

bool APenguinPawn::IsHoldingWaterEntryPose() const
{
    return bSwimming && (HasAuthority()
        ? SwimEntryTime < WaterEntryPoseSeconds * FColdLootTuning::ForWorld(GetWorld()).Get(Tune::WaterEntryHold)
        : NetworkPose.bHoldingWaterEntryPose);
}

void APenguinPawn::StepSwimming(float Seconds)
{
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    // The third-person camera normally sits nine degrees below the horizon.
    const FRotator Aim(FMath::Clamp(CameraArm->GetRelativeRotation().Pitch + 9.0, -70.0, 70.0), GetLookYaw().Yaw, 0.0);
    SwimHeading = FMath::RInterpTo(SwimHeading, Aim, Seconds, 11.0 * Tuning.Get(Tune::SwimSteering));
    const FVector Forward = SwimHeading.Vector();
    SwimThrust = FMath::Clamp(ForwardInput, 0.0f, 1.0f);
    const FVector Along = Forward * FVector::DotProduct(SwimVelocity, Forward);
    const FVector Slip = SwimVelocity - Along;
    // Water drag rises with speed. A stronger cross-flow drag lets entry momentum carry through a turn briefly.
    SwimEntryTime += Seconds;
    const float EntryDuration = WaterEntryPoseSeconds * Tuning.Get(Tune::WaterEntryHold);
    const double EntryBlend = EntryDuration > 0.0f ? FMath::SmoothStep(0.0f, EntryDuration, SwimEntryTime) : 1.0;
    const double SpeedScale = Tuning.Get(Tune::SwimSpeed);
    const double WaterDrag = Tuning.Get(Tune::WaterDrag);
    const double PaddleDrag = (1.45 + .0022 * SwimVelocity.Size() / (SwimSpeedMultiplier * SpeedScale)) * WaterDrag;
    const double Drag = FMath::Lerp(0.70 * Tuning.Get(Tune::WaterEntryDrag), PaddleDrag, EntryBlend);
    SwimVelocity = (Along + Slip * FMath::Exp(-6.0 * WaterDrag * EntryBlend * Seconds)) * FMath::Exp(-Drag * Seconds);
    SwimVelocity += Forward * (SwimThrust * SwimAcceleration * SpeedScale * Seconds);
    const double Draft = SwimDraft(SwimHeading);
    const bool bSkimming = SwimThrust > 0.0f && SwimDepth < Draft + 42.0
        && SwimHeading.Pitch >= -4.0 && SwimVelocity.Z > -75.0;
    if (bSkimming)
    {
        SwimVelocity.Z += 240.0 * SwimThrust * Seconds * Tuning.Get(Tune::SurfaceLift);
    }
    else
    {
        SwimVelocity.Z -= FMath::Lerp(420.0, 90.0, SwimThrust) * Seconds * Tuning.Get(Tune::Sink);
    }

    const FVector Start = GetActorLocation();
    FVector End = Terrain->KeepInside(Start + SwimVelocity * Seconds);
    if (const auto Water = Terrain->WaterSurfaceAt(End.X, End.Y); Water)
    {
        const double Surface = Water.GetValue();
        const double Seabed = Terrain->HeightAt(End.X, End.Y);
        const double SurfaceDraft = Surface - Draft;
        const double Bottom = FMath::Min(SurfaceDraft, Seabed + SwimSeabedClearance);
        const double Speed = SwimVelocity.Size();
        if (End.Z >= SurfaceDraft && SwimThrust > 0.0f && SwimVelocity.Z >= SwimBreachRise * Tuning.Get(Tune::BreachRise)
            && Speed >= SwimBreachSpeed * Tuning.Get(Tune::BreachSpeed))
        {
            // Keep the approach velocity and let gravity carry the penguin through the wave and back down.
            bSwimming = false;
            bSlideAirborne = bWaterBreaching = true;
            SlideVerticalSpeed = SwimVelocity.Z;
            Heading = Facing = SwimVelocity.GetSafeNormal2D();
            GroundSpeed = SwimVelocity.Size2D();
            SwimVelocity = FVector::ZeroVector;
            SwimThrust = 0.0f;
            SwimDepth = 0.0;
            SetActorLocation(End);
            return;
        }
        if (bSkimming && SwimDepth < Draft + 10.0 && SwimVelocity.Z >= -12.0)
        {
            End.Z = SurfaceDraft;
        }
        if (End.Z >= SurfaceDraft)
        {
            SwimVelocity.Z = FMath::Min(SwimVelocity.Z, 0.0);
        }
        else if (End.Z <= Bottom)
        {
            const double BedVelocity = (Seabed - Terrain->HeightAt(Start.X, Start.Y)) / Seconds;
            SwimVelocity.Z = FMath::Max(SwimVelocity.Z, FMath::Min(0.0, BedVelocity));
        }
        End.Z = FMath::Clamp(End.Z, Bottom, SurfaceDraft);
        SwimDepth = Surface - End.Z;
        GroundSpeed = SwimVelocity.Size();
    }
    else if (SwimDepth > Draft + 34.0)
    {
        // A submerged swimmer cannot snap up onto dry ice through the side of the bank.
        End.X = Start.X;
        End.Y = Start.Y;
        SwimVelocity.X = SwimVelocity.Y = 0.0;
        const double Surface = Terrain->WaterSurfaceAt(Start.X, Start.Y).Get(Start.Z + SwimDepth);
        const double SurfaceDraft = Surface - Draft;
        const double Bottom = FMath::Min(SurfaceDraft, Terrain->HeightAt(Start.X, Start.Y) + SwimSeabedClearance);
        End.Z = FMath::Clamp(End.Z, Bottom, SurfaceDraft);
        SwimDepth = Surface - End.Z;
        GroundSpeed = FMath::Abs(SwimVelocity.Z);
    }
    else
    {
        bSwimming = false;
        WaterSkips = 0;
        SwimThrust = 0.0f;
        SwimDepth = 0.0;
        Heading = Facing = Forward.GetSafeNormal2D();
        GroundSpeed = FMath::Min(SwimVelocity.Size2D(), GetGaitSpeed(false));
        SwimVelocity = FVector::ZeroVector;
        FRotator Orbit = CameraArm->GetRelativeRotation();
        Orbit.Pitch = FMath::Clamp(Orbit.Pitch, -55.0, 8.0);
        CameraArm->SetRelativeRotation(Orbit);
        End.Z = Terrain->HeightAt(End.X, End.Y) + 1.0;
    }
    SetActorLocation(End);
}

void APenguinPawn::UpdateUnderwaterView(float DeltaSeconds)
{
    if (bSwimming || bWaterBreaching || !WaterCameraOffset.IsNearlyZero())
    {
        const FTransform Socket = CameraArm->GetSocketTransform(USpringArmComponent::SocketName);
        const FVector Mount = Camera->GetRelativeLocation() - WaterCameraOffset;
        const FVector UncorrectedEye = Socket.TransformPosition(Mount);
        const auto Surface = Terrain->WaterSurfaceAt(UncorrectedEye.X, UncorrectedEye.Y);
        const double SurfaceBlend = bWaterBreaching ? 1.0 : bSwimming
            ? 1.0 - FMath::SmoothStep(10.0, 97.0, SwimDepth - SwimDraft(SwimHeading)) : 0.0;
        const double Raise = Surface ? FMath::Max(0.0, Surface.GetValue() + 28.0 - UncorrectedEye.Z) * SurfaceBlend : 0.0;
        WaterCameraOffset = Socket.InverseTransformVector(FVector::UpVector * Raise);
        Camera->SetRelativeLocation(Mount + WaterCameraOffset);
    }
    const FVector Eye = Camera->GetComponentLocation();
    const auto Water = Terrain->WaterSurfaceAt(Eye.X, Eye.Y);
    const float Target = Water ? FMath::SmoothStep(5.0, 140.0, Water.GetValue() - Eye.Z) : 0.0f;
    Camera->PostProcessBlendWeight = FMath::FInterpConstantTo(Camera->PostProcessBlendWeight, Target, DeltaSeconds, 5.0f);
}

const TCHAR* APenguinPawn::MotionLabel() const
{
    if (IsKnockedOut())
    {
        return TEXT("KNOCKED OUT");
    }
    if (bDiving)
    {
        return TEXT("DIVING");
    }
    if (bWaterBreaching)
    {
        return TEXT("BREACHING");
    }
    if (bSwimming)
    {
        if (IsHoldingWaterEntryPose())
        {
            return TEXT("WATER ENTRY");
        }
        return SwimDepth > SwimDraft(SwimHeading) + 34.0 ? TEXT("UNDERWATER") : TEXT("SWIMMING");
    }
    if (bSlideAirborne)
    {
        if (WaterSkips > 0)
        {
            return TEXT("SKIPPING");
        }
        return bSliding ? TEXT("AIRBORNE SLIDE") : TEXT("AIRBORNE");
    }
    if (bSliding)
    {
        if (GroundSpeed <= 0.0f)
        {
            return TEXT("ON BELLY");
        }
        return bSprintEntry ? TEXT("SPRINT SLIDE") : TEXT("BELLY SLIDE");
    }
    if (bRecovering)
    {
        return TEXT("RECOVERING");
    }
    if (IsAiming())
    {
        return TEXT("AIMING");
    }
    return bSprinting ? TEXT("SPRINT") : GroundSpeed > 1.0f ? TEXT("WALK") : TEXT("READY");
}
