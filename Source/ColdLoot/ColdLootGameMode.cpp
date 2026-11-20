#include "ColdLootGameMode.h"

#include "PenguinPawn.h"
#include "ColdzoneMapTerrain.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

AColdLootGameMode::AColdLootGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = AColdLootHUD::StaticClass();
}

void AColdLootHUD::DrawHUD()
{
    Super::DrawHUD();

    const APenguinPawn* Penguin = Cast<APenguinPawn>(GetOwningPawn());
    if (!Penguin)
    {
        return;
    }
    if (IsGraphicsMenuOpen())
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
    const bool bEditorForm = Penguin->IsEditorPenguinFormActive();
    const FLinearColor Accent = bEditorForm ? FLinearColor(1.0f, 0.37f, 0.33f) : FLinearColor(0.37f, 0.87f, 1.0f);
    const bool bIcebergNetwork = UGameplayStatics::GetCurrentLevelName(this) == TEXT("IcebergNetwork");
    const TCHAR* LevelTitle = bIcebergNetwork ? TEXT("COLDZONE  /  ICEBERG NETWORK")
        : Island ? TEXT("COLDZONE  /  FROZEN BASIN") : TEXT("COLD LOOT  /  SLIDE PARK");
    DrawRect(FLinearColor(0.018f, 0.047f, 0.082f, 0.90f), 24, 24, 330, 142);
    DrawRect(Accent, 24, 24, 3, 142);
    const TCHAR* FormTitle = bEditorForm ? TEXT("EDITOR PENGUIN FORM") : TEXT("PENGUIN FORM ACTIVE");
    DrawText(Penguin->IsPenguinFormActive() ? FormTitle : LevelTitle, Accent, 41, 34, nullptr, 1.15f);
    DrawText(FString::Printf(TEXT("%s     %.1f m/s     %+d deg"), Penguin->MotionLabel(),
        Penguin->Speed() / 100.0f, FMath::RoundToInt(Penguin->Grade())), Ice, 41, 62, nullptr, 1.2f);
    DrawText(Penguin->IsSwimming() ? FString::Printf(TEXT("Depth   %.1f m"), Penguin->GetSwimDepth() / 100.0)
        : FString::Printf(TEXT("Slide   %.1f m     Peak   %.1f m/s"),
            Penguin->SlideDistance() / 100.0f, Penguin->SlidePeakSpeed() / 100.0f), Ice, 41, 96);

    const float ControlsX = FMath::Max(24.0f, Canvas->ClipX - 459.0f);
    DrawRect(FLinearColor(0.018f, 0.047f, 0.082f, 0.90f), ControlsX, Canvas->ClipY - 72, 435, 48);
    DrawText(Penguin->IsSwimming() ? TEXT("Mouse Steer   /   W Paddle   /   Swim Up Fast to Breach")
        : TEXT("WASD Move   /   Shift Sprint   /   C Slide or stand   /   Mouse Look"),
        Ice, ControlsX + 14, Canvas->ClipY - 66);
    DrawText(bIcebergNetwork ? TEXT("Q Penguin Form   /   R Return to Base")
        : Island ? TEXT("Q Penguin Form   /   R Return to Summit")
        : TEXT("Q Penguin Form   /   1 Flat   /   2 Downhill   /   3 Uphill   /   4 Ramp"),
        Accent, ControlsX + 14, Canvas->ClipY - 47);
    if (Island)
    {
        DrawText(TEXT("M Island Map"), Ice, ControlsX + 317, Canvas->ClipY - 47);
    }

    if (Penguin->HasWeapon())
    {
        FVector2D Center(Canvas->ClipX * 0.5, Canvas->ClipY * 0.5);
        const FLinearColor Reticle = Penguin->CanFire() ? Ice : FLinearColor(Ice.R, Ice.G, Ice.B, 0.4f);
        const float Gap = Penguin->ReticleGap();
        if (!Penguin->CanFire() || PlayerOwner->ProjectWorldLocationToScreen(Penguin->GetBoreReticlePoint(), Center, true))
        {
            for (const FVector2D Direction : {FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)})
            {
                const FVector2D Inner = Center + Direction * Gap;
                const FVector2D Outer = Inner + Direction * 6.0f;
                DrawLine(Inner.X, Inner.Y, Outer.X, Outer.Y, FLinearColor::Black, 3.0f);
                DrawLine(Inner.X, Inner.Y, Outer.X, Outer.Y, Reticle, 1.4f);
            }
        }
    }
    DrawText(TEXT("LMB Fire   /   RMB Aim   /   E Red Form"), Ice, 41, 116);
    DrawText(TEXT("F2 Graphics"), Ice, 41, 141);
}
