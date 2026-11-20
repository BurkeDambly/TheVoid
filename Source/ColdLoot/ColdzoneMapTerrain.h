#pragma once

#include "SlideParkTerrain.h"
#include "ColdzoneMapTerrain.generated.h"

struct FColdzoneZone
{
    FString Name;
    FVector2D Center;
};

UCLASS(Abstract)
class AColdzoneMapTerrain : public ASlideParkTerrain
{
    GENERATED_BODY()

public:
    virtual TConstArrayView<FColdzoneZone> Zones() const PURE_VIRTUAL(AColdzoneMapTerrain::Zones, return {};);
    virtual TConstArrayView<TConstArrayView<FVector>> NavigationRoutes() const { return {}; }
    virtual FVector2D MapMinimum() const PURE_VIRTUAL(AColdzoneMapTerrain::MapMinimum, return {};);
    virtual FVector2D MapMaximum() const PURE_VIRTUAL(AColdzoneMapTerrain::MapMaximum, return {};);
    virtual FColor MapColorAt(const FVector2D& Point) const PURE_VIRTUAL(AColdzoneMapTerrain::MapColorAt, return FColor::Black;);

    FVector2D ToMapPoint(const FVector& Location) const { return FVector2D(Location - GetActorLocation()) / 100.0; }

    UFUNCTION(BlueprintPure, Category = "Coldzone Map")
    double SurfaceHeight(double WorldX, double WorldY) const { return HeightAt(WorldX, WorldY); }

    UFUNCTION(BlueprintPure, Category = "Coldzone Map")
    TArray<FString> ZoneNames() const;

    UFUNCTION(BlueprintPure, Category = "Coldzone Map")
    FVector ZoneLocation(int32 Index) const;
};
