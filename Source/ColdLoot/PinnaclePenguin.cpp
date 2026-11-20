#include "PinnaclePenguin.h"

#include "IcebergIsland.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
constexpr float TurnSpeed = 150.0f;
constexpr float ShuffleDistance = 91.0f;
}

APinnaclePenguin::APinnaclePenguin()
{
    PrimaryActorTick.bCanEverTick = true;
    PenguinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PenguinMesh"));
    PenguinMesh->SetCollisionProfileName(TEXT("NoCollision"));
    PenguinMesh->SetGenerateOverlapEvents(false);
    PenguinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
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
    const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, DesiredYaw, TurnSpeed * DeltaSeconds);
    SetActorRotation(FRotator(0, Yaw, 0));
    if (FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, DesiredYaw)) > 1.0)
    {
        return;
    }

    UAnimSingleNodeInstance* Gait = PenguinMesh->GetSingleNodeInstance();
    if (!bWalking)
    {
        bWalking = true;
        if (Gait)
        {
            Gait->SetPlaying(true);
        }
    }

    const double Step = WalkSpeed * DeltaSeconds;
    const bool bArrived = Offset.SizeSquared() <= FMath::Square(Step);
    const FVector2D Next = bArrived ? Destination : Position + Offset.GetSafeNormal() * Step;
    SetActorLocation(FVector(Next, Island->HeightAt(Next.X, Next.Y)));
    if (bArrived)
    {
        bWalking = false;
        PauseRemaining = FMath::FRandRange(0.55f, 1.45f);
        if (Gait)
        {
            Gait->SetPlaying(false);
            Gait->SetPosition(0.0f, false);
        }
    }
}
