#include "ColdLootTuning.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
using E = EColdLootTuning;
constexpr TCHAR PresetSection[] = TEXT("ColdLoot.PlaytestTuning");
constexpr FColdLootTuningSetting TuningSettings[] = {
    { E::WalkSpeed, TEXT("WalkSpeed"), TEXT("Walking & sprinting"), TEXT("Walk speed"),
      TEXT("Changes the distance covered by each walk cycle."), 0.10f, 4.00f, 1.0f },
    { E::SprintSpeed, TEXT("SprintSpeed"), TEXT("Walking & sprinting"), TEXT("Sprint speed"),
      TEXT("Changes the distance covered by each sprint cycle."), 0.10f, 4.00f, 1.0f },
    { E::GroundAcceleration, TEXT("GroundAcceleration"), TEXT("Walking & sprinting"), TEXT("Ground acceleration"),
      TEXT("How fast the penguin gets up to walking or sprinting speed."), 0.10f, 4.00f, 1.0f },
    { E::GroundBraking, TEXT("GroundBraking"), TEXT("Walking & sprinting"), TEXT("Ground braking"),
      TEXT("How fast the penguin slows when you release movement."), 0.10f, 4.00f, 1.0f },
    { E::WalkTurn, TEXT("WalkTurn"), TEXT("Walking & sprinting"), TEXT("Walk turn rate"),
      TEXT("Higher values turn more tightly while walking."), 0.10f, 4.00f, 1.0f },
    { E::SprintTurn, TEXT("SprintTurn"), TEXT("Walking & sprinting"), TEXT("Sprint turn rate"),
      TEXT("Higher values turn more tightly while sprinting."), 0.10f, 4.00f, 1.0f },
    { E::TurnLean, TEXT("TurnLean"), TEXT("Walking & sprinting"), TEXT("Turn lean"),
      TEXT("How far the penguin banks into ground and slide turns."), 0.00f, 3.00f, 1.0f },
    { E::BodyJiggle, TEXT("BodyJiggle"), TEXT("Walking & sprinting"), TEXT("Upper-body jiggle"),
      TEXT("How much the torso and head follow through during movement."), 0.00f, 3.00f, 1.0f },
    { E::SlideCruise, TEXT("SlideCruise"), TEXT("Sliding"), TEXT("Slide cruise speed"),
      TEXT("The speed maintained after the initial boost, before friction and slope take over."), 0.10f, 4.00f, 1.0f },
    { E::SlideMaximum, TEXT("SlideMaximum"), TEXT("Sliding"), TEXT("Slide speed limit"),
      TEXT("Caps slide speed, including downhill acceleration and landing speed."), 0.10f, 4.00f, 1.0f },
    { E::SlideStandingBoost, TEXT("SlideStandingBoost"), TEXT("Sliding"), TEXT("Standing slide boost"),
      TEXT("Initial speed when you slide from rest."), 0.00f, 4.00f, 1.0f },
    { E::SlideWalkingBoost, TEXT("SlideWalkingBoost"), TEXT("Sliding"), TEXT("Walking slide boost"),
      TEXT("Speed added when you slide while moving."), 0.00f, 4.00f, 1.0f },
    { E::SlideSprintBoost, TEXT("SlideSprintBoost"), TEXT("Sliding"), TEXT("Sprint slide boost"),
      TEXT("Speed added when you slide out of a sprint."), 0.00f, 4.00f, 1.0f },
    { E::SlideFriction, TEXT("SlideFriction"), TEXT("Sliding"), TEXT("Slow slide friction"),
      TEXT("Deceleration from the ice at low slide speeds."), 0.00f, 5.00f, 1.0f },
    { E::SlideFastFriction, TEXT("SlideFastFriction"), TEXT("Sliding"), TEXT("Fast slide friction"),
      TEXT("Deceleration from the ice at high slide speeds."), 0.00f, 5.00f, 1.0f },
    { E::SlideSlope, TEXT("SlideSlope"), TEXT("Sliding"), TEXT("Slope influence"),
      TEXT("How much a slope accelerates downhill or brakes uphill."), 0.00f, 4.00f, 1.0f },
    { E::SlideSteering, TEXT("SlideSteering"), TEXT("Sliding"), TEXT("Slide steering"),
      TEXT("How fast A and D rotate a sliding penguin."), 0.00f, 5.00f, 1.0f },
    { E::SlideBrake, TEXT("SlideBrake"), TEXT("Sliding"), TEXT("Slide braking"),
      TEXT("How much the brake input slows a slide."), 0.00f, 4.00f, 1.0f },
    { E::SlideBoostDecay, TEXT("SlideBoostDecay"), TEXT("Sliding"), TEXT("Boost braking"),
      TEXT("How quickly a boost settles toward cruise speed."), 0.00f, 4.00f, 1.0f },
    { E::SlideGroundLock, TEXT("SlideGroundLock"), TEXT("Sliding"), TEXT("Ground-lock duration"),
      TEXT("How long the penguin stays on the ground after starting a slide."), 0.00f, 4.00f, 1.0f },
    { E::Gravity, TEXT("Gravity"), TEXT("Jumping & flight"), TEXT("Air gravity"),
      TEXT("Changes fall speed and how readily a slide lifts off a crest."), 0.05f, 4.00f, 1.0f },
    { E::Jump, TEXT("Jump"), TEXT("Jumping & flight"), TEXT("Jump lift"),
      TEXT("Upward push for a normal jump."), 0.00f, 4.00f, 1.0f },
    { E::SlideJump, TEXT("SlideJump"), TEXT("Jumping & flight"), TEXT("Slide jump lift"),
      TEXT("Upward push when jumping out of a slide."), 0.00f, 4.00f, 1.0f },
    { E::AirSpeed, TEXT("AirSpeed"), TEXT("Jumping & flight"), TEXT("Air control speed"),
      TEXT("Minimum target speed when steering in the air."), 0.00f, 4.00f, 1.0f },
    { E::AirSteering, TEXT("AirSteering"), TEXT("Jumping & flight"), TEXT("Air steering"),
      TEXT("How strongly movement input changes your course in the air."), 0.00f, 5.00f, 1.0f },
    { E::DiveRate, TEXT("DiveRate"), TEXT("Jumping & flight"), TEXT("Dive pitch rate"),
      TEXT("How fast C turns the penguin nose-down in the air."), 0.00f, 5.00f, 1.0f },
    { E::CrestLift, TEXT("CrestLift"), TEXT("Jumping & flight"), TEXT("Crest launch lift"),
      TEXT("Extra upward push when a fast slide leaves the ground at a crest."), 0.00f, 4.00f, 1.0f },
    { E::SwimSpeed, TEXT("SwimSpeed"), TEXT("Swimming"), TEXT("Swim speed"),
      TEXT("Scales paddle thrust and the speed-dependent drag together."), 0.10f, 4.00f, 1.0f },
    { E::SwimSteering, TEXT("SwimSteering"), TEXT("Swimming"), TEXT("Swim steering"),
      TEXT("How fast the penguin turns toward the direction you look."), 0.05f, 4.00f, 1.0f },
    { E::WaterEntryHold, TEXT("WaterEntryHold"), TEXT("Swimming"), TEXT("Water-entry pose duration"),
      TEXT("How long the entry pose is held before the swim animation takes over."), 0.00f, 5.00f, 1.0f },
    { E::WaterEntryDrag, TEXT("WaterEntryDrag"), TEXT("Swimming"), TEXT("Water-entry drag"),
      TEXT("How much forward momentum water removes on entry."), 0.00f, 4.00f, 1.0f },
    { E::WaterDrag, TEXT("WaterDrag"), TEXT("Swimming"), TEXT("Swimming drag"),
      TEXT("How quickly water slows the penguin after entry."), 0.00f, 4.00f, 1.0f },
    { E::Sink, TEXT("Sink"), TEXT("Swimming"), TEXT("Sink rate"),
      TEXT("How strongly the penguin sinks without enough lift from paddling."), 0.00f, 4.00f, 1.0f },
    { E::SurfaceLift, TEXT("SurfaceLift"), TEXT("Swimming"), TEXT("Surface paddle lift"),
      TEXT("How strongly a level stroke holds the penguin near the waterline."), 0.00f, 4.00f, 1.0f },
    { E::BreachSpeed, TEXT("BreachSpeed"), TEXT("Swimming"), TEXT("Breach speed threshold"),
      TEXT("Minimum overall swim speed needed to break out of the water."), 0.10f, 4.00f, 1.0f },
    { E::BreachRise, TEXT("BreachRise"), TEXT("Swimming"), TEXT("Breach upward threshold"),
      TEXT("Minimum upward swim speed needed to break out of the water."), 0.10f, 4.00f, 1.0f },
    { E::SkipCount, TEXT("SkipCount"), TEXT("Swimming"), TEXT("Maximum water skips"),
      TEXT("Number of bounces allowed on a shallow water landing."), 0.00f, 6.00f, 2.0f },
    { E::SkipSpeed, TEXT("SkipSpeed"), TEXT("Swimming"), TEXT("Water skip speed threshold"),
      TEXT("Minimum forward speed required to skip over the water."), 0.10f, 4.00f, 1.0f },
    { E::SkipAngle, TEXT("SkipAngle"), TEXT("Swimming"), TEXT("Water skip impact angle"),
      TEXT("How steeply you can hit the water and still skip."), 0.00f, 3.00f, 1.0f },
    { E::SkipBounce, TEXT("SkipBounce"), TEXT("Swimming"), TEXT("Water skip bounce"),
      TEXT("How high each water skip rebounds."), 0.10f, 4.00f, 1.0f },
    { E::BulletSpeed, TEXT("BulletSpeed"), TEXT("Gunplay"), TEXT("Bullet speed"),
      TEXT("Changes projectile travel speed relative to this mode's default."), 0.10f, 5.00f, 1.0f },
    { E::FireRate, TEXT("FireRate"), TEXT("Gunplay"), TEXT("Fire rate"),
      TEXT("Scales shots per second up to the weapon's 30-shot safety limit."), 0.07f, 4.00f, 1.0f },
    { E::TargetDamage, TEXT("TargetDamage"), TEXT("Gunplay"), TEXT("Damage to practice targets"),
      TEXT("Changes reported damage and impulse on practice props."), 0.00f, 5.00f, 1.0f },
    { E::PlayerDamage, TEXT("PlayerDamage"), TEXT("Gunplay"), TEXT("Damage per hit on penguins"),
      TEXT("Changes health lost per registered bullet. The default is 20 HP."), 0.00f, 5.00f, 1.0f },
    { E::Knockback, TEXT("Knockback"), TEXT("Gunplay"), TEXT("Knockout launch"),
      TEXT("Scales the impulse on the hit that starts a ragdoll."), 0.00f, 4.00f, 1.0f },
    { E::Recoil, TEXT("Recoil"), TEXT("Gunplay"), TEXT("Recoil"),
      TEXT("Scales the aim climb, weave, and camera kick."), 0.00f, 4.00f, 1.0f },
    { E::SprintSpread, TEXT("SprintSpread"), TEXT("Gunplay"), TEXT("Sprint-fire spread"),
      TEXT("Changes how widely shots spread while sprinting."), 0.00f, 4.00f, 1.0f },
    { E::Ricochet, TEXT("Ricochet"), TEXT("Gunplay"), TEXT("Bullet ricochet"),
      TEXT("Changes bounce strength. Set it to zero to turn bounces off."), 0.00f, 2.50f, 1.0f },
    { E::GunfireVolume, TEXT("GunfireVolume"), TEXT("Gunplay"), TEXT("Gunfire volume"),
      TEXT("Playback volume of your own shots."), 0.00f, 2.00f, 1.0f },
    { E::HipCamera, TEXT("HipCamera"), TEXT("Camera"), TEXT("Hip camera distance"),
      TEXT("Changes how far the camera follows behind when you are not aiming."), 0.30f, 2.00f, 1.0f },
    { E::ADSCamera, TEXT("ADSCamera"), TEXT("Camera"), TEXT("Aim camera distance"),
      TEXT("Changes how far back the camera sits while aiming."), 0.30f, 2.00f, 1.0f },
    { E::ShoulderOffset, TEXT("ShoulderOffset"), TEXT("Camera"), TEXT("Shoulder offset"),
      TEXT("Changes how far the camera sits to the side."), 0.00f, 2.00f, 1.0f },
    { E::AimFOV, TEXT("AimFOV"), TEXT("Camera"), TEXT("Aim field of view"),
      TEXT("Scales the field of view while aiming down sights."), 0.40f, 1.50f, 1.0f },
};
static_assert(UE_ARRAY_COUNT(TuningSettings) == static_cast<int32>(E::Count));
}

