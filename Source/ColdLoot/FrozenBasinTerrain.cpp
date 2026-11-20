#include "FrozenBasinTerrain.h"

#include "ProceduralMeshComponent.h"

namespace
{
constexpr double Units = 100.0;
constexpr double Spacing = 11.0;
constexpr int32 Columns = 377;
constexpr int32 Rows = 351;
constexpr double ShoreLimit = 1.015;
constexpr double RouteShoulder = 105.0;
constexpr double BasinIceHeight = 105.0;
const FVector2D Corner(-2068, -1925);
const FVector2D CoastRadii(1800, 1620);

struct FBasinLandmark
{
    const TCHAR* Name;
    FVector2D Center;
    FVector2D Extent;
};

const FBasinLandmark Landmarks[] = {
    {TEXT("Summit Crown"),   {    0,     0}, {115, 115}},
    {TEXT("Glacier Cirque"), {  365,  -755}, {245, 185}},
    {TEXT("Open Quarry"),    {  535,   680}, {237, 198}},
    {TEXT("Frost Basin"),   {-1035,   440}, {270, 185}},
    {TEXT("Black Sand Bay"), { -240, -1330}, {340, 140}},
    {TEXT("Tidal Shelf"),    { 1200,   890}, {260, 175}},
    {TEXT("Frozen Tarn"),    { 1120,  -615}, {222, 145}},
    {TEXT("Split Gorge"),    {-1160,  -480}, {255, 165}},
    {TEXT("Windfield"),      { -615, -1000}, {240, 170}},
    {TEXT("Basalt Steps"),   {   50,  1180}, {285, 185}},
    {TEXT("North Saddle"),   { -770,   -20}, {230, 145}},
    {TEXT("White Dunes"),    { 1050,   200}, {270, 210}},
};

const FVector CoastalLoop[] = {
    {1210,875,25},{905,1070,39},{525,1280,42},{50,1180,57},{-430,1180,56},{-835,910,63},
    {-1100,630,72},{-1245,294,73},{-1360,-65,70},{-1190,-505,62},{-1010,-825,48},
    {-651,-1030,65},{-240,-1270,13},{270,-1330,34},{700,-1130,31},{1005,-865,34},
    {1135,-717,41},{1305,-585,54},{1480,-180,67},{1440,365,54},{1325,695,39},{1210,875,25},
};
const FVector HighTraverse[] = {
    {450,330,413},{230,550,370},{-95,640,318},{-440,590,283},{-751,621,233},
    {-591,511,244},{-515,392,261},{-533,240,283},{-655,109,270},
    {-770,-20,248},{-810,-326,216},{-665,-630,194},{-338,-796,195},{37,-796,216},
    {365,-755,231},{660,-570,248},{820,-305,251},{950,-30,194},{991,177,176},{755,245,278},
    {620,335,337},{450,330,413},
};
const FVector SummitSwitchbacks[] = {
    {-6,0,610},{55,0,606},{142,23,580},{127,126,553},{239,105,522},
    {244,221,493},{364,199,458},{450,330,413},
};
const FVector GlacierLink[] = {
    {37,-796,216},{98,-972,177},{232,-937,138},{330,-1077,96},{475,-1011,64},{700,-1130,31},
};
const FVector QuarryLink[] = {
    {450,330,413},{586,354,367},{657,459,329},{542,530,268},{443,625,231},{462,737,200},
    {535,680,174},{621,735,201},
    {774,684,194},{909,778,155},{1027,700,125},{1110,830,82},{1230,749,43},{1210,875,25},
};
const FVector GorgeLink[] = {
    {-770,-20,248},{-927,-87,212},{-1044,-206,174},{-1015,-370,137},
    {-1140,-443,96},{-1190,-505,62},
};
const FVector BasinPath[] = {
    {-1245,294,73},{-1190,373,90},{-1176,489,106},{-1095,541,113},
    {-975,575,146},{-920,667,178},{-815,678,208},{-751,621,233},
};
const TConstArrayView<FVector> Routes[] = {
    CoastalLoop, HighTraverse, SummitSwitchbacks, GlacierLink, QuarryLink, GorgeLink, BasinPath,
};

double Hill(const FVector2D& P, const FVector2D& Center, const FVector2D& Width)
{
    return FMath::Exp(-((P - Center) / Width).SizeSquared());
}

double ZoneFalloff(const FVector2D& P, int32 Index)
{
    const FBasinLandmark& Zone = Landmarks[Index];
    return Hill(P, Zone.Center, Zone.Extent);
}

double Noise(const FVector2D& P, double Scale)
{
    return FMath::PerlinNoise2D(P / Scale);
}

double Coast(const FVector2D& P)
{
    const FVector2D Ellipse = P / CoastRadii;
    const double Angle = FMath::Atan2(Ellipse.Y, Ellipse.X);
    const double Shore = 1.0 + .039 * FMath::Sin(3 * Angle + .4) + .028 * FMath::Cos(7 * Angle - .6);
    return Ellipse.Size() / Shore;
}

struct FRouteSample
{
    double Distance = RouteShoulder;
    double Height = 0.0;
};

FRouteSample SampleRoute(const FVector2D& P)
{
    double NearestSq = FMath::Square(RouteShoulder);
    double WeightedHeight = 0.0;
    double TotalWeight = 0.0;
    for (const auto Route : Routes)
    {
        for (int32 I = 1; I < Route.Num(); ++I)
        {
            const FVector2D A(Route[I - 1]);
            const FVector2D B(Route[I]);
            if (P.X < FMath::Min(A.X, B.X) - RouteShoulder || P.X > FMath::Max(A.X, B.X) + RouteShoulder
                || P.Y < FMath::Min(A.Y, B.Y) - RouteShoulder || P.Y > FMath::Max(A.Y, B.Y) + RouteShoulder)
            {
                continue;
            }
            const FVector2D Delta = B - A;
            const double T = FMath::Clamp(FVector2D::DotProduct(P - A, Delta) / Delta.SizeSquared(), 0.0, 1.0);
            const double DistanceSq = (P - A - T * Delta).SizeSquared();
            if (DistanceSq < FMath::Square(RouteShoulder))
            {
                NearestSq = FMath::Min(NearestSq, DistanceSq);
                const double Influence = 1.0 - FMath::SmoothStep(0.0, RouteShoulder, FMath::Sqrt(DistanceSq));
                // Nearby segments blend through switchbacks and junctions instead of creating a height seam.
                const double Weight = FMath::Square(Influence) / (FMath::Square(23.0) + DistanceSq);
                WeightedHeight += FMath::Lerp(Route[I - 1].Z, Route[I].Z, T) * Weight;
                TotalWeight += Weight;
            }
        }
    }
    return {FMath::Sqrt(NearestSq), TotalWeight > 0.0 ? WeightedHeight / TotalWeight : 0.0};
}

double BasinIceMask(const FVector2D& P)
{
    return FMath::SmoothStep(.29, .57, Hill(P, Landmarks[3].Center, {190, 109}));
}

double SculptBasin(const FVector2D& P, double Z)
{
    // The headwall faces inland and the paired walls taper away at the coastal mouth.
    const double Head = 129 * Hill(P, {-804, 444}, {101, 207});
    const double LowerWing = 87 * Hill(P, {-1032, 282}, {223, 72});
    const double UpperWing = 96 * Hill(P, {-1023, 611}, {212, 83});
    const double Ground = BasinIceHeight + .08 * (P.X + 1035) + FMath::Max3(Head, LowerWing, UpperWing);
    const double Footprint = FMath::SmoothStep(.21, .58, Hill(P, {-1014, 443}, {382, 280}));
    return FMath::Lerp(Z, Ground, Footprint);
}

double GorgeAxis(double X)
{
    return -485.0 + 85.0 * FMath::Sin((X + 1160.0) / 177.0);
}

double Elevation(const FVector2D& P)
{
    const double CoastDistance = Coast(P);
    if (CoastDistance > 1.10)
    {
        return -26.0;
    }

    const double Mountain = Hill(P, {0, 0}, {850, 820});
    const double Mass = Hill(P, {-60, 45}, {1220, 1060});
    const double Spurs = 119 * Hill(P, {-330, 520}, {490, 209}) + 103 * Hill(P, {350, -445}, {280, 460})
        + 100 * Hill(P, {-530, -288}, {560, 200}) + 81 * Hill(P, {728, 365}, {355, 228});
    const double Relief = Noise(P, 340) * 21 + Noise(P, 147) * 7;
    double Z = 12.0 + 363.0 * Mountain + 138.0 * Mass + Spurs + Relief * (0.28 + 0.72 * Mass);

    Z = FMath::Lerp(Z, 219.0 + 0.14 * (P.X - 365) + 8 * FMath::Sin((P.X + P.Y * 0.3) / 51.0),
        FMath::SmoothStep(.25, .86, ZoneFalloff(P, 1)));
    const double Pit = ((P - Landmarks[2].Center) / Landmarks[2].Extent).Size();
    const double Benches = 174 + 27 * FMath::SmoothStep(.22,.32,Pit) + 29 * FMath::SmoothStep(.43,.53,Pit)
        + 31 * FMath::SmoothStep(.63,.73,Pit) + 33 * FMath::SmoothStep(.83,.96,Pit);
    Z = FMath::Lerp(Benches + 2 * Noise(P, 33), Z, FMath::SmoothStep(.95, 1.48, Pit));

    Z = FMath::Lerp(Z, 11 + 3.5 * Noise(P, 70), FMath::SmoothStep(.22, .75, ZoneFalloff(P, 4)));
    Z = FMath::Lerp(Z, 24 + 8 * FMath::Sin((P.X * .7 + P.Y) / 31.0), FMath::SmoothStep(.21, .84, ZoneFalloff(P, 5)));
    Z = FMath::Lerp(Z, 41 + 0.10 * Noise(P, 70), FMath::SmoothStep(.25, .61, ZoneFalloff(P, 6)));

    const double Gorge = FMath::SmoothStep(-1475.0, -1300.0, P.X) * (1 - FMath::SmoothStep(-930.0, -820.0, P.X))
        * (1 - FMath::SmoothStep(30.0, 126.0, FMath::Abs(P.Y - GorgeAxis(P.X))));
    Z = FMath::Lerp(Z, FMath::Min(Z, 64 + .22 * (P.X + 1200) + 3 * Noise(P, 44)), Gorge);
    Z = FMath::Lerp(Z, 85 + Noise(P, 102) * 17.0, FMath::SmoothStep(.26, .87, ZoneFalloff(P, 8)));
    const double Steps = 65 + 24 * FMath::SmoothStep(.23, .48, .5 + .5 * FMath::Sin((P.Y + .24 * P.X) / 39.0));
    Z = FMath::Lerp(Z, Steps + Noise(P, 72) * 9.0, FMath::SmoothStep(.18, .81, ZoneFalloff(P, 9)));
    Z = FMath::Lerp(Z, 245 + 18 * FMath::Square(P.Y / 145.0), FMath::SmoothStep(.27,.82,ZoneFalloff(P,10)));
    const double Dunes = 108 + 19 * FMath::Sin((P.X + .77 * P.Y) / 85.0 + Noise(P, 208));
    Z = FMath::Lerp(Z, Dunes, FMath::SmoothStep(.24, .79, ZoneFalloff(P, 11)));

    Z = SculptBasin(P, Z);
    const FRouteSample Route = SampleRoute(P);
    Z = FMath::Lerp(Route.Height, Z, FMath::SmoothStep(13.0, RouteShoulder, Route.Distance));
    Z = FMath::Lerp(Z, BasinIceHeight, BasinIceMask(P));

    const double Crown = Hill(P, {0, 0}, { P.X > 0 ? 79.0 : 105.0, 108});
    // Adjacent crags frame the summit trail and leave its first switchback open.
    const double Crags = FMath::Max(Hill(P, {96, 66}, {31, 25}), Hill(P, {143, -101}, {49, 32}));
    Z = FMath::Lerp(Z, 608.0, FMath::SmoothStep(.19, .91, Crags));
    Z = FMath::Lerp(FMath::Min(Z, 608.0), 610.0, FMath::SmoothStep(.33, .96, Crown));
    const double Shore = FMath::Lerp(FMath::Min(Z, 22.0), -26.0, FMath::SmoothStep(.96, 1.077, CoastDistance));
    return FMath::Lerp(Z, Shore, FMath::SmoothStep(.87, 1.025, CoastDistance));
}

FLinearColor SurfaceColor(const FVector2D& P, double Z, double Up)
{
    const double Grain = FMath::Clamp(.58 + Noise(P, 170) * .56 + Noise(P, 47) * .19, 0.0, 1.0);
    const double Rock = FMath::SmoothStep(.08, .41, 1 - Up + Noise(P, 116) * .16);
    const FLinearColor Snow = FMath::Lerp(FLinearColor(.43,.53,.64), FLinearColor(.90,.94,.97), Grain);
    const FLinearColor Stone = FMath::Lerp(FLinearColor(.075,.091,.13), FLinearColor(.32,.30,.31), Grain);
    FLinearColor Color = FMath::Lerp(Snow, Stone, Rock);
    const FLinearColor Ice = FMath::Lerp(FLinearColor(.08,.26,.36), FLinearColor(.51,.76,.81), Grain);
    Color = FMath::Lerp(Color, Ice, FMath::SmoothStep(.27,.72,ZoneFalloff(P,1)) * .91);
    Color = FMath::Lerp(Color, Ice * .81f, FMath::SmoothStep(.36,.68,ZoneFalloff(P,6)) * (1 - FMath::SmoothStep(42.1, 54.0, Z)));
    const FLinearColor Quarry = FMath::Lerp(FLinearColor(.17,.10,.073), FLinearColor(.46,.33,.23), Grain);
    Color = FMath::Lerp(Color, Quarry, FMath::SmoothStep(.21,.58,ZoneFalloff(P,2)) * .93);
    const FLinearColor Gravel = FMath::Lerp(FLinearColor(.025,.036,.046), FLinearColor(.16,.20,.22), Grain);
    Color = FMath::Lerp(Color, Gravel, FMath::SmoothStep(.17,.67,ZoneFalloff(P,4)) * .95);
    Color = FMath::Lerp(Color, FLinearColor(.37,.43,.42), FMath::SmoothStep(.24,.67,ZoneFalloff(P,5)) * .70);
    Color = FMath::Lerp(Color, FLinearColor(.23,.22,.26), FMath::SmoothStep(.2,.76,ZoneFalloff(P,9)) * .71);
    Color = FMath::Lerp(Color, Snow * 1.12f, FMath::SmoothStep(.27,.82,ZoneFalloff(P,11)) * .78);
    const double Frost = 1.0 - FMath::SmoothStep(.009, .045, FMath::Abs(Noise(P + FVector2D(37, -11), 46)));
    const FLinearColor BasinIce = FMath::Lerp(FLinearColor(.07,.27,.39), FLinearColor(.48,.72,.78), .26 + .53 * Frost);
    const double Sheet = BasinIceMask(P);
    Color = FMath::Lerp(Color, BasinIce, Sheet);
    const double Route = 1 - FMath::SmoothStep(10.0, 30.0, SampleRoute(P).Distance);
    return FMath::Lerp(Color, FLinearColor(.61,.65,.67), Route * (1.0 - Sheet) * .46);
}

FVector2D MeshPoint(int32 X, int32 Y)
{
    return Corner + FVector2D(X, Y) * Spacing;
}
}

