#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "../PenguinPawn.h"
#include "../SlideParkTerrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenguinMovementTest, "ColdLoot.SlidePark.Movement",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPenguinMovementTest::RunTest(const FString& Parameters)
{
    UWorld* GameWorld = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
        {
            GameWorld = Context.World();
            break;
        }
    }
    if (!TestNotNull(TEXT("Slide Park running in game or PIE"), GameWorld))
    {
        return false;
    }

    TActorIterator<APenguinPawn> It(GameWorld);
    if (!TestTrue(TEXT("Penguin and terrain are initialized"), It && It->Terrain))
    {
        return false;
    }
    APenguinPawn& Penguin = **It;
    if (!TestTrue(TEXT("Penguin is possessed and has a mesh and all three clips"),
        Penguin.IsPlayerControlled() && Penguin.PenguinMesh->GetSkeletalMeshAsset()
        && Penguin.WalkAnimation && Penguin.SprintAnimation && Penguin.SlideAnimation))
    {
        return false;
    }

    Penguin.PenguinMesh->RefreshBoneTransforms();
    const FVector Head = Penguin.PenguinMesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component).GetLocation();
    TestTrue(TEXT("Animated rig is in Unreal centimeters"), Head.Size() > 40.0 && Head.Size() < 170.0);

    constexpr float StepSeconds = 1.0f / 120.0f;
    TestEqual(TEXT("Placed camera boom length in centimeters"), Penguin.CameraArm->TargetArmLength, 508.2f * 1.10f);

    TArray<FInputAxisKeyMapping> PitchMappings;
    GetDefault<UInputSettings>()->GetAxisMappingByName(TEXT("LookPitch"), PitchMappings);
    TestTrue(TEXT("Vertical mouse direction is reversed from the original mapping"),
        PitchMappings.Num() == 1 && PitchMappings[0].Key == EKeys::MouseY && PitchMappings[0].Scale == 1.0f);

    for (bool bSprint : {false, true})
    {
        Penguin.ResetFlat();
        Penguin.bSprintHeld = bSprint;
        for (int32 I = 0; I < 55; ++I)
        {
            Penguin.Step(StepSeconds, FVector::ForwardVector);
        }
        const float Cycle = (bSprint ? Penguin.SprintAnimation : Penguin.WalkAnimation)->GetPlayLength();
        const float Distance = bSprint ? Penguin.SprintDistancePerCycle : Penguin.WalkDistancePerCycle;
        TestTrue(bSprint ? TEXT("Sprint distance per cycle") : TEXT("Walk distance per cycle"),
            FMath::IsNearlyEqual(Penguin.Speed() * Cycle, Distance, 1.0f));
        const float NextPhase = FMath::Fmod(Penguin.GaitPhase + StepSeconds / Cycle, 1.0f);
        Penguin.Step(StepSeconds, FVector::ForwardVector);
        TestTrue(TEXT("Full-speed animation advances at the authored cadence"),
            FMath::IsNearlyEqual(Penguin.GaitPhase, NextPhase));
    }

    Penguin.ResetFlat();
    Penguin.bSprintHeld = false;
    for (int32 I = 0; I < 23; ++I)
    {
        Penguin.Step(StepSeconds, FVector::ForwardVector);
    }
    USkeletalMeshComponent& Mesh = *Penguin.PenguinMesh;
    auto EvaluatePose = [&Mesh]()
    {
        Mesh.TickAnimation(StepSeconds, false);
        Mesh.RefreshBoneTransforms();
    };
    auto Bone = [&Mesh](FName Name)
    {
        return Mesh.GetSocketTransform(Name, RTS_Component);
    };
    EvaluatePose();
    const FVector WalkFoot = Bone(TEXT("Foot_L")).GetLocation();
    const float WalkPhase = Penguin.GaitPhase;
    Penguin.bSprintHeld = true;
    Penguin.Step(StepSeconds, FVector::ForwardVector);
    EvaluatePose();
    TestTrue(TEXT("Sprint entry retains the walking stride phase"),
        Penguin.GaitPhase > WalkPhase && Penguin.SprintBlend > 0.0f && Penguin.SprintBlend < 0.10f);
    TestTrue(TEXT("Sprint entry has no abrupt foot jump"),
        FVector::Distance(WalkFoot, Bone(TEXT("Foot_L")).GetLocation()) < 3.0);
    for (int32 I = 0; I < 40; ++I)
    {
        Penguin.Step(StepSeconds, FVector::ForwardVector);
    }
    EvaluatePose();
    const FVector SprintFoot = Bone(TEXT("Foot_L")).GetLocation();
    const float SprintPhase = Penguin.GaitPhase;
    Penguin.bSprintHeld = false;
    Penguin.Step(StepSeconds, FVector::ForwardVector);
    EvaluatePose();
    TestTrue(TEXT("Sprint exit advances the same phase into the slower gait"),
        Penguin.GaitPhase > SprintPhase && Penguin.SprintBlend > 0.90f && Penguin.SprintBlend < 1.0f);
    TestTrue(TEXT("Sprint exit has no abrupt foot jump"),
        FVector::Distance(SprintFoot, Bone(TEXT("Foot_L")).GetLocation()) < 7.0);

    Penguin.GaitPhase = 0.28f;
    const float SavedJiggle = Penguin.UpperBodyJiggle;
    Penguin.UpperBodyJiggle = 0.0f;
    EvaluatePose();
    const FQuat BaseBody = Bone(TEXT("Body_Jiggle")).GetRotation();
    const FTransform BaseFoot = Bone(TEXT("Foot_L"));
    Penguin.UpperBodyJiggle = 1.0f;
    EvaluatePose();
    TestTrue(TEXT("Jiggle visibly rotates the torso while leaving the foot pose alone"),
        BaseBody.AngularDistance(Bone(TEXT("Body_Jiggle")).GetRotation()) > FMath::DegreesToRadians(0.8)
        && BaseFoot.Equals(Bone(TEXT("Foot_L")), 0.001));
    Penguin.UpperBodyJiggle = SavedJiggle;

    auto RunSlide = [&Penguin](const TCHAR* Label, bool bSprint)
    {
        Penguin.bSprintHeld = bSprint;
        if (bSprint)
        {
            for (int32 I = 0; I < 55; ++I)
            {
                Penguin.Step(StepSeconds, FVector::ForwardVector);
            }
        }
        Penguin.ToggleSlide();
        for (int32 I = 0; I < 1200 && (Penguin.Speed() > 0.0f || Penguin.bSlideAirborne); ++I)
        {
            Penguin.Step(StepSeconds, FVector::ZeroVector);
        }
        UE_LOG(LogTemp, Display, TEXT("%s: %.2f m distance, %.2f m/s peak"), Label,
            Penguin.SlideDistance() / 100.0f, Penguin.SlidePeakSpeed() / 100.0f);
        return FVector2D(Penguin.SlideDistance(), Penguin.SlidePeakSpeed());
    };

    Penguin.ResetFlat();
    const FVector2D Flat = RunSlide(TEXT("Flat belly slide"), false);
    Penguin.ResetFlat();
    const FVector2D Sprint = RunSlide(TEXT("Flat sprint slide"), true);
    Penguin.ResetDownhill();
    const FVector2D Downhill = RunSlide(TEXT("Downhill belly slide"), false);
    TestTrue(TEXT("Downhill slide reaches the flat runout"), Penguin.GetActorLocation().X > 2600.0);
    Penguin.ResetUphill();
    const FVector2D Uphill = RunSlide(TEXT("Uphill belly slide"), false);

    TestTrue(TEXT("A stationary slide starts with a push"),
        Flat.X > 1000.0 && Flat.Y >= Penguin.StationarySlidePush);
    TestTrue(TEXT("Sprint entry adds a stronger boost"), Sprint.Y > Penguin.StationarySlidePush);
    TestTrue(TEXT("Downhill builds speed and uphill drains momentum"),
        Downhill.Y > Penguin.StationarySlidePush && Uphill.X < Flat.X);
    TestTrue(TEXT("Peak speed stays below the cap"), Downhill.Y <= Penguin.SlideSpeedLimit);

    auto MeasureSlideTurn = [&Penguin]()
    {
        Penguin.ResetFlat();
        Penguin.ToggleSlide();
        Penguin.RightInput = 1.0f;
        const int32 Steps = 12;
        for (int32 I = 0; I < Steps; ++I)
        {
            Penguin.Step(0.1f / Steps, FVector::RightVector);
        }
        Penguin.RightInput = 0.0f;
        return Penguin.Heading.Rotation().Yaw;
    };
    auto TryRamp = [&Penguin]()
    {
        Penguin.ResetRamp();
        Penguin.bSprintHeld = true;
        for (int32 I = 0; I < 55; ++I)
        {
            Penguin.Step(StepSeconds, FVector::ForwardVector);
        }
        Penguin.ToggleSlide();
        for (int32 I = 0; I < 25; ++I)
        {
            Penguin.Step(StepSeconds, FVector::ZeroVector);
        }
        Penguin.Jump();
        double Clearance = 0.0;
        int32 AirSteps = 0;
        bool bLandedSliding = false;
        for (int32 I = 0; I < 600 && Penguin.bSliding; ++I)
        {
            const bool bWasAirborne = Penguin.bSlideAirborne;
            Penguin.Step(StepSeconds, FVector::ZeroVector);
            if (Penguin.bSlideAirborne)
            {
                ++AirSteps;
                const FVector Position = Penguin.GetActorLocation();
                Clearance = FMath::Max(Clearance, Position.Z - Penguin.Terrain->HeightAt(Position.X, Position.Y) - 1.0);
            }
            if (bWasAirborne && !Penguin.bSlideAirborne)
            {
                bLandedSliding = Penguin.bSliding;
                break;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("Ramp: %.2f seconds airborne, %.2f m over the snow"),
            AirSteps * StepSeconds, Clearance / 100.0);
        return Clearance > 20.0 && AirSteps > 18 && bLandedSliding;
    };
    const FVector RampCrest = Penguin.Terrain->GetActorLocation() + FVector(145.0, -520.0, 0.0);
    FHitResult RampSurface;
    TestTrue(TEXT("The practice ramp is present in the snow collision mesh"),
        GameWorld->LineTraceSingleByChannel(RampSurface, RampCrest + FVector(0, 0, 150),
            RampCrest - FVector(0, 0, 100), ECC_Visibility)
        && RampSurface.GetActor() == Penguin.Terrain
        && FMath::Abs(RampSurface.ImpactPoint.Z - Penguin.Terrain->HeightAt(RampCrest.X, RampCrest.Y)) < 12.0);
    TestTrue(TEXT("A sprint slide can jump and land back in the slide"), TryRamp());
    Penguin.bSprintHeld = false;
    TestTrue(TEXT("The slide turns 130 degrees per second"),
        FMath::IsNearlyEqual(MeasureSlideTurn(), 13.0, 0.05));

    // Exercise the real binding and the recovery path with the sprint modifier held.
    Penguin.ResetFlat();
    auto* Controller = CastChecked<APlayerController>(Penguin.GetController());
    if (TestTrue(TEXT("Player input initialized"), Controller->PlayerInput && Penguin.InputComponent))
    {
        UPlayerInput& PlayerInput = *Controller->PlayerInput;
        const TArray<UInputComponent*> Stack{Penguin.InputComponent};
        auto Key = [&PlayerInput, &Stack](const FKey& Button, EInputEvent Event)
        {
            const float Amount = Event == IE_Pressed ? 1.0f : 0.0f;
            PlayerInput.InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0),
                Button, Event, Amount, false, FPlatformTime::Cycles64()));
            PlayerInput.ProcessInputStack(Stack, StepSeconds, false);
        };

        Key(EKeys::LeftShift, IE_Pressed);
        Key(EKeys::W, IE_Pressed);
        for (int32 I = 0; I < 55; ++I)
        {
            Penguin.Tick(StepSeconds);
        }
        const float EntrySpeed = Penguin.Speed();
        Key(EKeys::C, IE_Pressed);
        TestTrue(TEXT("C starts a sprint slide"), Penguin.bSliding && Penguin.bSprintEntry);
        TestTrue(TEXT("A sprint slide boosts the entry speed immediately"), Penguin.Speed() > EntrySpeed + 1500.0f);

        Key(EKeys::C, IE_Released);
        Penguin.Tick(0.12f);
        const float DiveTime = Penguin.SlidePoseTime;
        Key(EKeys::C, IE_Pressed);
        TestTrue(TEXT("A second C press cancels the slide immediately"), !Penguin.bSliding && Penguin.bRecovering);
        const double CancelPosition = Penguin.GetActorLocation().X;
        Penguin.Tick(StepSeconds);
        TestTrue(TEXT("The interrupted dive reverses without losing movement input"),
            Penguin.SlidePoseTime < DiveTime && Penguin.GetActorLocation().X > CancelPosition);
        Key(EKeys::C, IE_Released);
        Key(EKeys::W, IE_Released);
        Key(EKeys::LeftShift, IE_Released);

        Penguin.ResetAt(-1000.0);
        Key(EKeys::C, IE_Pressed);
        Key(EKeys::C, IE_Released);
        Penguin.GroundSpeed = 0.0f;
        for (int32 I = 0; I < 480; ++I)
        {
            Penguin.Tick(StepSeconds);
        }
        const FVector RestPosition = Penguin.GetActorLocation();
        const float RestPose = Penguin.SlidePoseTime;
        for (int32 I = 0; I < 60; ++I)
        {
            Penguin.Tick(StepSeconds);
        }
        TestTrue(TEXT("A stopped slide holds its belly pose until C is pressed again"),
            Penguin.bSliding && !Penguin.bRecovering && Penguin.Speed() == 0.0f && !Penguin.CanFire()
            && Penguin.GetActorLocation().Equals(RestPosition, 0.01) && Penguin.SlidePoseTime == RestPose);
        Key(EKeys::C, IE_Pressed);
        TestTrue(TEXT("C starts standing up from a stopped slide"), !Penguin.bSliding && Penguin.bRecovering);
        Key(EKeys::C, IE_Released);
    }

    Penguin.bSprintHeld = false;
    Penguin.ResetFlat();
    return true;
}

#endif
