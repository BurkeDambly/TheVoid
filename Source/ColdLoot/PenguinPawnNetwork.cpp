#include "PenguinPawn.h"

#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr uint8 PoseReady = 1 << 0;
constexpr uint8 Sliding = 1 << 1;
constexpr uint8 Recovering = 1 << 2;
constexpr uint8 Swimming = 1 << 3;
constexpr uint8 Breaching = 1 << 4;
constexpr uint8 Sprinting = 1 << 5;
constexpr uint8 Airborne = 1 << 6;
constexpr uint8 Diving = 1 << 7;
}

void APenguinPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APenguinPawn, NetworkPose);
    DOREPLIFETIME(APenguinPawn, CombatState);
}

void APenguinPawn::CopyPlaytestAppearance()
{
    for (TActorIterator<APenguinPawn> It(GetWorld()); It; ++It)
    {
        if (!It->IsNetStartupActor())
        {
            continue;
        }
        const APenguinPawn& Placed = **It;
        PenguinMesh->SetSkeletalMeshAsset(Placed.PenguinMesh->GetSkeletalMeshAsset());
        PenguinMesh->SetRelativeTransform(Placed.PenguinMesh->GetRelativeTransform());
        for (int32 Index = 0; Index < Placed.PenguinMesh->GetNumMaterials(); ++Index)
        {
            PenguinMesh->SetMaterial(Index, Placed.PenguinMesh->GetMaterial(Index));
        }
        WalkAnimation = Placed.WalkAnimation;
        SprintAnimation = Placed.SprintAnimation;
        WalkWaddleAnimation = Placed.WalkWaddleAnimation;
        SprintWaddleAnimation = Placed.SprintWaddleAnimation;
        SlideAnimation = Placed.SlideAnimation;
        SwimAnimation = Placed.SwimAnimation;
        WeaponDefinition = Placed.WeaponDefinition;
        MeshYawOffset = Placed.MeshYawOffset;
        UpperBodyJiggle = Placed.UpperBodyJiggle;
        WalkDistancePerCycle = Placed.WalkDistancePerCycle;
        SprintDistancePerCycle = Placed.SprintDistancePerCycle;
        StationarySlidePush = Placed.StationarySlidePush;
        FlatDeceleration = Placed.FlatDeceleration;
        FastSlideDeceleration = Placed.FastSlideDeceleration;
        SlopeGravity = Placed.SlopeGravity;
        SlideSpeedLimit = Placed.SlideSpeedLimit;
        SlideSteering = Placed.SlideSteering;
        break;
    }
}

void APenguinPawn::PublishNetworkPose()
{
    NetworkPose.Position = GetActorLocation();
    NetworkPose.BodyRotation = BodyPivot->GetComponentRotation();
    NetworkPose.View = CameraArm->GetRelativeRotation();
    NetworkPose.Speed = GroundSpeed;
    NetworkPose.SprintBlend = SprintBlend;
    NetworkPose.SlideTime = SlidePoseTime;
    NetworkPose.SwimThrust = SwimThrust;
    NetworkPose.SwimDepth = SwimDepth;
    NetworkPose.FireBlend = SprintFireBlend;
    NetworkPose.bHoldingWaterEntryPose = IsHoldingWaterEntryPose();
    NetworkPose.Flags = PoseReady | (bSliding ? Sliding : 0) | (bRecovering ? Recovering : 0)
        | (bSwimming ? Swimming : 0) | (bWaterBreaching ? Breaching : 0) | (bSprinting ? Sprinting : 0)
        | (bSlideAirborne ? Airborne : 0) | (bDiving ? Diving : 0);
}