TConstArrayView<FColdzoneZone> AFrozenBasinTerrain::Zones() const
{
    static const TArray<FColdzoneZone> Locations = []
    {
        TArray<FColdzoneZone> Result;
        for (const auto& Site : Landmarks)
        {
            Result.Add({Site.Name, Site.Center});
        }
        return Result;
    }();
    return Locations;
}

TConstArrayView<TConstArrayView<FVector>> AFrozenBasinTerrain::NavigationRoutes() const { return Routes; }
FVector2D AFrozenBasinTerrain::MapMinimum() const { return Corner; }
FVector2D AFrozenBasinTerrain::MapMaximum() const { return MeshPoint(Columns - 1, Rows - 1); }

double AFrozenBasinTerrain::HeightAt(double X, double Y) const
{
    const FVector2D Point = ToMapPoint(FVector(X, Y, 0));
    const FVector2D Grid = (Point - Corner) / Spacing;
    const int32 I = FMath::Clamp(FMath::FloorToInt(Grid.X), 0, Columns - 2);
    const int32 J = FMath::Clamp(FMath::FloorToInt(Grid.Y), 0, Rows - 2);
    const double U = FMath::Clamp(Grid.X - I, 0.0, 1.0);
    const double V = FMath::Clamp(Grid.Y - J, 0.0, 1.0);
    const FProcMeshSection* Section = Surface->GetProcMeshSection(0);
    auto Sample = [Section](int32 Col, int32 Row)
    {
        return Section && Section->ProcVertexBuffer.Num() == Columns * Rows
            ? Section->ProcVertexBuffer[Row * Columns + Col].Position.Z : Units * Elevation(MeshPoint(Col, Row));
    };
    const double East = Sample(I + 1, J);
    const double South = Sample(I, J + 1);
    const double Z = U + V <= 1.0 ? (1.0 - U - V) * Sample(I, J) + U * East + V * South
        : (U + V - 1.0) * Sample(I + 1, J + 1) + (1.0 - V) * East + (1.0 - U) * South;
    return GetActorLocation().Z + Z;
}

