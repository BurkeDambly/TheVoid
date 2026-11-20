#pragma once

#include "Animation/AnimInstance.h"
#include "PenguinAnimInstance.generated.h"

UCLASS()
class UPenguinAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
