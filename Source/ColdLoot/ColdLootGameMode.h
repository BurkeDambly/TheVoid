#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ColdLootGameMode.generated.h"

class AColdzoneMapTerrain;
class SWidget;
class UTexture2D;

UCLASS()
class AColdLootHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void ToggleGraphicsMenu();
    bool IsGraphicsMenuOpen() const { return GraphicsMenu.IsValid(); }
    void ToggleTuningMenu();
    bool IsTuningMenuOpen() const { return TuningMenu.IsValid(); }
    void ToggleIslandMap() { bIslandMapOpen = !bIslandMapOpen; }
    bool IsIslandMapOpen() const { return bIslandMapOpen; }
    void SelectIslandZone(int32 Delta, int32 Count);
    int32 IslandZoneSelection() const { return SelectedZone; }

private:
    friend class FIcebergNetworkTest;
    void DrawIslandMap(const AColdzoneMapTerrain& Terrain, const FVector& PlayerLocation);
    void BuildIslandMapTexture(const AColdzoneMapTerrain& Terrain);
    void CloseGraphicsMenu();
    void CloseTuningMenu();
    TSharedPtr<SWidget> GraphicsMenu;
    TSharedPtr<SWidget> TuningMenu;
    bool bPausedForGraphics = false;
    bool bCursorBeforeGraphics = false;
    bool bCursorBeforeTuning = false;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> IslandMapTexture;

    bool bIslandMapOpen = false;
    int32 SelectedZone = 0;
};

UCLASS()
class AColdLootGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AColdLootGameMode();
    virtual void RestartPlayer(AController* NewPlayer) override;
};
