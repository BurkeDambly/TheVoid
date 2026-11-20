#include "IcebergIsland.h"

#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr int32 Row = 105;
constexpr double Half = 104.0;
constexpr double Step = 2.0;
constexpr double Seafloor = -850.0;

double Mask(const FVector2D& P, const FVector2D& Center, const FVector2D& Width)
{
    return FMath::Exp(-((P - Center) / Width).SizeSquared());
}
}

AIcebergIsland::AIcebergIsland()
{
    Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("IcebergSurface"));
    Surface->bUseComplexAsSimpleCollision = true;
    Surface->SetCanEverAffectNavigation(false);
    SetRootComponent(Surface);
}

double AIcebergIsland::Elevation(const FVector2D& P) const
{
    const FVector2D Radius = bBaseIsland ? FVector2D(76, 61) : FVector2D(73, 66);
    const FVector2D Q = P / Radius;
    const double Angle = FMath::Atan2(Q.Y, Q.X);
    const double Coast = Q.Size() / (1.0 + .064 * FMath::Sin(3 * Angle + .65) + .032 * FMath::Cos(5 * Angle));
    const double Detail = FMath::PerlinNoise2D(P / 23.0);
    double Ground;
    if (bBaseIsland)
    {
        Ground = 6.5 + 7.0 * Mask(P, {-35, 18}, {30, 31}) + 5.5 * Mask(P, {4, 34}, {31, 19}) + 1.3 * Detail;
        const double Clearing = FMath::SmoothStep(.24, .73, Mask(P, {0, -12}, {42, 31}));
        Ground = FMath::Lerp(Ground, 7.0, Clearing);
        const double EastBeach = FMath::SmoothStep(25.0, 68.0, P.X) * Mask(P, {61, 0}, {49, 24});
        Ground = FMath::Lerp(Ground, 3.2, EastBeach);
    }
    else
    {
        Ground = 4.9 + 23.0 * Mask(P, {10, 14}, {42, 35}) + 13.0 * Mask(P, {33, 33}, {29, 21})
            + 8.0 * Mask(P, {37, -24}, {29, 25}) + 2.7 * Detail;
        const double LandingShelf = FMath::SmoothStep(.19, .72, Mask(P, {-48, -3}, {34, 32}));
        Ground = FMath::Lerp(Ground, 3.1 + .13 * FMath::Max(0.0, P.X + 51.0), LandingShelf);
        const double Saddle = FMath::SmoothStep(.18, .77, Mask(P, {3, -26}, {34, 20}));
        Ground = FMath::Lerp(Ground, 9.5 + .11 * P.X, Saddle * .58);
    }
    const double Approach = 1.0 - FMath::SmoothStep(12.0, 28.0, FMath::Abs(P.Y));
    const double ShelfMask = FMath::SmoothStep(.70, .86, Coast) * (1.0 - FMath::SmoothStep(1.045, 1.13, Coast));
    const double Ledge = 1.4 + 1.1 * FMath::Sin(4.0 * Angle + .3) + .6 * FMath::Sin(9.0 * Angle - .5);
    const double Shelf = ShelfMask * Ledge * (1.0 - Approach);
    return FMath::Lerp(Ground, Seafloor / 100.0, FMath::SmoothStep(.80, 1.13, Coast)) + Shelf;
}

FLinearColor AIcebergIsland::IceColor(const FVector2D& P, double Height, double Up) const
{
    const double Grain = FMath::Clamp(.52 + FMath::PerlinNoise2D(P / 12.0) * .57, 0.0, 1.0);
    const FLinearColor Snow = FMath::Lerp(FLinearColor(.55,.69,.76), FLinearColor(.87,.94,.97), Grain);
    const FLinearColor BlueIce = FMath::Lerp(FLinearColor(.075,.31,.40), FLinearColor(.28,.62,.72), Grain);
    const double CoastIce = 1.0 - FMath::SmoothStep(1.0, 6.0, Height);
    const double RidgeIce = FMath::SmoothStep(.06, .35, 1.0 - Up);
    FLinearColor Color = FMath::Lerp(Snow, BlueIce, FMath::Max(CoastIce, RidgeIce));
    const double Layer = .5 + .5 * FMath::Sin((Height + .72 * FMath::PerlinNoise2D(P / 13.0)) * 3.6);
    const double Exposed = FMath::SmoothStep(.08, .38, 1.0 - Up) * (1.0 - FMath::SmoothStep(11.0, 19.0, Height));
    Color = FMath::Lerp(Color, FMath::Lerp(FLinearColor(.10,.33,.44), FLinearColor(.49,.75,.83), Layer), Exposed * .58);
    const double Vein = FMath::Abs(FMath::Sin(P.Y * .26 + P.X * .13 + 2.1 * FMath::PerlinNoise2D(P / 15.0)));
    return FMath::Lerp(Color, FLinearColor(.055,.25,.37), (1.0 - FMath::SmoothStep(.06,.31,Vein)) * RidgeIce * .41);
}

