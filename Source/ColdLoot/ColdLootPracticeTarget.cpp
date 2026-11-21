#include "ColdLootPracticeTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AColdLootPracticeTarget::AColdLootPracticeTarget()
{
    bReplicates = true;
    SetReplicateMovement(true);
    Barrel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Barrel"));
    SetRootComponent(Barrel);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Barrel->SetStaticMesh(Cylinder.Object);
    Barrel->SetCollisionProfileName(TEXT("PhysicsActor"));
    Barrel->SetLinearDamping(0.4f);
    Barrel->SetAngularDamping(0.6f);
}

void AColdLootPracticeTarget::BeginPlay()
{
    Super::BeginPlay();
    Barrel->SetSimulatePhysics(true);
    Barrel->SetMassOverrideInKg(NAME_None, 24.0f);
}

float AColdLootPracticeTarget::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator,
    AActor* DamageCauser)
{
    const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
    if (Applied > 0.0f && DamageEvent.IsOfType(FPointDamageEvent::ClassID))
    {
        const auto& Impact = static_cast<const FPointDamageEvent&>(DamageEvent);
        const FVector Impulse = (Impact.ShotDirection + FVector(0, 0, 0.12)) * Applied * 170.0;
        Barrel->AddImpulseAtLocation(Impulse, Impact.HitInfo.ImpactPoint);
    }
    return Applied;
}
