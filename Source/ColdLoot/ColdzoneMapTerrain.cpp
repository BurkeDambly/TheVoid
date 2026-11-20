#include "ColdzoneMapTerrain.h"

TArray<FString> AColdzoneMapTerrain::ZoneNames() const
{
    TArray<FString> Names;
    for (const auto& Zone : Zones())
    {
        Names.Add(Zone.Name);
    }
    return Names;
}

FVector AColdzoneMapTerrain::ZoneLocation(int32 Index) const
{
    const auto Locations = Zones();
    if (!Locations.IsValidIndex(Index))
    {
        return GetActorLocation();
    }
    FVector Location = GetActorLocation() + FVector(Locations[Index].Center * 100.0, 0.0);
    Location.Z = HeightAt(Location.X, Location.Y);
    return Location;
}
