#include "ArcticFish.h"

#include "Animation/AnimSequence.h"
#include "ColdLootBullet.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "IcebergNetworkTerrain.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr double OrbitSpeed = 0.43;
constexpr double ApproachTime = 0.55;
constexpr double LeapTime = 1.50;
constexpr double LeapInterval = 3.45;
constexpr double SurfaceDraft = 28.0;
constexpr double RespawnDelay = 9.0;
constexpr float RippleDuration = 0.65f;
const FName RippleOpacity(TEXT("RippleOpacity"));

double FishDepth(double Time, double Depth, double Offset, double LeapHeight)
{
    const double Phase = FMath::Fmod(Time - Offset + LeapInterval, LeapInterval);
    if (Phase < ApproachTime)
    {
        return FMath::Lerp(-Depth, -SurfaceDraft, FMath::SmoothStep(0.0, ApproachTime, Phase));
    }
    if (Phase < ApproachTime + LeapTime)
    {
        const double U = (Phase - ApproachTime) / LeapTime;
        return -SurfaceDraft + (4.0 * LeapHeight * U * (1.0 - U));
    }
    const double Recovery = Phase - ApproachTime - LeapTime;
    return FMath::Lerp(-SurfaceDraft, -Depth, FMath::SmoothStep(0.0, ApproachTime, Recovery));
}
}

AArcticFish::AArcticFish()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("FishHitbox"));
    Collision->InitBoxExtent(FVector(32.0, 15.0, 15.0));
    Collision->SetCollisionObjectType(ECC_WorldDynamic);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(AColdLootBullet::CollisionChannel, ECR_Block);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetCanEverAffectNavigation(false);
    SetRootComponent(Collision);

    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ArcticFish"));
    Mesh->SetupAttachment(Collision);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));

    RippleMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BreachRipple"));
    RippleMesh->SetupAttachment(Collision);
    RippleMesh->SetAbsolute(true, true, true);
    RippleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RippleMesh->SetCanEverAffectNavigation(false);
    RippleMesh->SetCastShadow(false);
    RippleMesh->SetVisibility(false);
}

void AArcticFish::Initialize(AIcebergNetworkTerrain& InTerrain, USkeletalMesh* FishMesh, UAnimSequence* Swim,
    UMaterialInterface* Ripple, FVector2D InCenter, FVector2D InOutward, int32 School, int32 Member)
{
    Terrain = &InTerrain;
    Center = InCenter;
    Outward = InOutward;
    SwimPhase = UE_TWO_PI * Member / ChannelSchoolSize + School * 0.68;
    const int32 Stroke = Member % 3;
    SwimDepth = 57.0 + Stroke * 10.0;
    LeapOffset = -LeapInterval * (Member + 0.5 * School) / ChannelSchoolSize;
    LeapHeight = 211.0 + Stroke * 14.0;
    Mesh->SetSkeletalMeshAsset(FishMesh);
    Mesh->PlayAnimation(Swim, true);
    Mesh->SetPosition(FMath::Fmod(School * 0.31f + Member * 0.68f, Swim->GetPlayLength()), false);
    Mesh->SetPlayRate(0.86f + Stroke * 0.12f);

    if (Ripple)
    {
        constexpr int32 Segments = 32;
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FLinearColor> Colors;
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const double Angle = Index * UE_TWO_PI / Segments;
            const FVector Ray(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
            Vertices.Append({Ray * 0.87, Ray * 0.94, Ray});
            Colors.Append({FLinearColor::Transparent, FLinearColor::White, FLinearColor::Transparent});
            const int32 A = Index * 3;
            const int32 B = ((Index + 1) % Segments) * 3;
            Triangles.Append({A, B, A + 1, A + 1, B, B + 1,
                A + 1, B + 1, A + 2, A + 2, B + 1, B + 2});
        }
        RippleMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, {}, Colors, {}, false, false);
        RippleMaterial = RippleMesh->CreateDynamicMaterialInstance(0, Ripple);
    }
    SetActorTickEnabled(true);
    Tick(0.0f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void AArcticFish::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const AIcebergNetworkTerrain* Network = Terrain.Get();
    if (!Network)
    {
        Destroy();
        return;
    }
    Time += DeltaSeconds;
    UpdateRipple(DeltaSeconds);

    if (RespawnAt > 0.0)
    {
        if (Time < RespawnAt)
        {
            return;
        }
        RespawnAt = 0.0;
        bHasPosition = false;
        Mesh->SetVisibility(true);
        Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }

    const double Angle = SwimPhase + Time * OrbitSpeed;
    const FVector2D Along(-Outward.Y, Outward.X);
    const FVector2D Position = Center + Outward * (RadialRadius * FMath::Cos(Angle))
        + Along * (ShoreRadius * FMath::Sin(Angle));
    const FVector2D Velocity = OrbitSpeed * (-Outward * (RadialRadius * FMath::Sin(Angle))
        + Along * (ShoreRadius * FMath::Cos(Angle)));
    const double WaveTime = GetWorld()->GetTimeSeconds();
    const double Surface = Network->GetActorLocation().Z + Network->WaveOffset(Position.X, Position.Y, WaveTime);
    const double Depth = FishDepth(Time, SwimDepth, LeapOffset, LeapHeight);
    const double VerticalSpeed = (FishDepth(Time + 0.02, SwimDepth, LeapOffset, LeapHeight)
        - FishDepth(Time - 0.02, SwimDepth, LeapOffset, LeapHeight)) / 0.04;
    const FVector Facing(Velocity, FMath::Clamp(VerticalSpeed, -1.3 * Velocity.Size(), 1.3 * Velocity.Size()));
    SetActorLocationAndRotation(FVector(Position, Surface + Depth), Facing.Rotation(), false, nullptr,
        ETeleportType::TeleportPhysics);

    if (bHasPosition && (LastDepth >= 0.0) != (Depth >= 0.0))
    {
        StartRipple(FVector(Position, Surface));
    }
    LastDepth = Depth;
    bHasPosition = true;
}

float AArcticFish::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator,
    AActor* DamageCauser)
{
    if (RespawnAt > 0.0 || !DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        return 0.0f;
    }
    const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
    if (Applied > 0.0f)
    {
        RespawnAt = Time + RespawnDelay;
        Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetVisibility(false);
    }
    return Applied;
}

void AArcticFish::StartRipple(const FVector& SurfacePosition)
{
    if (!RippleMaterial)
    {
        return;
    }
    RippleAge = 0.0f;
    RippleMesh->SetWorldLocation(SurfacePosition + FVector(0.0, 0.0, 4.0));
    RippleMesh->SetVisibility(true);
    UpdateRipple(0.0f);
}

void AArcticFish::UpdateRipple(float DeltaSeconds)
{
    if (!RippleMesh->IsVisible())
    {
        return;
    }
    RippleAge += DeltaSeconds;
    if (RippleAge >= RippleDuration)
    {
        RippleMesh->SetVisibility(false);
        return;
    }
    const float Progress = RippleAge / RippleDuration;
    const float Remaining = FMath::Square(1.0f - Progress);
    const double Radius = FMath::Lerp(18.0, 93.0, 1.0f - Remaining);
    FVector Location = RippleMesh->GetComponentLocation();
    Location.Z = Terrain->GetActorLocation().Z
        + Terrain->WaveOffset(Location.X, Location.Y, GetWorld()->GetTimeSeconds()) + 7.0;
    RippleMesh->SetWorldLocation(Location);
    RippleMesh->SetWorldScale3D(FVector(Radius, Radius, 1.0));
    RippleMaterial->SetScalarParameterValue(RippleOpacity, 0.80f * Remaining);
}
