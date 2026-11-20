#pragma once

#include "ColdzoneMapTerrain.h"
#include "FrozenBasinTerrain.generated.h"

UCLASS()
class AFrozenBasinTerrain : public AColdzoneMapTerrain
{
    GENERATED_BODY()

public:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual double HeightAt(double X, double Y) const override;
    virtual FVector NormalAt(double X, double Y) const override;
    virtual FVector KeepInside(const FVector& Position) const override;

    virtual TConstArrayView<FColdzoneZone> Zones() const override;
    virtual TConstArrayView<TConstArrayView<FVector>> NavigationRoutes() const override;
    virtual FVector2D MapMinimum() const override;
    virtual FVector2D MapMaximum() const override;
    virtual FColor MapColorAt(const FVector2D& Position) const override;
};