FVector AFrozenBasinTerrain::NormalAt(double X, double Y) const
{
    const FVector2D P = ToMapPoint(FVector(X, Y, 0));
    constexpr double D = 3.5;
    return FVector(Elevation(P - FVector2D(D, 0)) - Elevation(P + FVector2D(D, 0)),
        Elevation(P - FVector2D(0, D)) - Elevation(P + FVector2D(0, D)), 2.0 * D).GetSafeNormal();
}

FVector AFrozenBasinTerrain::KeepInside(const FVector& Position) const
{
    const FVector2D P = ToMapPoint(Position);
    const double Radius = Coast(P);
    if (Radius <= ShoreLimit)
    {
        return Position;
    }
    const FVector2D Edge = FVector2D(GetActorLocation()) + P * (Units * ShoreLimit / Radius);
    return FVector(Edge, Position.Z);
}

FColor AFrozenBasinTerrain::MapColorAt(const FVector2D& P) const
{
    const double Z = Elevation(P);
    if (Z < -4)
    {
        return FMath::Lerp(FLinearColor(.018,.039,.063), FLinearColor(.065,.15,.19),
            FMath::SmoothStep(-25.0, -4.0, Z)).ToFColorSRGB();
    }
    constexpr double D = 21.0;
    const FVector N = FVector(Elevation(P - FVector2D(D, 0)) - Elevation(P + FVector2D(D, 0)),
        Elevation(P - FVector2D(0, D)) - Elevation(P + FVector2D(0, D)), 2 * D).GetSafeNormal();
    const double Shade = .68 + .32 * FVector::DotProduct(N, FVector(.56,-.43,.75).GetSafeNormal());
    const double Contour = .83 + .17 * FMath::SmoothStep(.035,.12,FMath::Frac((Z + 27) / 48.0));
    const FLinearColor ElevationTint = FMath::Lerp(FLinearColor(.10,.20,.24), FLinearColor(.78,.86,.93),
        FMath::Pow(FMath::Clamp(Z / 610.0, 0.0, 1.0), .61));
    return (FMath::Lerp(ElevationTint, SurfaceColor(P, Z, N.Z), .58) * static_cast<float>(Shade * Contour)).ToFColorSRGB();
}

