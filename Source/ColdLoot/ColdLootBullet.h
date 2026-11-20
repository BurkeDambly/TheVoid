#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ColdLootBullet.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class AColdLootBullet : public AActor
{
    GENERATED_BODY()

public:
    static constexpr ECollisionChannel CollisionChannel = ECC_GameTraceChannel1; // ColdLootBullet
    static constexpr float CollisionRadius = 1.5f;

    AColdLootBullet();
    virtual void Tick(float DeltaSeconds) override;
    void Launch(const FVector& Direction, float Speed, float Damage, UMaterialInterface* TracerMaterial);

private:
    UFUNCTION()
    void HandleRicochet(const FHitResult& Hit, const FVector& IncomingVelocity);

    UFUNCTION()
    void HandleImpact(const FHitResult& Hit);

    void ResolveHit(const FHitResult& Hit, const FVector& Direction);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USphereComponent> Collision;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Tracer;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProjectileMovementComponent> Movement;

    float HitDamage = 0.0f;
    float RicochetAge = 0.0f;
    FVector ShotDirection = FVector::ForwardVector;
};
