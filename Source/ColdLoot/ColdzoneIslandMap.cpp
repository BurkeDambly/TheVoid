#include "ColdLootGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "ColdzoneMapTerrain.h"

void AColdLootHUD::SelectIslandZone(int32 Delta, int32 Count)
{
    SelectedZone = Count > 0 ? (SelectedZone + Delta + Count) % Count : 0;
}

void AColdLootHUD::BuildIslandMapTexture(const AColdzoneMapTerrain& Terrain)
{
    constexpr int32 Resolution = 256;
    IslandMapTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8);
    IslandMapTexture->SRGB = true;
    auto& Mip = IslandMapTexture->GetPlatformData()->Mips[0];
    FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    const FVector2D Minimum = Terrain.MapMinimum();
    const FVector2D Span = Terrain.MapMaximum() - Minimum;
    for (int32 Row = 0; Row < Resolution; ++Row)
    {
        for (int32 Column = 0; Column < Resolution; ++Column)
        {
            const FVector2D Fraction((Row + .5) / Resolution, (Column + .5) / Resolution);
            Pixels[Row * Resolution + Column] = Terrain.MapColorAt(Minimum + Span * Fraction);
        }
    }
    Mip.BulkData.Unlock();
    IslandMapTexture->UpdateResource();
}

void AColdLootHUD::DrawIslandMap(const AColdzoneMapTerrain& Terrain, const FVector& PlayerLocation)
{
    if (!IslandMapTexture)
    {
        BuildIslandMapTexture(Terrain);
    }
    const auto Zones = Terrain.Zones();
    const float Scale = FMath::Min(1.18f, FMath::Min(Canvas->ClipX / 1040.0f, Canvas->ClipY / 690.0f));
    const float MapSide = 545.0f * Scale;
    const float Width = 826 * Scale;
    const float Height = 638 * Scale;
    const float Left = (Canvas->ClipX - Width) * .5f;
    const float Top = (Canvas->ClipY - Height) * .5f;
    const FVector2D MapOrigin(Left + 20 * Scale, Top + 59 * Scale);
    const FVector2D Minimum = Terrain.MapMinimum();
    const FVector2D Span = Terrain.MapMaximum() - Minimum;
    const FLinearColor Ink(.88f,.94f,.98f);
    const FLinearColor Muted(.58f,.70f,.79f);
    const FLinearColor Amber(1.0f,.64f,.28f);
    const FLinearColor Cyan(.14f,.94f,1.0f);
    const FLinearColor Badge(.018f,.039f,.061f,.97f);
    UFont* Font = GEngine->GetSmallFont();

    DrawRect(FLinearColor(.005f,.013f,.022f,.86f), 0, 0, Canvas->ClipX, Canvas->ClipY);
    DrawRect(FLinearColor(.024f,.048f,.068f,.98f), Left, Top, Width, Height);
    DrawText(TEXT("COLDZONE  /  ISLAND MAP"), Ink, Left + 20 * Scale, Top + 15 * Scale, Font, 1.78f * Scale);
    DrawText(TEXT("M  CLOSE"), Muted, Left + 730 * Scale, Top + 24 * Scale, Font, Scale);
    DrawTexture(IslandMapTexture, MapOrigin.X, MapOrigin.Y, MapSide, MapSide, 0, 0, 1, 1, FLinearColor::White, BLEND_Opaque);

    auto Pixel = [&](const FVector2D& Position)
    {
        const FVector2D UV = (Position - Minimum) / Span;
        return MapOrigin + FVector2D(UV.Y, UV.X) * MapSide;
    };
    const FLinearColor TrailEdge(.10f,.12f,.15f);
    const FLinearColor TrailInk(.89f,.72f,.49f);
    for (const auto Route : Terrain.NavigationRoutes())
    {
        for (int32 Index = 1; Index < Route.Num(); ++Index)
        {
            const FVector2D A = Pixel(FVector2D(Route[Index - 1]));
            const FVector2D B = Pixel(FVector2D(Route[Index]));
            DrawLine(A.X, A.Y, B.X, B.Y, TrailEdge, 2.7f * Scale);
            DrawLine(A.X, A.Y, B.X, B.Y, TrailInk, 1.1f * Scale);
        }
    }
    auto Dot = [&](const FVector2D& Center, float Radius, const FLinearColor& Tint)
    {
        constexpr int32 Segments = 16;
        for (int32 Side = 0; Side < Segments; ++Side)
        {
            const double A = 2.0 * PI * Side / Segments;
            const double B = 2.0 * PI * (Side + 1) / Segments;
            DrawLine(Center.X + Radius * FMath::Cos(A), Center.Y + Radius * FMath::Sin(A),
                Center.X + Radius * FMath::Cos(B), Center.Y + Radius * FMath::Sin(B), Tint, 2.0f * Scale);
        }
    };

    const float LegendX = Left + 587 * Scale;
    const FVector2D PlayerPixel = Pixel(Terrain.ToMapPoint(PlayerLocation));
    DrawText(TEXT("LOCATIONS"), Muted, LegendX, Top + 64 * Scale, Font, 1.18f * Scale);
    for (int32 Index = 0; Index < Zones.Num(); ++Index)
    {
        const FVector2D Marker = Pixel(Zones[Index].Center);
        const FVector2D Position = (Marker - PlayerPixel).SizeSquared() < FMath::Square(20 * Scale)
            ? Marker - FVector2D(0, 19 * Scale) : Marker;
        const FString Number = FString::Printf(TEXT("%02d"), Index + 1);
        const bool bSelected = Index == SelectedZone;
        const FLinearColor Highlight = bSelected ? Amber : Ink;
        DrawRect(Badge, Position.X - 12 * Scale, Position.Y - 8 * Scale, 24 * Scale, 16 * Scale);
        DrawText(Number, Highlight, Position.X - 7 * Scale, Position.Y - 7 * Scale, Font, Scale);
        const float Row = Top + (96 + 34 * Index) * Scale;
        if (bSelected)
        {
            DrawRect(FLinearColor(.13f,.15f,.17f), LegendX - 7 * Scale, Row - 5 * Scale, 224 * Scale, 28 * Scale);
        }
        DrawText(Number, Highlight, LegendX, Row, Font, 1.08f * Scale);
        DrawText(Zones[Index].Name, Ink, LegendX + 31 * Scale, Row, Font, 1.10f * Scale);
    }
    Dot(PlayerPixel, 9 * Scale, Badge);
    Dot(PlayerPixel, 7 * Scale, Cyan);
    DrawRect(Cyan, PlayerPixel.X - 2 * Scale, PlayerPixel.Y - 2 * Scale, 4 * Scale, 4 * Scale);
    DrawText(TEXT("N"), Ink, MapOrigin.X + 11 * Scale, MapOrigin.Y + 9 * Scale, Font, 1.2f * Scale);
    DrawText(TEXT("YOU"), Cyan, LegendX, Top + 520 * Scale, Font, 1.12f * Scale);
    DrawText(FString::Printf(TEXT("Elevation %.0f m"), PlayerLocation.Z / 100.0), Muted,
        LegendX + 43 * Scale, Top + 520 * Scale, Font, Scale);
    if (const int32 Routes = Terrain.NavigationRoutes().Num(); Routes > 0)
    {
        DrawLine(LegendX, Top + 548 * Scale, LegendX + 23 * Scale, Top + 548 * Scale, TrailInk, 2 * Scale);
        DrawText(FString::Printf(TEXT("%d graded routes"), Routes), Muted, LegendX + 31 * Scale, Top + 540 * Scale, Font, Scale);
    }
    else
    {
        DrawText(TEXT("Mouse + W  Swim between islands"), Muted, LegendX, Top + 540 * Scale, Font, Scale);
    }
#if WITH_EDITOR
    DrawText(TEXT("UP / DOWN  Select location"), Muted, LegendX, Top + 571 * Scale, Font, Scale);
    DrawText(TEXT("ENTER  Visit     F10  Capture"), Amber, LegendX, Top + 594 * Scale, Font, Scale);
#endif
}
