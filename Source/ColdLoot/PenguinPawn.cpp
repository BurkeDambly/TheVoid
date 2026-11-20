#include "PenguinPawn.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ColdLootBullet.h"
#include "ColdLootBulletImpact.h"
#include "ColdLootGameMode.h"
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
#include "PenguinAnimInstance.h"
#include "PenguinCameraArm.h"
#include "SlideParkTerrain.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
// Source frames 1-18 tip onto the belly. Frames 18-36 are the glide.
constexpr float BellyPose = 17.0f / 30.0f;
constexpr float RecoverPose = 35.0f / 30.0f;
constexpr float SlideStopSpeed = 65.0f;
constexpr float EditorSlideDriveScale = 16.0f;
constexpr double SwimAcceleration = 3350.0;
constexpr double SwimBreachSpeed = 805.0;
constexpr double SwimBreachRise = 550.0;
constexpr double SwimSeabedClearance = 38.0;
constexpr double BaseSlideAirGravity = 980.0;
constexpr double SlideLaunchMinSpeed = 660.0;
constexpr double SlideCrestLookAhead = 65.0;
constexpr double UphillLaunchMinSpeed = 1000.0;
constexpr double UphillLaunchRise = 0.20;
constexpr double UphillLaunchLift = 460.0;
constexpr double RolloverLift = 85.0;
constexpr float SlideLandingRunout = 400.0f;
constexpr double BoreReticleDistance = 12000.0;
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

void APenguinPawn::BeginPlay()
{
    Super::BeginPlay();
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
        FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &APenguinPawn::FireFromUpdatedPose));
    PenguinMesh->SetRelativeRotation(FRotator(0, MeshYawOffset, 0));
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
    PenguinFormGlow = PenguinFormMaterial.LoadSynchronous();
    if (PenguinFormGlow)
    {
        PenguinFormGlow->EnsureIsComplete();
    }
    EditorPenguinFormGlow = EditorPenguinFormMaterial.LoadSynchronous();
    if (EditorPenguinFormGlow)
    {
        EditorPenguinFormGlow->EnsureIsComplete();
    }
    ResetFlat();
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
    Input->BindAction(TEXT("PenguinForm"), IE_Pressed, this, &APenguinPawn::TogglePenguinForm);
    Input->BindKey(EKeys::M, IE_Pressed, this, &APenguinPawn::ToggleIslandMap);
    Input->BindKey(EKeys::E, IE_Pressed, this, &APenguinPawn::ToggleEditorPenguinForm);
    Input->BindKey(EKeys::F2, IE_Pressed, this, &APenguinPawn::ToggleGraphicsMenu).bExecuteWhenPaused = true;
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
    return WeaponDefinition && WeaponReceiver->GetStaticMesh() && bAimHeld && !bSliding && !bSlideAirborne && !bRecovering && !bSwimming;
}

bool APenguinPawn::HasWeapon() const
{
    return WeaponDefinition && WeaponReceiver->DoesSocketExist(MuzzleSocket);
}

bool APenguinPawn::CanFire() const
{
    return HasWeapon() && !bSliding && !bSlideAirborne && !bRecovering && !bSwimming;
}

float APenguinPawn::ReticleGap() const
{
    return FMath::Lerp(bSprinting ? 16.0f : 10.0f, 4.0f, AimBlend);
}

bool APenguinPawn::TraceMuzzleObstruction(const FVector& GripLocation, const FVector& MuzzleLocation, FHitResult& Hit) const
{
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(MuzzleClearance), true, this);
    return GetWorld()->LineTraceSingleByChannel(Hit, GripLocation, MuzzleLocation, AColdLootBullet::CollisionChannel, Query);
}

FVector APenguinPawn::GetBoreReticlePoint() const
{
    const FTransform Muzzle = WeaponReceiver->GetSocketTransform(MuzzleSocket);
    return Muzzle.GetLocation() + Muzzle.GetRotation().GetForwardVector() * BoreReticleDistance;
}

