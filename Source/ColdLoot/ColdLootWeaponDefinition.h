#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ColdLootWeaponDefinition.generated.h"

class UAnimSequence;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;

UCLASS()
class UColdLootWeaponDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Weapon")
    TObjectPtr<UStaticMesh> Receiver;

    UPROPERTY(EditAnywhere, Category="Weapon")
    TObjectPtr<UStaticMesh> Bolt;

    UPROPERTY(EditAnywhere, Category="Weapon")
    TObjectPtr<UStaticMesh> Drum;

    UPROPERTY(EditAnywhere, Category="Weapon")
    TObjectPtr<UAnimSequence> HoldPose;

    UPROPERTY(EditAnywhere, Category="Weapon")
    TObjectPtr<UMaterialInterface> TracerMaterial;

    UPROPERTY(EditAnywhere, Category="Weapon")
    TSoftObjectPtr<USoundBase> FiringSound = TSoftObjectPtr<USoundBase>(
        FSoftObjectPath(TEXT("/Game/Weapons/Starter/Audio/S_StarterSuppressed.S_StarterSuppressed")));

    UPROPERTY(EditAnywhere, Category="Fit")
    FTransform GripOnFlipper;

    // Fitted poses can author the grip-side offset and the resting flipper.
    UPROPERTY(EditAnywhere, Category="Fit")
    bool bUseFittedFlipperTransforms = false;

    UPROPERTY(EditAnywhere, Category="Fit", meta=(EditCondition="bUseFittedFlipperTransforms"))
    FVector HoldHingeOnFlipper = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="Fit")
    FTransform ReceiverOnGrip;

    UPROPERTY(EditAnywhere, Category="Fit")
    FTransform BoltOnReceiver;

    UPROPERTY(EditAnywhere, Category="Fit")
    FTransform DrumOnReceiver;

    UPROPERTY(EditAnywhere, Category="Tuning", meta=(ClampMin="1", ClampMax="30"))
    float RoundsPerSecond = 15.0f;

    UPROPERTY(EditAnywhere, Category="Tuning", meta=(ClampMin="1", Units="cm/s"))
    float BulletSpeed = 14400.0f;

    UPROPERTY(EditAnywhere, Category="Tuning", meta=(ClampMin="0"))
    float Damage = 20.0f;
};