void AFrozenBasinTerrain::OnConstruction(const FTransform& Transform)
{
    AActor::OnConstruction(Transform);
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<int32> Triangles;
    Vertices.Reserve(Columns * Rows);
    Normals.Reserve(Columns * Rows);
    UVs.Reserve(Columns * Rows);
    Colors.Reserve(Columns * Rows);
    Triangles.Reserve((Columns - 1) * (Rows - 1) * 6);
    for (int32 Y = 0; Y < Rows; ++Y)
    {
        for (int32 X = 0; X < Columns; ++X)
        {
            const FVector2D P = MeshPoint(X, Y);
            Vertices.Emplace(P * Units, Elevation(P) * Units);
            UVs.Add(P / 8.0);
        }
    }
    for (int32 Y = 0; Y < Rows; ++Y)
    {
        for (int32 X = 0; X < Columns; ++X)
        {
            const int32 I = Y * Columns + X;
            const FVector East = Vertices[Y * Columns + FMath::Min(Columns - 1, X + 1)] - Vertices[Y * Columns + FMath::Max(0, X - 1)];
            const FVector South = Vertices[FMath::Min(Rows - 1, Y + 1) * Columns + X] - Vertices[FMath::Max(0, Y - 1) * Columns + X];
            const FVector N = FVector::CrossProduct(East, South).GetSafeNormal();
            Normals.Add(N);
            Colors.Add(SurfaceColor(MeshPoint(X, Y), Vertices[I].Z / Units, N.Z));
            if (X + 1 < Columns && Y + 1 < Rows)
            {
                Triangles.Append({I, I + Columns, I + 1, I + 1, I + Columns, I + Columns + 1});
            }
        }
    }
    Surface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, {}, true, true);
    Surface->SetMaterial(0, SnowMaterial);
}