FVector APenguinPawn::GetWeaponAimDirection() const
{
    const FQuat Look = CameraArm->GetTargetRotation().Quaternion() * Camera->GetRelativeRotation().Quaternion();
    return Look.RotateVector(FVector(1.0, SprintSpread.X, SprintSpread.Y)).GetSafeNormal();
}

void APenguinPawn::RollSprintSpread()
{
    const double Angle = FMath::FRandRange(0.0, UE_TWO_PI);
    const double Radius = FMath::Tan(FMath::DegreesToRadians(FMath::FRandRange(1.0, 3.25)));
    SprintSpread = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
}

void APenguinPawn::StartAim()
{
    bAimHeld = true;
    if (IsAiming())
    {
        bSprinting = false;
    }
}

void APenguinPawn::StartFire()
{
    bFireHeld = true;
    if (CanFire() && ShotCooldown <= 0.0f)
    {
        ShotRequestTime = 0.25f;
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
    const double Scale = RecoilStrength * (bSprinting ? 1.60 : FMath::Lerp(1.20, 1.0, AimBlend));
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
    const double KickScale = RecoilStrength * 1.44 * (bSprinting ? 2.6 : 1.0);
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

    const bool bADS = IsAiming();
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
    AimBlend = FMath::Lerp(AimBlend, bADS ? 1.0f : 0.0f, 1.0f - FMath::Exp(-15.0f * DeltaSeconds));
    CameraArm->TargetArmLength = FMath::Lerp(HipArmLength, AimCameraArmLength, AimBlend);
    CameraArm->SocketOffset = FMath::Lerp(HipSocketOffset, FVector(0, 196, 1), AimBlend);
    Camera->SetFieldOfView(FMath::Lerp(HipFieldOfView, 60.0f, AimBlend));
    UpdateRecoil(DeltaSeconds);
    UpdateCameraRecoil();
    WeaponGrip->SetVisibility(!bSwimming && !bSliding && !bSlideAirborne && !bRecovering
        && (!bSprinting || SprintBlend < 0.35f || SprintFireBlend > 0.3f), true);
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

void APenguinPawn::FireFromUpdatedPose()
{
    if (!bFireAfterPose)
    {
        return;
    }
    bFireAfterPose = false;
    const bool bSprintFire = IsSprintFiring();
    // The flipper pose is final here. Its attached components update after this callback.
    const FTransform Grip = WeaponGrip->GetRelativeTransform()
        * PenguinMesh->GetSocketTransform(WeaponGrip->GetAttachSocketName());
    const FTransform Muzzle = WeaponReceiver->GetSocketTransform(MuzzleSocket, RTS_Component)
        * WeaponReceiver->GetRelativeTransform() * Grip;
    const float Interval = 1.0f / FMath::Clamp(WeaponDefinition->RoundsPerSecond, 1.0f, 30.0f);
    do
    {
        FireBullet(Muzzle, Grip.GetLocation());
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

    // A wall between the grip and muzzle must stop the shot before it spawns.
    FHitResult Obstruction;
    const bool bMuzzleBlocked = TraceMuzzleObstruction(GripLocation, Origin, Obstruction);
    if (!bMuzzleBlocked)
    {
        FActorSpawnParameters Parameters;
        Parameters.Owner = this;
        Parameters.Instigator = this;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (AColdLootBullet* Bullet = GetWorld()->SpawnActor<AColdLootBullet>(
            Origin, Direction.Rotation(), Parameters))
        {
            Bullet->Launch(Direction, WeaponDefinition->BulletSpeed, WeaponDefinition->Damage,
                WeaponDefinition->TracerMaterial);
        }
    }
    else
    {
        AColdLootBulletImpact::Play(*GetWorld(), Obstruction, WeaponDefinition->TracerMaterial);
        UGameplayStatics::ApplyPointDamage(Obstruction.GetActor(), WeaponDefinition->Damage, Direction,
            Obstruction, GetController(), this, nullptr);
    }

    if (GroundSpeed <= 2.0f || IsAiming())
    {
        Facing = BodyPivot->GetForwardVector().GetSafeNormal2D();
    }
    if (EquippedFiringSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, EquippedFiringSound, Origin, 1.0f,
            FMath::FRandRange(0.97f, 1.03f));
    }
    AddShotRecoil();
}

void APenguinPawn::ResetFlat() { ResetAt(-450); }
void APenguinPawn::ResetDownhill() { ResetAt(1050); }
void APenguinPawn::ResetUphill() { ResetAt(4700); }
void APenguinPawn::ResetRamp() { ResetAt(-230, -520); }

void APenguinPawn::ResetAt(double X, double Y)
{
    if (!Terrain)
    {
        return;
    }

    FVector Position = Terrain->GetActorLocation() + FVector(X, Y, 0);
    Position.Z = Terrain->HeightAt(Position.X, Position.Y) + 1.0;
    SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
    Heading = Facing = FVector::ForwardVector;
    BodyPivot->SetWorldRotation(FRotator::ZeroRotator);
    CameraArm->SetRelativeRotation(FRotator(-9, 0, 0));
    GroundSpeed = CurrentGrade = DistanceThisSlide = PeakSlideSpeed = SlidePoseTime = 0;
    GaitPhase = SprintBlend = 0.0f;
    bSliding = bRecovering = bSprintEntry = bSprinting = false;
    bSlideAirborne = false;
    bSwimming = bWaterBreaching = false;
    SwimVelocity = FVector::ZeroVector;
    SwimHeading = FRotator::ZeroRotator;
    SwimThrust = 0.0f;
    SwimDepth = 0.0;
    Camera->SetRelativeLocation(Camera->GetRelativeLocation() - WaterCameraOffset);
    WaterCameraOffset = FVector::ZeroVector;
    Camera->PostProcessBlendWeight = 0.0f;
    bCanLaunchOnUphill = true;
    SlideVerticalSpeed = 0.0;
    SlideFlightSpeedScale = 1.0f;
    NextSlideLaunchDistance = 0.0f;
    AimBlend = ShotCooldown = ShotRequestTime = 0.0f;
    bFireAfterPose = false;
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
    return (bSprint ? SprintDistancePerCycle : WalkDistancePerCycle) / GetGaitCycle(bSprint);
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
    if (bSliding)
    {
        StandUpFromSlide();
        return;
    }
    if (!Terrain || bSlideAirborne || bRecovering || bSwimming)
    {
        return;
    }
    const bool bFromRest = GroundSpeed <= 1.0f;
    if (bFromRest)
    {
        Heading = Facing;
    }

    bSprintEntry = bSprintHeld && bSprinting && GroundSpeed >= GetGaitSpeed(true) * 0.60f;
    const float LaunchSpeed = bFromRest ? StationarySlidePush : GroundSpeed;
    GroundSpeed = FMath::Min(GetSlideSpeedLimit(), LaunchSpeed * GetSlideSpeedScale());
    PeakSlideSpeed = GroundSpeed;
    DistanceThisSlide = SlidePoseTime = 0;
    bCanLaunchOnUphill = true;
    NextSlideLaunchDistance = 0.0f;
    bSliding = true;
}

void APenguinPawn::TogglePenguinForm()
{
    SetForm(ActiveForm == EForm::Penguin ? EForm::Normal : EForm::Penguin);
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

void APenguinPawn::ToggleEditorPenguinForm()
{
    SetForm(IsEditorPenguinFormActive() ? EForm::Normal : EForm::EditorPenguin);
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

void APenguinPawn::SetForm(EForm Form)
{
    const float PreviousScale = GetSlideSpeedScale();
    ActiveForm = Form;
    UMaterialInterface* Glow = IsPenguinFormActive() ? PenguinFormGlow.Get() : nullptr;
    if (IsEditorPenguinFormActive())
    {
        Glow = EditorPenguinFormGlow.Get();
    }
    PenguinMesh->SetOverlayMaterial(Glow);
    if (bSliding && !bSlideAirborne)
    {
        GroundSpeed = FMath::Min(GetSlideSpeedLimit(), GroundSpeed * GetSlideSpeedScale() / PreviousScale);
        PeakSlideSpeed = FMath::Max(PeakSlideSpeed, GroundSpeed);
    }
}

float APenguinPawn::GetSlideSpeedScale() const
{
    const float FormScale = IsPenguinFormActive() ? FormSlideSpeedMultiplier : 1.0f;
    // The editor form uses the same terrain glide at a higher speed. Its W drive is tuned separately.
    return IsEditorPenguinFormActive() ? FormScale * (EditorSlideDriveScale / 3.0f) : FormScale;
}

float APenguinPawn::GetSlideSpeedLimit() const
{
    const float Limit = IsPenguinFormActive() ? FormSlideSpeedLimit * GetSlideSpeedScale() : SlideSpeedLimit;
    return IsEditorPenguinFormActive() ? FMath::Max(Limit, GetGaitSpeed(true) * EditorSlideDriveScale) : Limit;
}

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

    const FRotator ViewYaw = GetLookYaw();
    const FVector MoveInput(ForwardInput, RightInput, 0);
    const FVector Wish = ViewYaw.RotateVector(MoveInput).GetClampedToMaxSize(1.0);
    // Substep the slope and steering so the run behaves consistently at different frame rates.
    for (float Remaining = FMath::Min(DeltaSeconds, 0.2f); Remaining > KINDA_SMALL_NUMBER;)
    {
        const float StepScale = bSlideAirborne ? SlideFlightSpeedScale : bSliding ? GetSlideSpeedScale() : 1.0f;
        const float StepSeconds = FMath::Min(Remaining, 1.0f / (120.0f * StepScale));
        Step(StepSeconds, Wish);
        Remaining -= StepSeconds;
    }

    const FVector Position = GetActorLocation();
    UpdateUnderwaterView(DeltaSeconds);
    if (bSwimming)
    {
        BodyPivot->SetWorldRotation(SwimHeading);
        UpdateWeapon(DeltaSeconds);
        return;
    }
    if (bWaterBreaching)
    {
        SwimHeading = FMath::RInterpTo(SwimHeading,
            (Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed).Rotation(), DeltaSeconds, 9.0);
        BodyPivot->SetWorldRotation(SwimHeading);
        UpdateWeapon(DeltaSeconds);
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
    const FVector Up = FMath::Lerp(FVector::UpVector, SurfaceUp, 0.28f + 0.72f * BellyWeight).GetSafeNormal();
    UpdateWeapon(DeltaSeconds);
    const bool bAimFacing = CanFire();
    const bool bMoving = !MoveInput.IsNearlyZero() || GroundSpeed > 2.0f;
    const bool bFaceView = bAimFacing || (!bSliding && bMoving);
    const FVector ViewFacing = bAimFacing
        ? FRotator(0, GetWeaponAimDirection().Rotation().Yaw, 0).Vector()
        : ViewYaw.Vector();
    if (bFaceView)
    {
        Facing = ViewFacing;
    }
    const FQuat Target = FRotationMatrix::MakeFromZX(
        bAimFacing ? FVector::UpVector : Up,
        bFaceView ? ViewFacing : Facing).ToQuat();
    BodyPivot->SetWorldRotation(bAimFacing ? Target : FQuat::Slerp(BodyPivot->GetComponentQuat(), Target,
        1.0 - FMath::Exp(-17.0 * DeltaSeconds)));
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
    const FVector Start = GetActorLocation();
    const FVector Desired = Start + Heading * GroundSpeed * Seconds
        + FVector::UpVector * (SlideVerticalSpeed - 0.5 * SlideAirGravity * Seconds) * Seconds;
    SlideVerticalSpeed -= SlideAirGravity * Seconds;
    FVector End = Terrain->KeepInside(Desired);
    const double EntryDraft = bWaterBreaching ? SwimDraft(SwimHeading) : 18.0;
    if (const auto Water = Terrain->WaterSurfaceAt(End.X, End.Y);
        Water && End.Z <= Water.GetValue() - EntryDraft && (!bWaterBreaching || SlideVerticalSpeed <= 0.0))
    {
        EnterWater(End, Water.GetValue(), Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed);
        return;
    }
    const double Floor = Terrain->HeightAt(End.X, End.Y) + 1.0;
    const bool bAtEdge = !FVector2D(End).Equals(FVector2D(Desired), 0.1f);
    if (End.Z <= Floor || bAtEdge)
    {
        const FVector Normal = Terrain->NormalAt(End.X, End.Y);
        const double Rise = -FVector::DotProduct(Normal, Heading) / Normal.Z;
        const FVector Tangent = (Heading + FVector::UpVector * Rise).GetSafeNormal();
        const FVector Velocity = Heading * GroundSpeed + FVector::UpVector * SlideVerticalSpeed;
        const double LandingSpeed = FVector::DotProduct(Velocity, Tangent) * GetSlideSpeedScale() / SlideFlightSpeedScale;
        GroundSpeed = FMath::Clamp(LandingSpeed, 0.0, static_cast<double>(GetSlideSpeedLimit()));
        End.Z = Floor;
        if (bWaterBreaching)
        {
            FRotator Orbit = CameraArm->GetRelativeRotation();
            Orbit.Pitch = FMath::Clamp(Orbit.Pitch, -55.0, 8.0);
            CameraArm->SetRelativeRotation(Orbit);
        }
        bSlideAirborne = bWaterBreaching = false;
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
        if (bAtEdge || (!bSlideAirborne && GroundSpeed <= SlideStopSpeed * GetSlideSpeedScale()))
        {
            GroundSpeed = 0.0f;
        }
    }
}

bool APenguinPawn::TrySlideLaunch(float Seconds, const FVector& Start, const FVector& Travel, double Rise)
{
    if (Rise < 0.07)
    {
        bCanLaunchOnUphill = true;
    }
    const float SpeedScale = GetSlideSpeedScale();
    const float BaseSpeed = GroundSpeed / SpeedScale;
    if (BaseSpeed < SlideLaunchMinSpeed || DistanceThisSlide < NextSlideLaunchDistance)
    {
        return false;
    }

    const FVector Ahead = Start + Heading * SlideCrestLookAhead;
    const FVector AheadNormal = Terrain->NormalAt(Ahead.X, Ahead.Y);
    const double AheadRise = -FVector::DotProduct(AheadNormal, Heading) / AheadNormal.Z;
    const double CrestCurvature = (Rise - AheadRise) / (SlideCrestLookAhead * FMath::Pow(1.0 + Rise * Rise, 1.5));
    const double BaseAirGravity = BaseSlideAirGravity * FormAirGravityMultiplier;
    // Curvature can lift a fast slide when the surface rolls over in front of it, even on a downhill grade.
    const bool bUphillLaunch = bCanLaunchOnUphill && BaseSpeed >= UphillLaunchMinSpeed
        && Rise >= UphillLaunchRise && AheadRise >= Rise - 0.025;
    const bool bRolloverLaunch = CrestCurvature * FMath::Square(BaseSpeed) > BaseAirGravity * 1.15;
    if (!bUphillLaunch && !bRolloverLaunch)
    {
        return false;
    }

    bCanLaunchOnUphill = false;
    bSlideAirborne = true;
    SlideFlightSpeedScale = SpeedScale;
    SlideAirGravity = BaseAirGravity * FMath::Square(SpeedScale);
    SlideVerticalSpeed = Travel.Z * GroundSpeed + (bUphillLaunch ? UphillLaunchLift : RolloverLift) * SpeedScale;
    GroundSpeed *= Travel.Size2D();
    StepSlideFlight(Seconds);
    return true;
}

void APenguinPawn::Step(float Seconds, const FVector& Wish)
{
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
        StepSlideFlight(Seconds);
        return;
    }

    if (!Wish.IsNearlyZero())
    {
        if (bSliding)
        {
            const float Steering = SlideSteering * (IsPenguinFormActive() ? FormSlideSteeringMultiplier : 1.0f);
            const double Yaw = FMath::FixedTurn(Heading.Rotation().Yaw, Wish.Rotation().Yaw, Steering * Seconds);
            Heading = FRotator(0, Yaw, 0).Vector();
        }
        else
        {
            Heading = Wish.GetSafeNormal2D();
        }
    }
    if (bSliding)
    {
        Facing = Heading;
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
        const float SpeedScale = GetSlideSpeedScale();
        const float AccelerationScale = FMath::Square(SpeedScale);
        const float StopSpeed = SlideStopSpeed * SpeedScale;
        const float GradeGravity = IsPenguinFormActive()
            ? (Travel.Z < 0.0 ? FormDownhillGravity : SlopeGravity * FormUphillBrakingMultiplier)
            : SlopeGravity;
        const float GlideWeight = FMath::SmoothStep(230.0f, 630.0f, PreviousSpeed / SpeedScale);
        const float Friction = FMath::Lerp(FlatDeceleration, FastSlideDeceleration, GlideWeight)
            * Normal.Z * (IsPenguinFormActive() ? FormSlideFrictionMultiplier : 1.0f);
        // Scale acceleration by speed squared to keep the slide's speed profile over the terrain.
        const float Acceleration = (-GradeGravity * Travel.Z - Friction) * AccelerationScale;
        const float SpeedLimit = GetSlideSpeedLimit();
        const float GlideSpeed = FMath::Clamp(PreviousSpeed + Acceleration * Seconds, 0.0f, SpeedLimit);
        const float ForwardDrive = IsPenguinFormActive() ? FMath::Clamp(ForwardInput, 0.0f, 1.0f) : 0.0f;
        // Forward input can sustain a slow glide without overriding the acceleration of an existing fast slide.
        const float DriveScale = IsEditorPenguinFormActive() ? EditorSlideDriveScale : 1.0f;
        const float PoweredSpeed = FMath::Min3(GetGaitSpeed(true) * ForwardDrive * DriveScale,
            PreviousSpeed + 1550.0f * Seconds * DriveScale, SpeedLimit);
        GroundSpeed = FMath::Max(GlideSpeed, PoweredSpeed);
        if (GroundSpeed <= StopSpeed && Acceleration <= 0.0f && ForwardDrive <= 0.0f)
        {
            bStoppedSlide = true;
            MoveSeconds = PreviousSpeed > StopSpeed
                ? Seconds * (PreviousSpeed - StopSpeed) / (PreviousSpeed - GroundSpeed)
                : 0.0f;
            GroundSpeed = PreviousSpeed > StopSpeed ? StopSpeed : 0.0f;
        }
        PeakSlideSpeed = FMath::Max(PeakSlideSpeed, GroundSpeed);
        if (IsPenguinFormActive() && TrySlideLaunch(Seconds, Start, Travel, Rise))
        {
            return;
        }
        AdvanceSlidePose(Seconds);
    }
    else
    {
        bSprinting = bSprintHeld && !IsAiming() && !Wish.IsNearlyZero();
        const float TargetSpeed = Wish.Size() * GetGaitSpeed(bSprinting);
        GroundSpeed = FMath::FInterpConstantTo(GroundSpeed, TargetSpeed, Seconds,
            TargetSpeed > GroundSpeed ? 1550.0f : 2000.0f);
        AdvanceGait(Seconds);
    }

    const FVector Desired = Start + Travel * ((PreviousSpeed + GroundSpeed) * 0.5f * MoveSeconds);
    FVector End = Terrain->KeepInside(Desired);
    if (const auto Water = Terrain->WaterSurfaceAt(End.X, End.Y); Water)
    {
        EnterWater(End, Water.GetValue(), Travel * GroundSpeed);
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
    bSprinting = bSliding = bSlideAirborne = bWaterBreaching = bRecovering = bSprintEntry = false;
    bAimHeld = bFireHeld = false;
    ShotRequestTime = 0.0f;
    SlideVerticalSpeed = CurrentGrade = 0.0f;
    SwimThrust = 0.0f;
    const double EntrySpeed = EntryVelocity.Size();
    SwimVelocity = EntryVelocity.GetClampedToMaxSize(4300.0) * .68;
    const double Plunge = FMath::SmoothStep(480.0, 2450.0, EntrySpeed) * 870.0;
    if (Plunge > 0.0)
    {
        SwimVelocity.Z = FMath::Min(SwimVelocity.Z, -Plunge);
    }
    SwimHeading = EntrySpeed > 60.0 ? SwimVelocity.Rotation() : GetLookYaw();
    const double Bed = Terrain->HeightAt(Position.X, Position.Y);
    const double SurfaceDraft = SurfaceZ - SwimDraft(SwimHeading);
    Position.Z = FMath::Clamp(Position.Z, FMath::Min(Bed + SwimSeabedClearance, SurfaceDraft), SurfaceDraft);
    SwimDepth = SurfaceZ - Position.Z;
    GroundSpeed = SwimVelocity.Size();
    SetActorLocation(Position);
}

void APenguinPawn::StepSwimming(float Seconds)
{
    // The third-person camera normally sits nine degrees below the horizon.
    const FRotator Aim(FMath::Clamp(CameraArm->GetRelativeRotation().Pitch + 9.0, -70.0, 70.0), GetLookYaw().Yaw, 0.0);
    SwimHeading = FMath::RInterpTo(SwimHeading, Aim, Seconds, 11.0);
    const FVector Forward = SwimHeading.Vector();
    SwimThrust = FMath::Clamp(ForwardInput, 0.0f, 1.0f);
    const FVector Along = Forward * FVector::DotProduct(SwimVelocity, Forward);
    const FVector Slip = SwimVelocity - Along;
    // Water drag rises with speed. A stronger cross-flow drag lets entry momentum carry through a turn briefly.
    const double Drag = 1.45 + .0022 * SwimVelocity.Size();
    SwimVelocity = (Along + Slip * FMath::Exp(-6.0 * Seconds)) * FMath::Exp(-Drag * Seconds);
    SwimVelocity += Forward * (SwimThrust * SwimAcceleration * Seconds);
    const double Draft = SwimDraft(SwimHeading);
    const bool bSkimming = SwimThrust > 0.0f && SwimDepth < Draft + 42.0
        && SwimHeading.Pitch >= -4.0 && SwimVelocity.Z > -75.0;
    if (bSkimming)
    {
        SwimVelocity.Z += 240.0 * SwimThrust * Seconds;
    }
    else
    {
        SwimVelocity.Z -= FMath::Lerp(420.0, 90.0, SwimThrust) * Seconds;
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
        if (End.Z >= SurfaceDraft && SwimThrust > 0.0f && SwimVelocity.Z >= SwimBreachRise
            && Speed >= SwimBreachSpeed)
        {
            // Keep the approach velocity and let gravity carry the penguin through the wave and back down.
            bSwimming = false;
            bSlideAirborne = bWaterBreaching = true;
            SlideFlightSpeedScale = 1.0f;
            SlideAirGravity = BaseSlideAirGravity;
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
    if (bWaterBreaching)
    {
        return TEXT("BREACHING");
    }
    if (bSwimming)
    {
        return SwimDepth > SwimDraft(SwimHeading) + 34.0 ? TEXT("UNDERWATER") : TEXT("SWIMMING");
    }
    if (bSlideAirborne && bSliding)
    {
        return TEXT("AIRBORNE SLIDE");
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
