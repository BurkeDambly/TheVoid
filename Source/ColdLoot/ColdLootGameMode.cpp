#include "ColdLootGameMode.h"

#include "PenguinPawn.h"
#include "ColdzoneMapTerrain.h"
#include "ColdLootTuning.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

namespace
{
void DrawHealthBar(AHUD& HUD, const APenguinPawn& Penguin, float X, float Y, float Width, float Height)
{
    const float Fraction = static_cast<float>(Penguin.GetHealth()) / APenguinPawn::MaxHealth;
    HUD.DrawRect(FLinearColor(0.018f, 0.047f, 0.082f, 0.94f), X, Y, Width, Height);
    const FLinearColor Fill = FLinearColor::LerpUsingHSV(FLinearColor(1.0f, 0.22f, 0.12f),
        FLinearColor(0.30f, 0.92f, 0.71f), Fraction);
    if (Fraction > 0.0f)
    {
        HUD.DrawRect(Fill, X + 2, Y + 2, (Width - 4) * Fraction, Height - 4);
    }
}
}

AColdLootGameMode::AColdLootGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = AColdLootHUD::StaticClass();
    GameStateClass = AColdLootGameState::StaticClass();
}

void AColdLootGameMode::RestartPlayer(AController* NewPlayer)
{
    if (GetNetMode() == NM_Standalone || NewPlayer->GetPawn())
    {
        Super::RestartPlayer(NewPlayer);
        return;
    }

    for (TActorIterator<APenguinPawn> It(GetWorld()); It; ++It)
    {
        if (It->IsNetStartupActor())
        {
            FVector SpawnLocation = It->GetActorLocation();
            SpawnLocation.Y += 225.0 * FMath::Max(0, GetNumPlayers() - 1);
            DefaultPawnClass = APenguinPawn::StaticClass();
            RestartPlayerAtTransform(NewPlayer, FTransform(It->GetActorRotation(), SpawnLocation));
            return;
        }
    }
}

