#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArcticFish.generated.h"

class AIcebergNetworkTerrain;
class UAnimSequence;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS(NotPlaceable)
class AArcticFish : public AActor
{
    GENERATED_BODY()

public:
    AArcticFish();
    void Initialize(AIcebergNetworkTerrain& InTerrain, USkeletalMesh* FishMesh, UAnimSequence* Swim,
        UMaterialInterface* Ripple, FVector2D InCenter, FVector2D InOutward, int32 School, int32 Member);
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator,
        AActor* DamageCauser) override;

    static constexpr double RadialRadius = 285.0;
    static constexpr double ShoreRadius = 645.0;
    static constexpr int32 ChannelSchoolSize = 6;

private:
    void StartRipple(const FVector& SurfacePosition);
    void UpdateRipple(float DeltaSeconds);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Collision;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USkeletalMeshComponent> Mesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> RippleMesh;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> RippleMaterial;

    TWeakObjectPtr<AIcebergNetworkTerrain> Terrain;
    FVector2D Center;
    FVector2D Outward;
    double Time = 0.0;
    double SwimPhase = 0.0;
    double SwimDepth = 0.0;
    double LeapOffset = 0.0;
    double LeapHeight = 0.0;
    double RespawnAt = 0.0;
    double LastDepth = 0.0;
    float RippleAge = 0.0f;
    bool bHasPosition = false;
};
