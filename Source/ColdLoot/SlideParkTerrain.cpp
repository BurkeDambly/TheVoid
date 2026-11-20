#include "SlideParkTerrain.h"

#include "ProceduralMeshComponent.h"

namespace
{
constexpr int32 Cells = 192;
constexpr double HalfSize = 7200.0;
constexpr double Spacing = 2.0 * HalfSize / Cells;
}

ASlideParkTerrain::ASlideParkTerrain()
{
    Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SnowSurface"));
    SetRootComponent(Surface);
    Surface->bUseComplexAsSimpleCollision = true;
}

double ASlideParkTerrain::LocalHeight(double X, double Y) const
{
    // A level test pad feeds into a descent, a flat runout, then an uphill bank.
    const double Drop = -580.0 * FMath::SmoothStep(600.0, 2600.0, X);
    const double Bank = 720.0 * FMath::SmoothStep(3900.0, 6550.0, X);
    const double AwayFromLane = FMath::SmoothStep(450.0, 1500.0, FMath::Abs(Y));
    const double Phase = HillSeed * 0.137;
    const double Hills = 430.0 * FMath::Sin(X / 1040.0 + Y / 1720.0 + Phase)
        + 275.0 * FMath::Cos(Y / 830.0 - X / 1990.0 - Phase)
        + 95.0 * FMath::Sin((X - Y) / 490.0 + Phase);
    const double RampAlong = FMath::Max(0.0, 1.0 - FMath::Square((X - 145.0) / 190.0));
    const double RampAcross = FMath::Max(0.0, 1.0 - FMath::Square((Y + 520.0) / 140.0));
    const double Ramp = 62.0 * FMath::Square(RampAlong * RampAcross);
    return HillHeightScale * (Drop + Bank + AwayFromLane * Hills + Ramp);
}

double ASlideParkTerrain::HeightAt(double X, double Y) const
{
    const FVector Origin = GetActorLocation();
    return Origin.Z + LocalHeight(X - Origin.X, Y - Origin.Y);
}

FVector ASlideParkTerrain::NormalAt(double X, double Y) const
{
    constexpr double Step = 12.0;
    return FVector(HeightAt(X - Step, Y) - HeightAt(X + Step, Y),
        HeightAt(X, Y - Step) - HeightAt(X, Y + Step), 2.0 * Step).GetSafeNormal();
}

FVector ASlideParkTerrain::KeepInside(const FVector& Position) const
{
    const FVector Origin = GetActorLocation();
    constexpr double Limit = HalfSize - 350.0;
    return FVector(
        FMath::Clamp(Position.X, Origin.X - Limit, Origin.X + Limit),
        FMath::Clamp(Position.Y, Origin.Y - Limit, Origin.Y + Limit), Position.Z);
}

void ASlideParkTerrain::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    constexpr int32 Row = Cells + 1;
    const FVector Origin = GetActorLocation();
    Vertices.Reserve(Row * Row);
    Normals.Reserve(Row * Row);
    UVs.Reserve(Row * Row);
    Colors.Reserve(Row * Row);
    Triangles.Reserve(Cells * Cells * 6);

    for (int32 J = 0; J < Row; ++J)
    {
        for (int32 I = 0; I < Row; ++I)
        {
            const double X = I * Spacing - HalfSize;
            const double Y = J * Spacing - HalfSize;
            const double Z = LocalHeight(X, Y);
            const FVector Normal = NormalAt(X + Origin.X, Y + Origin.Y);
            Vertices.Emplace(X, Y, Z);
            Normals.Add(Normal);
            UVs.Emplace(X / 400.0, Y / 400.0);
            const float Crest = FMath::Clamp((Z + 770.0) / 1600.0, 0.0, 1.0);
            const float Pattern = 0.5f + 0.5f * FMath::Sin(X / 340.0 + FMath::Sin(Y / 330.0));
            const float Snow = FMath::Clamp(Crest * 0.62f + Normal.Z * 0.21f + Pattern * 0.13f, 0.0f, 1.0f);
            Colors.Add(FMath::Lerp(FLinearColor(0.26f, 0.46f, 0.61f), FLinearColor(0.83f, 0.92f, 0.96f), Snow));
            if (I < Cells && J < Cells)
            {
                const int32 A = J * Row + I;
                Triangles.Append({A, A + Row, A + 1, A + 1, A + Row, A + Row + 1});
            }
        }
    }

    Surface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, {}, true, true);
    Surface->SetMaterial(0, SnowMaterial);
}
