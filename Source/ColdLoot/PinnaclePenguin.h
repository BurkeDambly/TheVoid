#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PinnaclePenguin.generated.h"

class AIcebergIsland;
class UAnimSequence;

UCLASS()
class UPinnaclePenguinMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()

public:
    int32 GetFootBoneIndex(int32 Side) const;
    void PlantFeet();
    void BlendOutFootPlant(float DeltaSeconds);
    virtual void FinalizeBoneTransform() override;

private:
    FTransform PlantedFeet[2];
    float FootPlantWeight = 0.0f;
};

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
    TObjectPtr<UPinnaclePenguinMeshComponent> PenguinMesh;

    FVector2D Home;
    FVector2D Destination;
    float PauseRemaining = 0.0f;
    bool bWalking = false;
    bool bTurning = false;
};
