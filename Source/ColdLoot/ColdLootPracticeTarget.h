#pragma once

#include "GameFramework/Actor.h"
#include "ColdLootPracticeTarget.generated.h"

class UStaticMeshComponent;

UCLASS()
class AColdLootPracticeTarget : public AActor
{
    GENERATED_BODY()

public:
    AColdLootPracticeTarget();
    virtual void BeginPlay() override;
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator,
        AActor* DamageCauser) override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Barrel;
};
