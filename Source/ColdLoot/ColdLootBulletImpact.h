#pragma once

#include "GameFramework/Actor.h"
#include "ColdLootBulletImpact.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()
class AColdLootBulletImpact : public AActor
{
    GENERATED_BODY()

public:
    AColdLootBulletImpact();
    virtual void Tick(float DeltaSeconds) override;
    static void Play(UWorld& World, const FHitResult& Hit, UMaterialInterface* Material);

private:
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Sparks;

    float Age = 0.0f;
};
