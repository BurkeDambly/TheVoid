#include "ColdLootBulletImpact.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float SparkLifetime = 0.13f;
const FVector SparkScale(0.15, 0.028, 0.028);
}

AColdLootBulletImpact::AColdLootBulletImpact()
{
    InitialLifeSpan = SparkLifetime;
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Surface")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const FName Name(*FString::Printf(TEXT("Spark%d"), Index));
        UStaticMeshComponent* Spark = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Spark->SetupAttachment(RootComponent);
        Spark->SetStaticMesh(Sphere.Object);
        Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Spark->SetCastShadow(false);
        Spark->SetRelativeScale3D(SparkScale);
        Spark->SetRelativeRotation(FRotator(24.0 + 10.0 * Index, 120.0 * Index, 0.0));
        Sparks.Add(Spark);
    }
}

void AColdLootBulletImpact::Play(UWorld& World, const FHitResult& Hit, UMaterialInterface* Material)
{
    if (!Material || !Hit.bBlockingHit)
    {
        return;
    }

    const FQuat Surface = FRotationMatrix::MakeFromZ(Hit.ImpactNormal).ToQuat();
    const FQuat Spin(FVector::UpVector, FMath::FRandRange(0.0, UE_TWO_PI));
    if (AColdLootBulletImpact* Effect = World.SpawnActor<AColdLootBulletImpact>(
        Hit.ImpactPoint + Hit.ImpactNormal * 3.0, (Surface * Spin).Rotator()))
    {
        for (UStaticMeshComponent* Spark : Effect->Sparks)
        {
            Spark->SetMaterial(0, Material);
        }
    }
}

void AColdLootBulletImpact::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float Progress = FMath::Clamp(Age / SparkLifetime, 0.0f, 1.0f);
    for (UStaticMeshComponent* Spark : Sparks)
    {
        Spark->SetRelativeLocation(Spark->GetRelativeRotation().Vector() * (4.0f + 18.0f * Progress));
        Spark->SetRelativeScale3D(SparkScale * (1.0f - Progress));
    }
}