FColdLootTuning::FColdLootTuning()
{
    Values.SetNum(static_cast<int32>(E::Count));
    for (const FColdLootTuningSetting& Setting : TuningSettings)
    {
        Values[static_cast<int32>(Setting.Id)] = Setting.Default;
    }
}

float FColdLootTuning::Get(EColdLootTuning Id) const
{
    return Values[static_cast<int32>(Id)];
}

TConstArrayView<FColdLootTuningSetting> FColdLootTuning::Settings()
{
    return MakeArrayView(TuningSettings);
}

const FColdLootTuning& FColdLootTuning::ForWorld(const UWorld* World)
{
    static const FColdLootTuning Defaults;
    const AColdLootGameState* State = World ? World->GetGameState<AColdLootGameState>() : nullptr;
    return State ? State->GetTuning() : Defaults;
}

void AColdLootGameState::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority() || !GConfig || FParse::Param(FCommandLine::Get(), TEXT("ColdLootIgnoreTuningPreset")))
    {
        return;
    }

    for (const FColdLootTuningSetting& Setting : TuningSettings)
    {
        float Value = Setting.Default;
        if (GConfig->GetFloat(PresetSection, Setting.Key, Value, GGameUserSettingsIni))
        {
            SetTuning(Setting.Id, Value);
        }
    }
}

void AColdLootGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AColdLootGameState, Tuning);
}

bool AColdLootGameState::SetTuning(EColdLootTuning Id, float Value)
{
    const int32 Index = static_cast<int32>(Id);
    if (!HasAuthority() || !Tuning.Values.IsValidIndex(Index) || !FMath::IsFinite(Value))
    {
        return false;
    }

    const FColdLootTuningSetting& Setting = TuningSettings[Index];
    Value = FMath::Clamp(Value, Setting.Minimum, Setting.Maximum);
    if (Id == E::SkipCount)
    {
        Value = FMath::RoundToFloat(Value);
    }
    if (Tuning.Values[Index] != Value)
    {
        Tuning.Values[Index] = Value;
        ForceNetUpdate();
    }
    return true;
}

void AColdLootGameState::ResetTuning()
{
    if (HasAuthority())
    {
        Tuning = FColdLootTuning();
        ForceNetUpdate();
    }
}

bool AColdLootGameState::SaveTuningPreset() const
{
    if (!HasAuthority() || !GConfig)
    {
        return false;
    }
    for (const FColdLootTuningSetting& Setting : TuningSettings)
    {
        GConfig->SetFloat(PresetSection, Setting.Key, Tuning.Get(Setting.Id), GGameUserSettingsIni);
    }
    GConfig->Flush(false, GGameUserSettingsIni);
    return true;
}
