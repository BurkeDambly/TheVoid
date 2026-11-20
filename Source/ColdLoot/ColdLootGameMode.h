#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ColdLootGameMode.generated.h"

class AColdzoneMapTerrain;
class UTexture2D;

UCLASS()
class AColdLootHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    void ToggleIslandMap() { bIslandMapOpen = !bIslandMapOpen; }
    bool IsIslandMapOpen() const { return bIslandMapOpen; }
    void SelectIslandZone(int32 Delta, int32 Count);
    int32 IslandZoneSelection() const { return SelectedZone; }

private:
    void DrawIslandMap(const AColdzoneMapTerrain& Terrain, const FVector& PlayerLocation);
    void BuildIslandMapTexture(const AColdzoneMapTerrain& Terrain);

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
};
