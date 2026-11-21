#include "IcebergNetworkTerrain.h"

#include "ArcticFish.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "IcebergIsland.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr double SeafloorDepth = 850.0;
constexpr double WadeDepth = 30.0;
// Directional wave number in radians per meter, angular speed, amplitude in centimeters.
const FVector4 Waves[] = { FVector4(.38, .24, 1.32, 15.0), FVector4(-.26, .49, 1.56, 8.0),
    FVector4(.69, -.37, 1.94, 4.0) };
}

AIcebergNetworkTerrain::AIcebergNetworkTerrain()
{
    PracticeRing = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PracticeRing"));
    PracticeRing->SetupAttachment(GetRootComponent());
    PracticeRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PracticeRing->SetCanEverAffectNavigation(false);
    PracticeRing->SetCastShadow(false);

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> FishMesh(
        TEXT("/Game/Fish/ArcticFish/SK_ArcticFish.SK_ArcticFish"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> FishSwim(
        TEXT("/Game/Fish/ArcticFish/A_ArcticFishSwim.A_ArcticFishSwim"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> FishRipple(
        TEXT("/Game/Fish/ArcticFish/M_ArcticFishRipple.M_ArcticFishRipple"));
    ArcticFishMesh = FishMesh.Object;
    ArcticFishSwim = FishSwim.Object;
    ArcticFishRipple = FishRipple.Object;
}

void AIcebergNetworkTerrain::BeginPlay()
{
    Super::BeginPlay();
    if (ArcticFishMesh && ArcticFishSwim)
    {
        // In PIE, the persistent world can begin play before the island sublevels are visible.
        GetWorldTimerManager().SetTimer(FishSpawnTimer, this, &AIcebergNetworkTerrain::SpawnArcticFish, 0.1f, true);
        SpawnArcticFish();
    }
}

bool AIcebergNetworkTerrain::FindFishRoute(FVector2D RampLip, FVector2D Outward, FVector2D& Center) const
{
    const FVector2D Along(-Outward.Y, Outward.X);
    const double DeepEnough = GetActorLocation().Z - 155.0;
    for (double Range = 300.0; Range <= 1600.0; Range += 100.0)
    {
        const FVector2D Candidate = RampLip + Outward * Range;
        if (HeightAt(Candidate.X, Candidate.Y) > DeepEnough)
        {
            continue;
        }
        // Include the length of a fish and sample the whole orbit against both seabeds.
        constexpr int32 Samples = 48;
        bool bClear = true;
        for (int32 Sample = 0; Sample < Samples; ++Sample)
        {
            const double Angle = Sample * UE_TWO_PI / Samples;
            const FVector2D Point = Candidate + Outward * ((AArcticFish::RadialRadius + 50.0) * FMath::Cos(Angle))
                + Along * ((AArcticFish::ShoreRadius + 50.0) * FMath::Sin(Angle));
            if (HeightAt(Point.X, Point.Y) > DeepEnough)
            {
                bClear = false;
                break;
            }
        }
        if (bClear)
        {
            Center = Candidate;
            return true;
        }
    }
    return false;
}

void AIcebergNetworkTerrain::SpawnArcticFish()
{
    RefreshIslands();
    if (Islands.Num() < 2)
    {
        return;
    }
    GetWorldTimerManager().ClearTimer(FishSpawnTimer);
    const TOptional<FVector> BaseRamp = Islands[0]->PracticeRampLip();
    const TOptional<FVector> ExpeditionRamp = Islands.Last()->PracticeRampLip();
    if (!BaseRamp || !ExpeditionRamp)
    {
        return;
    }
    const FVector2D Channel = FVector2D(*ExpeditionRamp - *BaseRamp).GetSafeNormal();
    const FVector2D Ramps[] = {FVector2D(*BaseRamp), FVector2D(*ExpeditionRamp)};
    for (int32 School = 0; School < UE_ARRAY_COUNT(Ramps); ++School)
    {
        const FVector2D Outward = School == 0 ? Channel : -Channel;
        FVector2D Center;
        if (FindFishRoute(Ramps[School], Outward, Center))
        {
            SpawnFishSchool(Center, Outward, School);
        }
    }
}

void AIcebergNetworkTerrain::SpawnFishSchool(FVector2D Center, FVector2D Outward, int32 School)
{
    FActorSpawnParameters Parameters;
    Parameters.Owner = this;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 Member = 0; Member < AArcticFish::ChannelSchoolSize; ++Member)
    {
        if (AArcticFish* Fish = GetWorld()->SpawnActor<AArcticFish>(
            FVector(Center, GetActorLocation().Z), FRotator::ZeroRotator, Parameters))
        {
            Fish->Initialize(*this, ArcticFishMesh, ArcticFishSwim, ArcticFishRipple, Center, Outward, School, Member);
        }
    }
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
    RebuildPracticeRing();
}

void AIcebergNetworkTerrain::RebuildPracticeRing()
{
    if (Islands.Num() < 2)
    {
        PracticeRing->ClearAllMeshSections();
        return;
    }
    const TOptional<FVector> BaseRamp = Islands[0]->PracticeRampLip();
    const TOptional<FVector> ExpeditionRamp = Islands.Last()->PracticeRampLip();
    if (!BaseRamp || !ExpeditionRamp)
    {
        PracticeRing->ClearAllMeshSections();
        return;
    }

    const FVector Channel = (*ExpeditionRamp - *BaseRamp).GetSafeNormal2D();
    const FVector Center = (*BaseRamp + *ExpeditionRamp) * 0.5 + FVector::UpVector * 640.0;
    PracticeRing->SetWorldLocationAndRotation(Center, FRotationMatrix::MakeFromXZ(Channel, FVector::UpVector).Rotator());
    constexpr int32 RingSegments = 48;
    constexpr int32 TubeSegments = 8;
    constexpr double RingRadius = 370.0;
    constexpr double TubeRadius = 30.0;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<int32> Triangles;
    for (int32 Ring = 0; Ring < RingSegments; ++Ring)
    {
        const double Angle = UE_TWO_PI * Ring / RingSegments;
        const FVector Radial(0, FMath::Cos(Angle), FMath::Sin(Angle));
        for (int32 Tube = 0; Tube < TubeSegments; ++Tube)
        {
            const double SectionAngle = UE_TWO_PI * Tube / TubeSegments;
            const FVector Normal = Radial * FMath::Cos(SectionAngle) + FVector::ForwardVector * FMath::Sin(SectionAngle);
            Vertices.Add(Radial * RingRadius + Normal * TubeRadius);
            Normals.Add(Normal);
            UVs.Emplace(static_cast<double>(Ring) / RingSegments, static_cast<double>(Tube) / TubeSegments);
            Colors.Add(Ring % 12 < 2 ? FLinearColor(0.94, 0.82, 0.53) : FLinearColor(0.85, 0.30, 0.045));
            const int32 Next = (Tube + 1) % TubeSegments;
            const int32 Row = Ring * TubeSegments;
            const int32 FollowingRow = ((Ring + 1) % RingSegments) * TubeSegments;
            Triangles.Append({Row + Tube, Row + Next, FollowingRow + Tube,
                Row + Next, FollowingRow + Next, FollowingRow + Tube});
        }
    }
    PracticeRing->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, {}, false, false);
    PracticeRing->SetMaterial(0, Islands[0]->IslandMaterial);
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
