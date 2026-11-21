#include "PinnaclePenguin.h"

#include "IcebergIsland.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
constexpr float TurnSpeed = 150.0f;
constexpr float ShuffleDistance = 91.0f;
constexpr float FootReleaseDuration = 0.12f;
}

int32 UPinnaclePenguinMeshComponent::GetFootBoneIndex(int32 Side) const
{
    static const FName FootNames[] = {TEXT("Foot.L"), TEXT("Foot.R")};
    static const FName ImportedFootNames[] = {TEXT("Foot_L"), TEXT("Foot_R")};
    const int32 Index = GetBoneIndex(FootNames[Side]);
    return Index == INDEX_NONE ? GetBoneIndex(ImportedFootNames[Side]) : Index;
}

void UPinnaclePenguinMeshComponent::PlantFeet()
{
    for (int32 Side = 0; Side < UE_ARRAY_COUNT(PlantedFeet); ++Side)
    {
        const int32 Index = GetFootBoneIndex(Side);
        if (Index != INDEX_NONE)
        {
            PlantedFeet[Side] = GetBoneTransform(Index);
        }
    }
    FootPlantWeight = 1.0f;
}

void UPinnaclePenguinMeshComponent::BlendOutFootPlant(float DeltaSeconds)
{
    FootPlantWeight = FMath::Max(0.0f, FootPlantWeight - DeltaSeconds / FootReleaseDuration);
}

void UPinnaclePenguinMeshComponent::FinalizeBoneTransform()
{
    if (FootPlantWeight > 0.0f)
    {
        auto& BoneTransforms = GetEditableComponentSpaceTransforms();
        for (int32 Side = 0; Side < UE_ARRAY_COUNT(PlantedFeet); ++Side)
        {
            const int32 Index = GetFootBoneIndex(Side);
            if (BoneTransforms.IsValidIndex(Index))
            {
                const FTransform Planted = PlantedFeet[Side].GetRelativeTransform(GetComponentTransform());
                BoneTransforms[Index].BlendWith(Planted, FootPlantWeight);
            }
        }
    }
    Super::FinalizeBoneTransform();
}

APinnaclePenguin::APinnaclePenguin()
{
    PrimaryActorTick.bCanEverTick = true;
    PenguinMesh = CreateDefaultSubobject<UPinnaclePenguinMeshComponent>(TEXT("PenguinMesh"));
    PenguinMesh->SetCollisionProfileName(TEXT("NoCollision"));
    PenguinMesh->SetGenerateOverlapEvents(false);
    PenguinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    PenguinMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    USceneComponent* Ground = CreateDefaultSubobject<USceneComponent>(TEXT("Ground"));
    SetRootComponent(Ground);
    PenguinMesh->SetupAttachment(Ground);
    PenguinMesh->SetRelativeRotation(FRotator(0, -90, 0));
}

void APinnaclePenguin::BeginPlay()
{
    Super::BeginPlay();
    if (!IsValid(Island) || !Island->bBaseIsland || !PenguinMesh->GetSkeletalMeshAsset() || !ShuffleAnimation)
    {
        SetActorTickEnabled(false);
        return;
    }

    Home = FVector2D(GetActorLocation());
    Destination = Home;
    SetActorLocation(FVector(Home, Island->HeightAt(Home.X, Home.Y)));
    PauseRemaining = FMath::FRandRange(0.15f, 0.65f);
    PenguinMesh->AddTickPrerequisiteActor(this);
    PenguinMesh->PlayAnimation(ShuffleAnimation, true);
    if (UAnimSingleNodeInstance* Gait = PenguinMesh->GetSingleNodeInstance())
    {
        Gait->SetPlaying(false);
        Gait->SetPlayRate(WalkSpeed * ShuffleAnimation->GetPlayLength() / ShuffleDistance);
    }
}

void APinnaclePenguin::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsValid(Island))
    {
        SetActorTickEnabled(false);
        return;
    }

    const FVector2D Position(GetActorLocation());
    if (!bWalking && FVector2D::DistSquared(Position, Destination) < 1.0)
    {
        PauseRemaining -= DeltaSeconds;
        if (PauseRemaining > 0.0f)
        {
            return;
        }

        const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
        const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
        Destination = Home + Direction * FMath::FRandRange(WanderRadius * 0.6f, WanderRadius);
    }

    const FVector2D Offset = Destination - Position;
    const float DesiredYaw = FMath::RadiansToDegrees(FMath::Atan2(Offset.Y, Offset.X));
    const float CurrentYaw = GetActorRotation().Yaw;
    if (!bWalking && !FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw), 0.01f))
    {
        if (!bTurning)
        {
            PenguinMesh->PlantFeet();
            bTurning = true;
        }
        const float Yaw = FMath::FixedTurn(CurrentYaw, DesiredYaw, TurnSpeed * DeltaSeconds);
        SetActorRotation(FRotator(0, Yaw, 0));
        return;
    }

    UAnimSingleNodeInstance* Gait = PenguinMesh->GetSingleNodeInstance();
    if (!bWalking)
    {
        bWalking = true;
        bTurning = false;
        if (Gait)
        {
            Gait->SetPlaying(true);
        }
    }

    const double Step = WalkSpeed * DeltaSeconds;
    const bool bArrived = Offset.SizeSquared() <= FMath::Square(Step);
    const FVector2D Next = bArrived ? Destination : Position + Offset.GetSafeNormal() * Step;
    PenguinMesh->BlendOutFootPlant(DeltaSeconds);
    SetActorLocation(FVector(Next, Island->HeightAt(Next.X, Next.Y)));
    if (bArrived)
    {
        bWalking = false;
        PauseRemaining = FMath::FRandRange(0.55f, 1.45f);
        if (Gait)
        {
            Gait->SetPlaying(false);
        }
    }
}
