#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ColdLootTuning.generated.h"

enum class EColdLootTuning : uint8
{
    WalkSpeed,
    SprintSpeed,
    GroundAcceleration,
    GroundBraking,
    WalkTurn,
    SprintTurn,
    TurnLean,
    BodyJiggle,
    SlideCruise,
    SlideMaximum,
    SlideStandingBoost,
    SlideWalkingBoost,
    SlideSprintBoost,
    SlideFriction,
    SlideFastFriction,
    SlideSlope,
    SlideSteering,
    SlideBrake,
    SlideBoostDecay,
    SlideGroundLock,
    Gravity,
    Jump,
    SlideJump,
    AirSpeed,
    AirSteering,
    DiveRate,
    CrestLift,
    SwimSpeed,
    SwimSteering,
    WaterEntryHold,
    WaterEntryDrag,
    WaterDrag,
    Sink,
    SurfaceLift,
    BreachSpeed,
    BreachRise,
    SkipCount,
    SkipSpeed,
    SkipAngle,
    SkipBounce,
    BulletSpeed,
    FireRate,
    TargetDamage,
    PlayerDamage,
    Knockback,
    Recoil,
    SprintSpread,
    Ricochet,
    GunfireVolume,
    HipCamera,
    ADSCamera,
    ShoulderOffset,
    AimFOV,
    Count
};

struct FColdLootTuningSetting
{
    EColdLootTuning Id;
    const TCHAR* Key;
    const TCHAR* Group;
    const TCHAR* Label;
    const TCHAR* Description;
    float Minimum;
    float Maximum;
    float Default;
};

USTRUCT()
struct FColdLootTuning
{
    GENERATED_BODY()

    FColdLootTuning();

    float Get(EColdLootTuning Id) const;
    static TConstArrayView<FColdLootTuningSetting> Settings();
    static const FColdLootTuning& ForWorld(const UWorld* World);

private:
    friend class AColdLootGameState;

    UPROPERTY()
    TArray<float> Values;
};

UCLASS()
class AColdLootGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    const FColdLootTuning& GetTuning() const { return Tuning; }
    bool SetTuning(EColdLootTuning Id, float Value);
    void ResetTuning();
    bool SaveTuningPreset() const;

private:
    UPROPERTY(Replicated)
    FColdLootTuning Tuning;
};
