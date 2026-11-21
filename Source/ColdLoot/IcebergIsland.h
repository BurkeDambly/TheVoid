#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IcebergIsland.generated.h"

class UProceduralMeshComponent;

UCLASS()
class AIcebergIsland : public AActor
{
    GENERATED_BODY()

public:
    AIcebergIsland();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostInitializeComponents() override;


    UFUNCTION(BlueprintPure, Category = "Coldzone Island")
    double HeightAt(double WorldX, double WorldY) const;

    FColor ColorAt(double WorldX, double WorldY) const;
    TOptional<FVector> PracticeRampLip() const;

    UFUNCTION(BlueprintCallable, Category = "Coldzone Island")
    bool ExportBlenderMesh(const FString& Filename) const;

    UFUNCTION(BlueprintCallable, Category = "Coldzone Island")
    void RebuildSurface();

    UPROPERTY(EditAnywhere, Category = "Coldzone Island")
    FString IslandName = TEXT("Expedition Island");

    UPROPERTY(EditAnywhere, Category = "Coldzone Island")
    bool bBaseIsland = false;

    UPROPERTY(EditAnywhere, Category = "Coldzone Island")
    TObjectPtr<UMaterialInterface> IslandMaterial;

    UPROPERTY(EditAnywhere, Category = "Coldzone Island")
    bool bShowPracticeRamp = true;

private:
    double TerrainHeightAt(const FVector2D& Point) const;
    void RebuildPracticeRamp();
    double Elevation(const FVector2D& Position) const;
    FLinearColor IceColor(const FVector2D& Position, double Height, double Up) const;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> Surface;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> PracticeRamp;
};
