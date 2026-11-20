#pragma once

#include "GameFramework/SpringArmComponent.h"
#include "PenguinCameraArm.generated.h"

UCLASS()
class UPenguinCameraArm : public USpringArmComponent
{
    GENERATED_BODY()

protected:
    virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag,
        float DeltaSeconds) override;
};