double AIcebergIsland::HeightAt(double WorldX, double WorldY) const
{
    const FVector2D Point = (FVector2D(WorldX, WorldY) - FVector2D(GetActorLocation())) / 100.0;
    if (FMath::Abs(Point.X) >= Half || FMath::Abs(Point.Y) >= Half)
    {
        return GetActorLocation().Z + Seafloor;
    }
    const FVector2D Grid = (Point + FVector2D(Half, Half)) / Step;
    const int32 X = FMath::FloorToInt(Grid.X);
    const int32 Y = FMath::FloorToInt(Grid.Y);
    const FProcMeshSection* Section = Surface->GetProcMeshSection(0);
    if (!Section || Section->ProcVertexBuffer.Num() != Row * Row)
    {
        return GetActorLocation().Z + Elevation(Point) * 100.0;
    }
    const auto& Vertices = Section->ProcVertexBuffer;
    const int32 Index = Y * Row + X;
    const double U = Grid.X - X;
    const double V = Grid.Y - Y;
    const double East = Vertices[Index + 1].Position.Z;
    const double South = Vertices[Index + Row].Position.Z;
    const double Z = U + V <= 1.0 ? (1.0 - U - V) * Vertices[Index].Position.Z + U * East + V * South
        : (U + V - 1.0) * Vertices[Index + Row + 1].Position.Z + (1.0 - V) * East + (1.0 - U) * South;
    return GetActorLocation().Z + Z;
}

FColor AIcebergIsland::ColorAt(double WorldX, double WorldY) const
{
    const FVector2D P = (FVector2D(WorldX, WorldY) - FVector2D(GetActorLocation())) / 100.0;
    const double Z = Elevation(P);
    const double DX = Elevation(P + FVector2D(2, 0)) - Elevation(P - FVector2D(2, 0));
    const double DY = Elevation(P + FVector2D(0, 2)) - Elevation(P - FVector2D(0, 2));
    const FVector N = FVector(-DX, -DY, 4).GetSafeNormal();
    const float Relief = .64f + .36f * FMath::Max(0.0, FVector::DotProduct(N, FVector(-.40, .32, .86).GetSafeNormal()));
    return (IceColor(P, Z, N.Z) * Relief).ToFColorSRGB();
}

void AIcebergIsland::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildSurface();
}

void AIcebergIsland::RebuildSurface()
{
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<int32> Triangles;
    for (int32 Y = 0; Y < Row; ++Y)
    {
        for (int32 X = 0; X < Row; ++X)
        {
            const FVector2D P(X * Step - Half, Y * Step - Half);
            Vertices.Emplace(P * 100.0, Elevation(P) * 100.0);
            UVs.Add(P / 8.0);
        }
    }
    for (int32 Y = 0; Y < Row; ++Y)
    {
        for (int32 X = 0; X < Row; ++X)
        {
            const int32 I = Y * Row + X;
            const FVector East = Vertices[Y * Row + FMath::Min(X + 1, Row - 1)] - Vertices[Y * Row + FMath::Max(0, X - 1)];
            const FVector South = Vertices[FMath::Min(Y + 1, Row - 1) * Row + X] - Vertices[FMath::Max(0, Y - 1) * Row + X];
            const FVector N = FVector::CrossProduct(East, South).GetSafeNormal();
            Normals.Add(N);
            Colors.Add(IceColor(FVector2D(Vertices[I]) / 100.0, Vertices[I].Z / 100.0, N.Z));
            if (X < Row - 1 && Y < Row - 1)
            {
                Triangles.Append({I, I + Row, I + 1, I + 1, I + Row, I + Row + 1});
            }
        }
    }
    Surface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, {}, true, true);
    Surface->SetMaterial(0, IslandMaterial);
}

bool AIcebergIsland::ExportBlenderMesh(const FString& Filename) const
{
    const FProcMeshSection* Mesh = Surface->GetProcMeshSection(0);
    if (!Mesh || Mesh->ProcVertexBuffer.IsEmpty())
    {
        return false;
    }
    FString File = FString::Printf(TEXT("ply\nformat ascii 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\nelement face %d\nproperty list uchar int vertex_indices\nend_header\n"),
        Mesh->ProcVertexBuffer.Num(), Mesh->ProcIndexBuffer.Num() / 3);
    File.Reserve(1024 * 1024);
    for (const auto& Vertex : Mesh->ProcVertexBuffer)
    {
        // Convert Unreal centimeters and left-handed Y to Blender meters and right-handed Y.
        File += FString::Printf(TEXT("%.5f %.5f %.5f %d %d %d\n"), Vertex.Position.X / 100, -Vertex.Position.Y / 100,
            Vertex.Position.Z / 100, Vertex.Color.R, Vertex.Color.G, Vertex.Color.B);
    }
    for (int32 I = 0; I < Mesh->ProcIndexBuffer.Num(); I += 3)
    {
        File += FString::Printf(TEXT("3 %d %d %d\n"), Mesh->ProcIndexBuffer[I], Mesh->ProcIndexBuffer[I + 1], Mesh->ProcIndexBuffer[I + 2]);
    }
    return FFileHelper::SaveStringToFile(File, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
