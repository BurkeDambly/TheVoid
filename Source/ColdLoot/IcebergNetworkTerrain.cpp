#include "IcebergNetworkTerrain.h"

#include "EngineUtils.h"
#include "IcebergIsland.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr double SeafloorDepth = 850.0;
constexpr double WadeDepth = 30.0;
// Directional wave number in radians per meter, angular speed, amplitude in centimeters.
const FVector4 Waves[] = { FVector4(.38, .24, 1.32, 15.0), FVector4(-.26, .49, 1.56, 8.0),
    FVector4(.69, -.37, 1.94, 4.0) };
}

void AIcebergNetworkTerrain::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    RefreshIslands();
}

void AIcebergNetworkTerrain::OnConstruction(const FTransform& Transform)
{
    // The islands own the rendered surfaces and can be authored independently.
    AActor::OnConstruction(Transform);
    RefreshIslands();
}

void AIcebergNetworkTerrain::RefreshIslands()
{
    Islands.Reset();
    Locations.Reset();
    for (TActorIterator<AIcebergIsland> It(GetWorld()); It; ++It)
    {
        Islands.Add(*It);
    }
    Islands.Sort([](const AIcebergIsland& A, const AIcebergIsland& B)
    {
        return A.bBaseIsland != B.bBaseIsland ? A.bBaseIsland
            : A.GetActorLocation().X < B.GetActorLocation().X;
    });
    FBox2D Bounds(ForceInit);
    for (const auto& Island : Islands)
    {
        const FVector2D Center = ToMapPoint(Island->GetActorLocation());
        Locations.Add({Island->IslandName, Center});
        Bounds += Center;
    }
    if (Bounds.bIsValid)
    {
        MapCenter = Bounds.GetCenter();
        MapHalfSize = Bounds.GetExtent().GetMax() + 108.0;
    }
}

double AIcebergNetworkTerrain::HeightAt(double X, double Y) const
{
    double Height = GetActorLocation().Z - SeafloorDepth;
    for (const auto& Island : Islands)
    {
        Height = FMath::Max(Height, Island->HeightAt(X, Y));
    }
    return Height;
}

FVector AIcebergNetworkTerrain::NormalAt(double X, double Y) const
{
    constexpr double Spacing = 65.0;
    return FVector(HeightAt(X - Spacing, Y) - HeightAt(X + Spacing, Y),
        HeightAt(X, Y - Spacing) - HeightAt(X, Y + Spacing), 2.0 * Spacing).GetSafeNormal();
}

TOptional<double> AIcebergNetworkTerrain::WaterSurfaceAt(double X, double Y) const
{
    const double Sea = GetActorLocation().Z;
    if (HeightAt(X, Y) >= Sea - WadeDepth)
    {
        return {};
    }
    return Sea + WaveOffset(X, Y, GetWorld()->GetTimeSeconds());
}

TArray<FVector4> AIcebergNetworkTerrain::OceanWaves() const
{
    TArray<FVector4> Result;
    Result.Append(Waves, UE_ARRAY_COUNT(Waves));
    return Result;
}

double AIcebergNetworkTerrain::WaveOffset(double X, double Y, double Time) const
{
    double Offset = 0.0;
    for (const FVector4& Wave : Waves)
    {
        Offset += Wave.W * FMath::Sin((X * Wave.X + Y * Wave.Y) * .01 - Time * Wave.Z);
    }
    return Offset;
}

void AIcebergNetworkTerrain::RebuildOcean()
{
    // Dense in the playable water, with a continuous coarse skirt toward the horizon.
    constexpr int32 NearCells = 112;
    constexpr double Spacing = 200.0;
    constexpr double Skirt[] = {23500, 26000, 32000, 46000, 76000, 125000, 190000};
    TArray<double> Coordinates;
    for (int32 I = UE_ARRAY_COUNT(Skirt) - 1; I >= 0; --I)
    {
        Coordinates.Add(-Skirt[I]);
    }
    for (int32 I = -NearCells; I <= NearCells; ++I)
    {
        Coordinates.Add(I * Spacing);
    }
    Coordinates.Append(Skirt, UE_ARRAY_COUNT(Skirt));

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    const int32 Row = Coordinates.Num();
    const FVector2D Shift = MapCenter * 100.0;
    Vertices.Reserve(Row * Row);
    UVs.Reserve(Row * Row);
    Colors.Reserve(Row * Row);
    Triangles.Reserve(FMath::Square(Row - 1) * 6);
    for (int32 Y = 0; Y < Row; ++Y)
    {
        for (int32 X = 0; X < Row; ++X)
        {
            const FVector2D Point(Coordinates[X], Coordinates[Y]);
            Vertices.Emplace(Point + Shift, 0.0);
            UVs.Add(Point / 100.0);
            const double Fade = 1.0 - FMath::SmoothStep(22450.0, 36000.0,
                FMath::Max(FMath::Abs(Point.X), FMath::Abs(Point.Y)));
            Colors.Emplace(Fade, 0, 0, 1);
            if (X + 1 < Row && Y + 1 < Row)
            {
                const int32 I = Y * Row + X;
                Triangles.Append({I, I + Row, I + 1, I + 1, I + Row, I + Row + 1});
            }
        }
    }
    Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Surface->SetCanEverAffectNavigation(false);
    Surface->SetCastShadow(false);
    Surface->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, UVs, Colors, {}, false, false);
    Surface->SetMaterial(0, OceanMaterial);
}

FVector AIcebergNetworkTerrain::KeepInside(const FVector& Position) const
{
    const FVector2D Point = ToMapPoint(Position);
    const FVector2D Min = MapMinimum();
    const FVector2D Max = MapMaximum();
    const FVector2D Limited(FMath::Clamp(Point.X, Min.X, Max.X), FMath::Clamp(Point.Y, Min.Y, Max.Y));
    return FVector(FVector2D(GetActorLocation()) + Limited * 100.0, Position.Z);
}

FColor AIcebergNetworkTerrain::MapColorAt(const FVector2D& Point) const
{
    const FVector2D World = FVector2D(GetActorLocation()) + Point * 100.0;
    const AIcebergIsland* Top = nullptr;
    double Height = GetActorLocation().Z - SeafloorDepth;
    for (const auto& Island : Islands)
    {
        const double Candidate = Island->HeightAt(World.X, World.Y);
        if (Candidate > Height)
        {
            Height = Candidate;
            Top = Island.Get();
        }
    }
    if (Top && Height >= GetActorLocation().Z)
    {
        return Top->ColorAt(World.X, World.Y);
    }
    return FMath::Lerp(FLinearColor(.013,.048,.077), FLinearColor(.029,.24,.28),
        FMath::SmoothStep(-650.0, 0.0, Height - GetActorLocation().Z)).ToFColorSRGB();
}