void APenguinPawn::TickNetworkClient(float DeltaSeconds)
{
    const bool bLocalPlayer = IsLocallyControlled();
    InputSendTime -= DeltaSeconds;
    if (bLocalPlayer && InputSendTime <= 0.0f)
    {
        ServerUpdateInput(ForwardInput, RightInput, CameraArm->GetRelativeRotation(), bSprintHeld, bSlideBrakeHeld);
        InputSendTime = 1.0f / 30.0f;
    }
    if (!(NetworkPose.Flags & PoseReady))
    {
        return;
    }

    const bool bTeleport = !bReceivedNetworkPose || FVector::DistSquared(GetActorLocation(), NetworkPose.Position)
        > FMath::Square(1000.0);
    SetActorLocation(bTeleport ? FVector(NetworkPose.Position)
        : FMath::VInterpTo(GetActorLocation(), NetworkPose.Position, DeltaSeconds, 25.0f));
    BodyPivot->SetWorldRotation(bTeleport ? NetworkPose.BodyRotation
        : FMath::RInterpTo(BodyPivot->GetComponentRotation(), NetworkPose.BodyRotation, DeltaSeconds, 25.0f));
    bReceivedNetworkPose = true;
    const bool bWasInWater = bSwimming || bWaterBreaching;
    bSliding = (NetworkPose.Flags & Sliding) != 0;
    bRecovering = (NetworkPose.Flags & Recovering) != 0;
    bSwimming = (NetworkPose.Flags & Swimming) != 0;
    bWaterBreaching = (NetworkPose.Flags & Breaching) != 0;
    bSprinting = (NetworkPose.Flags & Sprinting) != 0;
    bSlideAirborne = (NetworkPose.Flags & Airborne) != 0;
    bDiving = (NetworkPose.Flags & Diving) != 0;
    GroundSpeed = NetworkPose.Speed;
    SprintBlend = NetworkPose.SprintBlend;
    SlidePoseTime = NetworkPose.SlideTime;
    SwimThrust = NetworkPose.SwimThrust;
    SwimDepth = NetworkPose.SwimDepth;
    SwimHeading = BodyPivot->GetComponentRotation();
    SprintFireBlend = NetworkPose.FireBlend;
    AdvanceGait(DeltaSeconds);
    UpdateWeaponVisibility();
    if (bLocalPlayer)
    {
        if (bWasInWater && !bSwimming && !bWaterBreaching)
        {
            LookPitch(0.0f);
        }
        UpdateAimCamera(DeltaSeconds);
        UpdateUnderwaterView(DeltaSeconds);
    }
    else
    {
        CameraArm->SetRelativeRotation(NetworkPose.View);
    }
}

void APenguinPawn::ServerUpdateInput_Implementation(float Forward, float Right, FRotator View, bool bSprint, bool bBrake)
{
    if (!FMath::IsFinite(Forward) || !FMath::IsFinite(Right) || View.ContainsNaN())
    {
        return;
    }
    ForwardInput = FMath::Clamp(Forward, -1.0f, 1.0f);
    RightInput = FMath::Clamp(Right, -1.0f, 1.0f);
    // A serialized negative angle can arrive wrapped into the 0–360 degree range.
    const double Pitch = FRotator::NormalizeAxis(View.Pitch);
    const bool bWaterCamera = bSwimming || bWaterBreaching;
    CameraArm->SetRelativeRotation(FRotator(FMath::Clamp(Pitch, bWaterCamera ? -76.0 : -55.0,
        bWaterCamera ? 61.0 : 8.0),
        FRotator::NormalizeAxis(View.Yaw), 0.0));
    bSprintHeld = bSprint;
    bSlideBrakeHeld = bBrake;
    LastRemoteInputTime = GetWorld()->GetTimeSeconds();
}

void APenguinPawn::ServerAction_Implementation(EPenguinAction Action)
{
    LastRemoteInputTime = GetWorld()->GetTimeSeconds();
    switch (Action)
    {
        case EPenguinAction::Slide: ToggleSlide(); break;
        case EPenguinAction::Jump: Jump(); break;
        case EPenguinAction::AimPressed: StartAim(); break;
        case EPenguinAction::AimReleased: StopAim(); break;
        case EPenguinAction::FirePressed: StartFire(); break;
        case EPenguinAction::FireReleased: StopFire(); break;
        case EPenguinAction::ReturnToBase: ResetFlat(); break;
    }
}
