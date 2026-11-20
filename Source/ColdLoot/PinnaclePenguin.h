#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PinnaclePenguin.generated.h"

class AIcebergIsland;
class UAnimSequence;
class USkeletalMeshComponent;

UCLASS()
class APinnaclePenguin : public AActor
{
    GENERATED_BODY()

public:
    APinnaclePenguin();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditInstanceOnly, Category = "Wander")
    TObjectPtr<AIcebergIsland> Island;

    UPROPERTY(EditAnywhere, Category = "Wander")
    TObjectPtr<UAnimSequence> ShuffleAnimation;

    UPROPERTY(EditAnywhere, Category = "Wander", meta = (ClampMin = "40.0"))
    float WanderRadius = 300.0f;

    UPROPERTY(EditAnywhere, Category = "Wander", meta = (ClampMin = "1.0"))
    float WalkSpeed = 100.0f;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> PenguinMesh;

    FVector2D Home;
    FVector2D Destination;
    float PauseRemaining = 0.0f;
    bool bWalking = false;
};