void AColdLootHUD::DrawHUD()
{
    Super::DrawHUD();

    const APenguinPawn* Penguin = Cast<APenguinPawn>(GetOwningPawn());
    if (!Penguin)
    {
        return;
    }
    if (IsGraphicsMenuOpen() || IsTuningMenuOpen())
    {
        return;
    }

    if (FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename().Contains(TEXT("Coldzone_ZoneReference")))
    {
        return;
    }
    const AColdzoneMapTerrain* Island = Penguin->IslandTerrain();
    if (Island && bIslandMapOpen)
    {
        DrawIslandMap(*Island, Penguin->GetActorLocation());
        return;
    }

    const FLinearColor Ice(0.84f, 0.95f, 1.0f);
    const FLinearColor Accent(0.37f, 0.87f, 1.0f);
    const bool bIcebergNetwork = UGameplayStatics::GetCurrentLevelName(this) == TEXT("IcebergNetwork");
    const TCHAR* LevelTitle = bIcebergNetwork ? TEXT("COLDZONE  /  ICEBERG NETWORK")
        : Island ? TEXT("COLDZONE  /  FROZEN BASIN") : TEXT("COLD LOOT  /  SLIDE PARK");
    DrawRect(FLinearColor(0.018f, 0.047f, 0.082f, 0.90f), 24, 24, 330, 142);
    DrawRect(Accent, 24, 24, 3, 142);
    DrawText(LevelTitle, Accent, 41, 34, nullptr, 1.15f);
    DrawText(FString::Printf(TEXT("%s     %.1f m/s     %+d deg"), Penguin->MotionLabel(),
        Penguin->Speed() / 100.0f, FMath::RoundToInt(Penguin->Grade())), Ice, 41, 62, nullptr, 1.2f);
    DrawText(Penguin->IsSwimming() ? FString::Printf(TEXT("Depth   %.1f m"), Penguin->GetSwimDepth() / 100.0)
        : FString::Printf(TEXT("Slide   %.1f m     Peak   %.1f m/s"),
            Penguin->SlideDistance() / 100.0f, Penguin->SlidePeakSpeed() / 100.0f), Ice, 41, 96);

    const float ControlsX = FMath::Max(24.0f, Canvas->ClipX - 459.0f);
    DrawRect(FLinearColor(0.018f, 0.047f, 0.082f, 0.90f), ControlsX, Canvas->ClipY - 72, 435, 48);
    DrawText(Penguin->IsSwimming() ? TEXT("Mouse Steer   /   W Paddle   /   Swim Up Fast to Breach")
        : TEXT("WASD Move   /   Shift Sprint   /   C Slide or Dive"),
        Ice, ControlsX + 14, Canvas->ClipY - 66);
    DrawText(bIcebergNetwork ? TEXT("Space Jump   /   R Return to Base")
        : Island ? TEXT("Space Jump   /   R Return to Summit")
        : TEXT("Space Jump   /   1 Flat   /   2 Downhill   /   3 Uphill   /   4 Ramp"),
        Accent, ControlsX + 14, Canvas->ClipY - 47);
    if (Island)
    {
        DrawText(TEXT("M Island Map"), Ice, ControlsX + 317, Canvas->ClipY - 47);
    }

    if (Penguin->HasWeapon())
    {
        const FVector2D Center(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);
        const FLinearColor Reticle = Penguin->CanFire() ? Ice : FLinearColor(Ice.R, Ice.G, Ice.B, 0.4f);
        const float Gap = Penguin->ReticleGap();
        for (const FVector2D Direction : {FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)})
        {
            const FVector2D Inner = Center + Direction * Gap;
            const FVector2D Outer = Inner + Direction * 6.0f;
            DrawLine(Inner.X, Inner.Y, Outer.X, Outer.Y, FLinearColor::Black, 3.0f);
            DrawLine(Inner.X, Inner.Y, Outer.X, Outer.Y, Reticle, 1.4f);
        }
    }
    DrawText(TEXT("LMB Fire   /   RMB Aim"), Ice, 41, 116);
    DrawText(TEXT("F2 Graphics   /   F3 Tuning"), Ice, 41, 141);

    const float HealthY = Canvas->ClipY - 55.0f;
    DrawHealthBar(*this, *Penguin, 24, HealthY, 156, 14);
    DrawText(Penguin->IsKnockedOut() ? TEXT("Knocked out  /  R to respawn")
        : FString::Printf(TEXT("%d / %d HP"), Penguin->GetHealth(), APenguinPawn::MaxHealth),
        Ice, 25, HealthY + 17);

    const APlayerController* Player = GetOwningPlayerController();
    FVector CameraPosition;
    FRotator CameraRotation;
    Player->GetPlayerViewPoint(CameraPosition, CameraRotation);
    FCollisionQueryParams Visibility(SCENE_QUERY_STAT(PenguinHealthBar), true, Penguin);
    for (TActorIterator<APenguinPawn> It(GetWorld()); It; ++It)
    {
        const APenguinPawn& Other = **It;
        if (&Other == Penguin || Other.IsHidden() || Other.IsKnockedOut())
        {
            continue;
        }
        const FVector Label = Other.PenguinMesh->Bounds.Origin
            + FVector(0, 0, Other.PenguinMesh->Bounds.BoxExtent.Z + 18.0);
        FVector2D Screen;
        if (FVector::DistSquared(CameraPosition, Label) <= FMath::Square(3500.0)
            && Player->ProjectWorldLocationToScreen(Label, Screen, true)
            && !GetWorld()->LineTraceTestByChannel(CameraPosition, Label, ECC_Visibility, Visibility))
        {
            DrawHealthBar(*this, Other, Screen.X - 30.0f, Screen.Y - 7.0f, 60.0f, 7.0f);
        }
    }
}
