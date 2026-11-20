#include "PenguinCameraArm.h"
#include "PenguinPawn.h"

void UPenguinCameraArm::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag,
    float DeltaSeconds)
{
    const APenguinPawn* Penguin = Cast<APenguinPawn>(GetOwner());
    const bool bDirectAim = Penguin && Penguin->CanFire();
    Super::UpdateDesiredArmLocation(bDoTrace, bDoLocationLag && !bDirectAim, bDoRotationLag && !bDirectAim,
        DeltaSeconds);
}
