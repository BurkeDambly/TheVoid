#pragma once

#include "ColdzoneMapTerrain.h"
#include "IcebergNetworkTerrain.generated.h"

class AIcebergIsland;
class UAnimSequence;
class USkeletalMesh;
class UProceduralMeshComponent;

UCLASS()
class AIcebergNetworkTerrain : public AColdzoneMapTerrain
{
    GENERATED_BODY()

public:
    AIcebergNetworkTerrain();
    virtual void BeginPlay() override;
    virtual void PostInitializeComponents() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual double HeightAt(double X, double Y) const override;
    virtual FVector NormalAt(double X, double Y) const override;
    virtual FVector KeepInside(const FVector& Position) const override;
    virtual TOptional<double> WaterSurfaceAt(double X, double Y) const override;
    virtual TConstArrayView<FColdzoneZone> Zones() const override { return Locations; }
    virtual FVector2D MapMinimum() const override { return MapCenter - FVector2D(MapHalfSize); }
    virtual FVector2D MapMaximum() const override { return MapCenter + FVector2D(MapHalfSize); }
    virtual FColor MapColorAt(const FVector2D& Point) const override;

    UFUNCTION(BlueprintCallable, Category = "Coldzone Island Network")
    void RefreshIslands();

    UFUNCTION(BlueprintCallable, Category = "Coldzone Island Network")
    void RebuildOcean();

    UFUNCTION(BlueprintPure, Category = "Coldzone Island Network")
    TArray<FVector4> OceanWaves() const;

    double WaveOffset(double X, double Y, double Time) const;

    UPROPERTY(EditAnywhere, Category = "Coldzone Island Network")
    TObjectPtr<UMaterialInterface> OceanMaterial;

    UPROPERTY(EditAnywhere, Category = "Coldzone Island Network")
    TObjectPtr<UMaterialInterface> UnderwaterFog;

    UPROPERTY(EditAnywhere, Category = "Arctic Fish")
    TObjectPtr<USkeletalMesh> ArcticFishMesh;

    UPROPERTY(EditAnywhere, Category = "Arctic Fish")
    TObjectPtr<UAnimSequence> ArcticFishSwim;

    UPROPERTY(EditAnywhere, Category = "Arctic Fish")
    TObjectPtr<UMaterialInterface> ArcticFishRipple;

private:
    void RebuildPracticeRing();
    void SpawnArcticFish();
    void SpawnFishSchool(FVector2D Center, FVector2D Outward, int32 School);
    bool FindFishRoute(FVector2D RampLip, FVector2D Outward, FVector2D& Center) const;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AIcebergIsland>> Islands;

    UPROPERTY(VisibleAnywhere, Category = "Coldzone Island Network")
    TObjectPtr<UProceduralMeshComponent> PracticeRing;
    FTimerHandle FishSpawnTimer;
    TArray<FColdzoneZone> Locations;
    FVector2D MapCenter = FVector2D::ZeroVector;
    double MapHalfSize = 125.0;
};
