#include "ColdLootBullet.h"
#include "ColdLootBulletImpact.h"
#include "ColdLootTuning.h"
#include "PenguinPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float RicochetLifetime = 0.16f;
constexpr float TracerLength = 0.92f;
constexpr float TracerWidth = 0.062f;
}

AColdLootBullet::AColdLootBullet()
{
    InitialLifeSpan = 1.8f;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BulletCollision"));
    Collision->InitSphereRadius(CollisionRadius);
    Collision->SetCollisionObjectType(CollisionChannel);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionResponseToChannel(CollisionChannel, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    SetRootComponent(Collision);

    Tracer = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tracer"));
    Tracer->SetupAttachment(Collision);
    Tracer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Tracer->SetCastShadow(false);
    Tracer->SetRelativeLocation(FVector(-50.0f * TracerLength, 0, 0));
    Tracer->SetRelativeScale3D(FVector(TracerLength, TracerWidth, TracerWidth));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Tracer->SetStaticMesh(Shape.Object);

    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Flight"));
    Movement->SetUpdatedComponent(Collision);
    Movement->ProjectileGravityScale = 0.0f;
    Movement->bRotationFollowsVelocity = true;
    Movement->bShouldBounce = true;
    Movement->Bounciness = 0.40f;
    Movement->Friction = 0.45f;
    Movement->OnProjectileBounce.AddDynamic(this, &AColdLootBullet::HandleRicochet);
    Movement->OnProjectileStop.AddDynamic(this, &AColdLootBullet::HandleImpact);
}

void AColdLootBullet::Launch(const FVector& Direction, float Speed, float Damage, UMaterialInterface* TracerMaterial)
{
    Collision->IgnoreActorWhenMoving(GetOwner(), true);
    HitDamage = Damage;
    ShotDirection = Direction;
    Movement->Velocity = Direction * Speed;
    Movement->Bounciness = 0.4f * FColdLootTuning::ForWorld(GetWorld()).Get(EColdLootTuning::Ricochet);
    Movement->bShouldBounce = Movement->Bounciness > 0.0f;
    Tracer->SetMaterial(0, TracerMaterial);
}

void AColdLootBullet::HandleRicochet(const FHitResult& Hit, const FVector& IncomingVelocity)
{
    if (Cast<APenguinPawn>(Hit.GetActor()))
    {
        HandleImpact(Hit);
        return;
    }
    ResolveHit(Hit, IncomingVelocity.GetSafeNormal());
    Movement->bShouldBounce = false;
    ShotDirection = Movement->Velocity.GetSafeNormal();
    HitDamage *= 0.35f;
    SetLifeSpan(RicochetLifetime);
    SetActorTickEnabled(true);
}

void AColdLootBullet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    RicochetAge += DeltaSeconds;
    const float Fade = FMath::Clamp(1.0f - RicochetAge / RicochetLifetime, 0.0f, 1.0f);
    Tracer->SetRelativeScale3D(FVector(TracerLength * Fade, TracerWidth * Fade, TracerWidth * Fade));
    Tracer->SetRelativeLocation(FVector(-50.0f * TracerLength * Fade, 0, 0));
}

void AColdLootBullet::HandleImpact(const FHitResult& Hit)
{
    if (IsActorBeingDestroyed())
    {
        return;
    }
    ResolveHit(Hit, ShotDirection);
    Destroy();
}

void AColdLootBullet::ResolveHit(const FHitResult& Hit, const FVector& Direction)
{
    AColdLootBulletImpact::Play(*GetWorld(), Hit, Tracer->GetMaterial(0));
    if (AActor* Target = Hit.GetActor(); GetNetMode() != NM_Client && IsValid(Target) && Target != GetOwner())
    {
        const float DamageScale = Cast<APenguinPawn>(Target) ? 1.0f
            : FColdLootTuning::ForWorld(GetWorld()).Get(EColdLootTuning::TargetDamage);
        UGameplayStatics::ApplyPointDamage(Target, HitDamage * DamageScale, Direction, Hit,
            GetInstigatorController(), this, nullptr);
    }
}
