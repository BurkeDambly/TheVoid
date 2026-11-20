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
            Penguin.Step(StepSeconds / Penguin.GetSlideSpeedScale(), FVector::ZeroVector);
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

    TestTrue(TEXT("A stationary slide starts with a short push and comes to rest"),
        Flat.X > 90.0 && Flat.X < 160.0 && FMath::IsNearlyEqual(Flat.Y, Penguin.StationarySlidePush, 1.0));
    TestTrue(TEXT("Sprint entry adds flat distance"), Sprint.X > Flat.X);
    TestTrue(TEXT("Downhill builds speed and carries farther"), Downhill.X > Sprint.X && Downhill.Y > Sprint.Y);
    TestTrue(TEXT("Uphill stops sooner"), Uphill.X < Flat.X);
    TestTrue(TEXT("Peak speed stays below the cap"), Downhill.Y <= Penguin.SlideSpeedLimit);

    Penguin.TogglePenguinForm();
    const float RequestedMultiplier = Penguin.FormSlideSpeedMultiplier;
    Penguin.FormSlideSpeedMultiplier = 1.0f;
    Penguin.ResetDownhill();
    const FVector2D FormAtBaseSpeed = RunSlide(TEXT("Penguin Form downhill at 1x"), false);
    Penguin.FormSlideSpeedMultiplier = RequestedMultiplier;
    Penguin.ResetDownhill();
    const FVector2D FormDownhill = RunSlide(TEXT("Penguin Form downhill slide"), false);
    TestTrue(TEXT("Penguin Form's speed multiplier preserves the route over the same terrain"),
        FMath::IsNearlyEqual(FormDownhill.Y, FormAtBaseSpeed.Y * 3.0, 1.0)
        && FMath::IsNearlyEqual(FormDownhill.X, FormAtBaseSpeed.X, 2.0)
        && FormDownhill.Y <= Penguin.GetSlideSpeedLimit());

    Penguin.ResetFlat();
    const FVector2D FormFlat = RunSlide(TEXT("Penguin Form flat slide"), false);
    TestTrue(TEXT("Penguin Form coasts farther on flat snow with reduced friction"),
        FMath::IsNearlyEqual(FormFlat.X, Flat.X / Penguin.FormSlideFrictionMultiplier, 2.0));
    Penguin.ResetUphill();
    const FVector2D FormUphill = RunSlide(TEXT("Penguin Form low-speed uphill slide"), false);
    TestTrue(TEXT("A slower Penguin Form slide retains more momentum uphill"),
        FormUphill.X > Uphill.X * 1.5 && !Penguin.bSlideAirborne);

    Penguin.ResetDownhill();
    Penguin.ToggleSlide();
    float ApproachSpeed = 0.0f;
    float LandingSpeed = 0.0f;
    int32 UphillLaunches = 0;
    bool bTrackingUphillLaunch = false;
    double MaxUphillClearance = 0.0;
    const float FormStep = StepSeconds / Penguin.GetSlideSpeedScale();
    for (int32 I = 0; I < 1200 && Penguin.bSliding; ++I)
    {
        const bool bWasAirborne = Penguin.bSlideAirborne;
        const float BeforeStep = Penguin.Speed();
        Penguin.Step(FormStep, FVector::ZeroVector);
        if (Penguin.bSlideAirborne)
        {
            if (!bWasAirborne)
            {
                bTrackingUphillLaunch = Penguin.Grade() >= 11.0f && Penguin.GetActorLocation().X < 5000.0;
                if (bTrackingUphillLaunch)
                {
                    ++UphillLaunches;
                    ApproachSpeed = BeforeStep;
                }
            }
            if (bTrackingUphillLaunch)
            {
                const FVector Position = Penguin.GetActorLocation();
                MaxUphillClearance = FMath::Max(MaxUphillClearance,
                    Position.Z - Penguin.Terrain->HeightAt(Position.X, Position.Y) - 1.0);
            }
        }
        if (bWasAirborne && !Penguin.bSlideAirborne && bTrackingUphillLaunch)
        {
            LandingSpeed = Penguin.Speed();
            bTrackingUphillLaunch = false;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Uphill launch: %.2f m clearance, %.2f m/s approach, %.2f m/s landing"),
        MaxUphillClearance / 100.0, ApproachSpeed / 100.0, LandingSpeed / 100.0);
    TestTrue(TEXT("A fast downhill run launches on the uphill bank and lands with most of its speed"),
        UphillLaunches == 1 && MaxUphillClearance > 65.0 && LandingSpeed > ApproachSpeed * 0.80f);

    Penguin.ResetAt(525.0);
    Penguin.ToggleSlide();
    Penguin.GroundSpeed = 1200.0f * Penguin.GetSlideSpeedScale();
    double RolloverClearance = 0.0;
    int32 RolloverAirSteps = 0;
    bool bLiftedOnDrop = false;
    for (int32 I = 0; I < 300 && Penguin.bSliding; ++I)
    {
        const bool bWasAirborne = Penguin.bSlideAirborne;
        Penguin.Step(FormStep, FVector::ZeroVector);
        if (Penguin.bSlideAirborne)
        {
            if (!bWasAirborne)
            {
                bLiftedOnDrop = Penguin.Grade() <= 1.0f;
            }
            ++RolloverAirSteps;
            const FVector Position = Penguin.GetActorLocation();
            RolloverClearance = FMath::Max(RolloverClearance,
                Position.Z - Penguin.Terrain->HeightAt(Position.X, Position.Y) - 1.0);
        }
        if (bWasAirborne && !Penguin.bSlideAirborne)
        {
            break;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Downhill rollover: %.2f seconds airborne, %.2f m over the snow"),
        RolloverAirSteps * FormStep, RolloverClearance / 100.0);
    TestTrue(TEXT("A fast slide lifts off where the terrain drops away"),
        bLiftedOnDrop && RolloverAirSteps * FormStep > 0.15f && RolloverClearance > 25.0);

    auto MeasureSlideTurn = [&Penguin]()
    {
        Penguin.ResetFlat();
        Penguin.ToggleSlide();
        const int32 Steps = FMath::RoundToInt(12.0f * Penguin.GetSlideSpeedScale());
        for (int32 I = 0; I < Steps; ++I)
        {
            Penguin.Step(0.1f / Steps, FVector::RightVector);
        }
        return Penguin.Heading.Rotation().Yaw;
    };
    const double FormTurn = MeasureSlideTurn();
    auto TryRamp = [&Penguin]()
    {
        Penguin.ResetRamp();
        Penguin.bSprintHeld = true;
        for (int32 I = 0; I < 55; ++I)
        {
            Penguin.Step(StepSeconds, FVector::ForwardVector);
        }
        Penguin.ToggleSlide();
        double Clearance = 0.0;
        int32 AirSteps = 0;
        bool bLandedSliding = false;
        for (int32 I = 0; I < 600 && Penguin.bSliding; ++I)
        {
            const bool bWasAirborne = Penguin.bSlideAirborne;
            Penguin.Step(StepSeconds / Penguin.GetSlideSpeedScale(), FVector::ZeroVector);
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
            AirSteps * StepSeconds / Penguin.GetSlideSpeedScale(), Clearance / 100.0);
        return Clearance > 20.0 && AirSteps > 18 && bLandedSliding;
    };
    const FVector RampCrest = Penguin.Terrain->GetActorLocation() + FVector(145.0, -520.0, 0.0);
    FHitResult RampSurface;
    TestTrue(TEXT("The practice ramp is present in the snow collision mesh"),
        GameWorld->LineTraceSingleByChannel(RampSurface, RampCrest + FVector(0, 0, 150),
            RampCrest - FVector(0, 0, 100), ECC_Visibility)
        && RampSurface.GetActor() == Penguin.Terrain
        && FMath::Abs(RampSurface.ImpactPoint.Z - Penguin.Terrain->HeightAt(RampCrest.X, RampCrest.Y)) < 12.0);
    TestTrue(TEXT("A Penguin Form sprint slide clears the ramp and lands back in the slide"), TryRamp());
    Penguin.TogglePenguinForm();
    TestFalse(TEXT("The normal sprint slide stays on the snow at the ramp"), TryRamp());
    Penguin.bSprintHeld = false;
    TestTrue(TEXT("Penguin Form turns 156 degrees per second while normal slides turn 52"),
        FMath::IsNearlyEqual(FormTurn, 15.6, 0.05)
        && FMath::IsNearlyEqual(MeasureSlideTurn(), 5.2, 0.05));

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

        const TArray<UMaterialInterface*> OriginalMaterials = Mesh.GetMaterials();
        Key(EKeys::Q, IE_Pressed);
        TestTrue(TEXT("Q activates the blue outline over the original character materials"),
            Penguin.IsPenguinFormActive() && Penguin.PenguinFormGlow
            && Mesh.GetOverlayMaterial() == Penguin.PenguinFormGlow && Mesh.GetMaterials() == OriginalMaterials);
        Key(EKeys::Q, IE_Released);
        Key(EKeys::Q, IE_Pressed);
        TestTrue(TEXT("Q clears the outline while leaving the original materials in place"),
            !Penguin.IsPenguinFormActive() && !Mesh.GetOverlayMaterial() && Mesh.GetMaterials() == OriginalMaterials);
        Key(EKeys::Q, IE_Released);

        Key(EKeys::LeftShift, IE_Pressed);
        Key(EKeys::W, IE_Pressed);
        for (int32 I = 0; I < 55; ++I)
        {
            Penguin.Tick(StepSeconds);
        }
        const float EntrySpeed = Penguin.Speed();
        Key(EKeys::C, IE_Pressed);
        TestTrue(TEXT("C starts a sprint slide"), Penguin.bSliding && Penguin.bSprintEntry);
        TestTrue(TEXT("Sprint slide carries the sprint entry speed"),
            FMath::IsNearlyEqual(Penguin.Speed(), FMath::Min(Penguin.SlideSpeedLimit, EntrySpeed), 1.0f));

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

        for (bool bForm : {false, true})
        {
            // Leave room for the powered glide and its runout before the main downhill begins.
            Penguin.ResetAt(-1000.0);
            if (bForm)
            {
                Key(EKeys::Q, IE_Pressed);
                Key(EKeys::Q, IE_Released);
            }
            Key(EKeys::C, IE_Pressed);
            Key(EKeys::C, IE_Released);
            for (int32 I = 0; I < 300; ++I)
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
            Key(EKeys::W, IE_Pressed);
            for (int32 I = 0; I < 100; ++I)
            {
                Penguin.Tick(StepSeconds);
            }
            TestTrue(bForm ? TEXT("W powers a stopped Penguin Form slide at sprint speed")
                           : TEXT("W keeps a stopped normal slide in place"),
                Penguin.bSliding && (bForm
                    ? FMath::IsNearlyEqual(Penguin.Speed(), Penguin.GetGaitSpeed(true), 1.0f)
                        && Penguin.GetActorLocation().X > RestPosition.X + 200.0
                    : Penguin.Speed() == 0.0f && Penguin.GetActorLocation().Equals(RestPosition, 0.01)));
            Key(EKeys::W, IE_Released);
            if (bForm)
            {
                for (int32 I = 0; I < 100; ++I)
                {
                    Penguin.Tick(StepSeconds);
                }
                TestTrue(TEXT("Releasing W lets the powered glide coast to a stop on its belly"),
                    Penguin.bSliding && Penguin.Speed() == 0.0f);
            }
            Key(EKeys::C, IE_Pressed);
            TestTrue(TEXT("C starts standing up from a stopped slide"), !Penguin.bSliding && Penguin.bRecovering);
            Key(EKeys::C, IE_Released);
            if (bForm)
            {
                Key(EKeys::Q, IE_Pressed);
                Key(EKeys::Q, IE_Released);
            }
        }
    }

    Penguin.bSprintHeld = false;
    Penguin.ResetFlat();
    if (FParse::Param(FCommandLine::Get(), TEXT("ColdLootFormPreview")))
    {
        Penguin.TogglePenguinForm();
        Penguin.ResetRamp();
        const FString Image = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PenguinForm.png"));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([&Penguin, Image, Frame = 0]() mutable
        {
            if (++Frame == 60)
            {
                FScreenshotRequest::RequestScreenshot(Image, false, false);
            }
            if (Frame >= 70)
            {
                Penguin.TogglePenguinForm();
                Penguin.ResetFlat();
                return true;
            }
            return false;
        }));
    }
    return true;
}

#endif
