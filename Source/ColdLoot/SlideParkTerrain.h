#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SlideParkTerrain.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS()
class ASlideParkTerrain : public AActor
{
    GENERATED_BODY()

public:
    ASlideParkTerrain();
    virtual void OnConstruction(const FTransform& Transform) override;

    virtual double HeightAt(double X, double Y) const;
    virtual FVector NormalAt(double X, double Y) const;
    virtual FVector KeepInside(const FVector& Position) const;
    virtual TOptional<double> WaterSurfaceAt(double X, double Y) const { return {}; }

    UPROPERTY(EditAnywhere, Category="Terrain", meta=(ClampMin="0.25", ClampMax="1.5"))
    float HillHeightScale = 1.0f;

    UPROPERTY(EditAnywhere, Category="Terrain")
    int32 HillSeed = 73;

    UPROPERTY(EditAnywhere, Category="Terrain")
    TObjectPtr<UMaterialInterface> SnowMaterial;

protected:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> Surface;

private:

    double LocalHeight(double X, double Y) const;
};
