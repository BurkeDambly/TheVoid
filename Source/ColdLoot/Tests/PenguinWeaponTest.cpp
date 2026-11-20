#if WITH_DEV_AUTOMATION_TESTS

#include "../ColdLootBullet.h"
#include "../ColdLootBulletImpact.h"
#include "../ColdLootPracticeTarget.h"
#include "../ColdLootWeaponDefinition.h"
#include "../PenguinPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenguinWeaponTest, "ColdLoot.SlidePark.Weapon",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPenguinWeaponTest::RunTest(const FString&)
{
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
        {
            World = Context.World();
            break;
        }
    }
    if (!TestNotNull(TEXT("Running Slide Park world"), World))
    {
        return false;
    }
    TActorIterator<APenguinPawn> It(World);
    if (!TestTrue(TEXT("Pawn has equipped the fitted weapon"), It && It->WeaponDefinition
        && It->WeaponDefinition->HoldPose && It->EquippedFiringSound && It->CanFire()))
    {
        return false;
    }
    APenguinPawn& Pawn = **It;
    APlayerController* Controller = CastChecked<APlayerController>(Pawn.GetController());
    if (!TestTrue(TEXT("Real input bindings are available"), Controller->PlayerInput && Pawn.InputComponent))
    {
        return false;
    }

    constexpr float Step = 1.0f / 60.0f;
    UPlayerInput& Input = *Controller->PlayerInput;
    const TArray<UInputComponent*> Stack{Pawn.InputComponent};
    auto Key = [&Input, &Stack](const FKey& Button, EInputEvent Event)
    {
        Input.InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Button,
            Event, Event == IE_Pressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64()));
        Input.ProcessInputStack(Stack, Step, false);
    };
    auto Advance = [&Pawn](int32 Frames, float DeltaSeconds = 1.0f / 60.0f)
    {
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            Pawn.Tick(DeltaSeconds);
            Pawn.CameraArm->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
            Pawn.PenguinMesh->TickAnimation(DeltaSeconds, false);
            Pawn.PenguinMesh->RefreshBoneTransforms();
        }
    };
    auto CountBullets = [World]()
    {
        int32 Count = 0;
        for (TActorIterator<AColdLootBullet> Bullet(World); Bullet; ++Bullet)
        {
            ++Count;
        }
        return Count;
    };

    Pawn.ResetFlat();
    Advance(3);
    const FTransform Grip = Pawn.WeaponGrip->GetComponentTransform();
    const FTransform ReceiverContact = Pawn.WeaponReceiver->GetSocketTransform(TEXT("Weapon_Contact"));
    TestTrue(TEXT("The receiver contact coincides with the flipper grip"), Grip.Equals(ReceiverContact, 0.05));
    TestTrue(TEXT("The fitted weapon points forward in the idle hold"),
        FVector::DotProduct(Pawn.WeaponReceiver->GetForwardVector(), FVector::ForwardVector) > 0.98);
    const float HipFOV = Pawn.Camera->FieldOfView;
    TestTrue(TEXT("Hip fire uses a side-on shoulder camera"), Pawn.CameraArm->SocketOffset.Y > 140.0f);

    Pawn.LookYaw(90.0f);
    TestTrue(TEXT("Standing mouse input rotates the camera"),
        FMath::IsNearlyEqual(Pawn.CameraArm->GetRelativeRotation().Yaw, 90.0, 0.1));
    Pawn.LookPitch(-35.0f);
    Advance(10);
    const FRotator IdleBore = Pawn.WeaponReceiver->GetSocketRotation(TEXT("Muzzle"));
    TestTrue(TEXT("Standing aim turns the body and pitches the rifle without turning the flipper sideways"),
        FMath::Abs(FMath::FindDeltaAngleDegrees(IdleBore.Pitch, Pawn.Camera->GetForwardVector().Rotation().Pitch)) < 1.5
        && FVector::DotProduct(Pawn.BodyPivot->GetForwardVector(), Pawn.GetLookYaw().Vector()) > 0.999
        && FMath::Abs(FMath::FindDeltaAngleDegrees(IdleBore.Yaw, Pawn.BodyPivot->GetComponentRotation().Yaw)) < 1.5);
    Pawn.LookPitch(35.0f);
    Advance(2);
    const FRotator StrafeView = Pawn.CameraArm->GetRelativeRotation();
    const FVector Forward = FRotator(0, StrafeView.Yaw, 0).Vector();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const FVector WalkStart = Pawn.GetActorLocation();
    Key(EKeys::W, IE_Pressed);
    Advance(24);
    const FVector WalkDelta = Pawn.GetActorLocation() - WalkStart;
    TestTrue(TEXT("W moves along the reticle direction after a 90-degree orbit"),
        FVector::DotProduct(WalkDelta.GetSafeNormal2D(), Forward) > 0.999
        && Pawn.CameraArm->GetRelativeRotation().Equals(StrafeView, 0.01));
    Key(EKeys::W, IE_Released);

    const FVector WalkingFacing = Pawn.BodyPivot->GetForwardVector().GetSafeNormal2D();
    for (const FKey& StrafeKey : {EKeys::A, EKeys::D})
    {
        const FVector Before = Pawn.GetActorLocation();
        Key(StrafeKey, IE_Pressed);
        Advance(18);
        Key(StrafeKey, IE_Released);
        const double Side = StrafeKey == EKeys::A ? -1.0 : 1.0;
        TestTrue(TEXT("A/D strafe without steering the body or camera"),
            FVector::DotProduct((Pawn.GetActorLocation() - Before).GetSafeNormal2D(), Right * Side) > 0.999
            && FVector::DotProduct(Pawn.BodyPivot->GetForwardVector().GetSafeNormal2D(), WalkingFacing) > 0.999
            && Pawn.CameraArm->GetRelativeRotation().Equals(StrafeView, 0.01));
    }
    Key(EKeys::W, IE_Pressed);
    Pawn.LookYaw(-35.0f);
    Advance(12);
    TestTrue(TEXT("Mouse yaw still controls the view while moving"),
        FMath::IsNearlyEqual(Pawn.CameraArm->GetRelativeRotation().Yaw, 55.0, 0.1));
    Key(EKeys::W, IE_Released);
    Pawn.ResetFlat();
    Advance(24);

    TestTrue(TEXT("Hip camera boom is ten percent longer"),
        FMath::IsNearlyEqual(Pawn.CameraArm->TargetArmLength, 559.02f, 0.1f));

    Key(EKeys::W, IE_Pressed);
    Key(EKeys::LeftShift, IE_Pressed);
    Advance(27);
    TestTrue(TEXT("Shift starts sprint"), Pawn.bSprinting);
    Key(EKeys::LeftMouseButton, IE_Pressed);
    double LargestSprintShake = 0.0;
    for (int32 Frame = 0; Frame < 26; ++Frame)
    {
        Advance(1);
        LargestSprintShake = FMath::Max(LargestSprintShake, Pawn.CameraKick.Size());
    }
    TestTrue(TEXT("Hip-fire shoots without breaking sprint"), Pawn.bSprinting && CountBullets() > 1);
    TestTrue(TEXT("Sprint shots have moderate spread and a stronger camera kick"),
        Pawn.SprintSpread.Size() >= 0.017 && Pawn.SprintSpread.Size() < 0.057 && LargestSprintShake > 0.078);
    TestTrue(TEXT("Every sprint shot refreshes the two-second weapon hold"), Pawn.WeaponHoldTime > 1.93f);
    Key(EKeys::LeftMouseButton, IE_Released);
    Advance(FMath::FloorToInt((Pawn.WeaponHoldTime - 0.03f) / Step));
    TestTrue(TEXT("Weapon stays raised until two seconds after the last sprint shot"),
        Pawn.bSprinting && Pawn.WeaponHoldTime > 0.0f && Pawn.WeaponGrip->IsVisible() && Pawn.SprintFireBlend > 0.99f);
    Advance(10);
    TestTrue(TEXT("Weapon reholsters after the hold expires while sprinting"),
        Pawn.bSprinting && Pawn.WeaponHoldTime == 0.0f && !Pawn.WeaponGrip->IsVisible());
    for (TActorIterator<AColdLootBullet> Bullet(World); Bullet; ++Bullet)
    {
        Bullet->Destroy();
    }
    Pawn.ResetFlat();
    Advance(27);
    Key(EKeys::RightMouseButton, IE_Pressed);
    Advance(30);
    TestTrue(TEXT("Holding RMB breaks sprint with Shift still held"), Pawn.IsAiming() && !Pawn.bSprinting);
    TestTrue(TEXT("ADS extends the shoulder camera boom by fifty percent and keeps its narrower FOV"),
        Pawn.AimBlend > 0.99f && FMath::IsNearlyEqual(Pawn.CameraArm->TargetArmLength, 580.8f, 1.0f)
        && Pawn.CameraArm->TargetArmLength > Pawn.HipArmLength
        && Pawn.CameraArm->SocketOffset.Y > 175.0f
        && Pawn.Camera->FieldOfView < HipFOV);
    Key(EKeys::W, IE_Released);
    Key(EKeys::LeftShift, IE_Released);
    Pawn.LookYaw(22.0f);
    const FVector AimHeading = Pawn.GetLookYaw().Vector();
    const FVector BeforeStrafe = Pawn.GetActorLocation();
    Key(EKeys::D, IE_Pressed);
    Advance(24);
    const FVector AimedHeading = Pawn.GetWeaponAimDirection().GetSafeNormal2D();
    TestTrue(TEXT("ADS allows strafe movement while the penguin stays facing its aim"),
        FVector::DotProduct(Pawn.BodyPivot->GetForwardVector().GetSafeNormal2D(), AimedHeading) > 0.995
        && FVector::DotProduct(Pawn.GetActorLocation() - BeforeStrafe, FVector::CrossProduct(FVector::UpVector, AimHeading)) > 20.0);
    Key(EKeys::D, IE_Released);
    Advance(12);

    const FVector LookDirection = Pawn.Camera->GetForwardVector();
    const FTransform InitialMuzzle = Pawn.WeaponReceiver->GetSocketTransform(TEXT("Muzzle"));
    const FVector AimPoint = InitialMuzzle.GetLocation() + InitialMuzzle.GetRotation().GetForwardVector() * 980.0;
    AActor* Target = World->SpawnActor<AActor>();
    UBoxComponent* Collider = NewObject<UBoxComponent>(Target);
    Target->SetRootComponent(Collider);
    Collider->SetBoxExtent(FVector(25, 32.5, 32.5));
    Collider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collider->SetCollisionObjectType(ECC_WorldDynamic);
    Collider->SetCollisionResponseToAllChannels(ECR_Block);
    Collider->SetWorldLocation(AimPoint);
    Collider->RegisterComponent();

    Advance(24);
    const FVector OriginalFacing = Pawn.BodyPivot->GetForwardVector();
    Pawn.LookYaw(6.0f);
    bool bFlipperStayedForward = true;
    for (int32 Frame = 0; Frame < 24; ++Frame)
    {
        Advance(1);
        const FVector MuzzleDirection = Pawn.WeaponReceiver->GetSocketRotation(TEXT("Muzzle")).Vector();
        const double RelativePitch = FMath::RadiansToDegrees(FMath::Atan2(
            FVector::DotProduct(MuzzleDirection, Pawn.BodyPivot->GetUpVector()),
            FVector::DotProduct(MuzzleDirection, Pawn.BodyPivot->GetForwardVector())));
        bFlipperStayedForward &= FMath::Abs(FVector::DotProduct(MuzzleDirection, Pawn.BodyPivot->GetRightVector())) < 0.002
            && FMath::Abs(RelativePitch - Pawn.Camera->GetForwardVector().Rotation().Pitch) < 0.25;
    }
    TestTrue(TEXT("Sweeping off a close target turns the body without swinging the flipper sideways"),
        bFlipperStayedForward && FVector::DotProduct(OriginalFacing, Pawn.BodyPivot->GetForwardVector()) < 0.999);
    Pawn.LookYaw(-6.0f);
    Advance(30);
    const FTransform MuzzleBeforeShot = Pawn.WeaponReceiver->GetSocketTransform(TEXT("Muzzle"));
    const FVector Bore = MuzzleBeforeShot.GetRotation().GetForwardVector();
    const FVector ReticlePoint = Pawn.GetBoreReticlePoint();
    TestTrue(TEXT("The bore follows the view and the reticle looks past the target"),
        FVector::DotProduct(LookDirection, Bore) > 0.999
        && FVector::Distance(ReticlePoint, MuzzleBeforeShot.GetLocation()) > 10000.0
        && FVector::DotProduct(Bore, (ReticlePoint - MuzzleBeforeShot.GetLocation()).GetSafeNormal()) > 0.9999);
    Key(EKeys::LeftMouseButton, IE_Pressed);
    Advance(3);
    TActorIterator<AColdLootBullet> Shot(World);
    TestTrue(TEXT("LMB launches a shot and adds a small camera kick with a sideways component"),
        Shot && FMath::Abs(Pawn.CameraKick.X) > 0.003 && Pawn.CameraKick.Size() < 0.108);
    if (Shot)
    {
        AColdLootBullet* Bullet = *Shot;
        auto* Flight = Bullet->FindComponentByClass<UProjectileMovementComponent>();
        TestTrue(TEXT("Projectile starts at the fitted muzzle and flies exactly along its bore"),
            FVector::Distance(Bullet->GetActorLocation(), MuzzleBeforeShot.GetLocation()) < 1.0
            && FVector::DotProduct(Bore, Flight->Velocity.GetSafeNormal()) > 0.99999);
        TestTrue(TEXT("The starter projectile launches at 14400 cm/s"),
            FMath::IsNearlyEqual(Flight->Velocity.Size(), 14400.0, 0.1));
        const FVector LaunchVelocity = Flight->Velocity;
        const float TravelTime = (FVector::Distance(Bullet->GetActorLocation(), AimPoint) + 20.0)
            / LaunchVelocity.Size();
        Flight->TickComponent(TravelTime, LEVELTICK_All, nullptr);
        if (TestTrue(TEXT("The first target impact reflects the tracer once"),
            !Bullet->IsActorBeingDestroyed() && !Flight->bShouldBounce
            && FVector::DotProduct(Flight->Velocity, LaunchVelocity) < 0.0))
        {
            bool bSurfaceFlash = false;
            for (TActorIterator<AColdLootBulletImpact> Effect(World); Effect; ++Effect)
            {
                bSurfaceFlash |= Collider->Bounds.GetBox().ExpandBy(8.0).IsInsideOrOn(Effect->GetActorLocation());
            }
            TestTrue(TEXT("The ricochet flashes at the hit surface"), bSurfaceFlash);
            auto* Tracer = Bullet->FindComponentByClass<UStaticMeshComponent>();
            const double LengthBeforeFade = Tracer->GetRelativeScale3D().X;
            const float Life = Bullet->GetLifeSpan();
            Bullet->Tick(0.075f);
            TestTrue(TEXT("The ricochet tracer shrinks and expires quickly"),
                Life > 0.0f && Life < 0.18f && Tracer->GetRelativeScale3D().X < LengthBeforeFade * 0.65);

            Collider->SetWorldLocation(Bullet->GetActorLocation() + Flight->Velocity.GetSafeNormal() * 70.0);
            Flight->TickComponent(0.04f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("The ricochet stops at the second surface"), Bullet->IsActorBeingDestroyed());
        }
        if (IsValid(Bullet))
        {
            Bullet->Destroy();
        }
    }
    Target->Destroy();
    Key(EKeys::LeftMouseButton, IE_Released);
    Key(EKeys::RightMouseButton, IE_Released);
    TestFalse(TEXT("Releasing RMB exits ADS"), Pawn.IsAiming());

    int32 MarkerHits = 0;
    for (TActorIterator<AStaticMeshActor> Marker(World); Marker; ++Marker)
    {
        if (GetNameSafe(Marker->GetStaticMeshComponent()->GetStaticMesh()) != TEXT("Cylinder"))
        {
            continue;
        }
        const FVector Center = Marker->GetActorLocation();
        const FVector AcrossMarker(40, 0, 0);
        FHitResult Hit;
        const bool bStopsBullet = World->LineTraceSingleByChannel(Hit, Center - AcrossMarker, Center + AcrossMarker,
            AColdLootBullet::CollisionChannel) && Hit.GetActor() == *Marker;
        TestTrue(TEXT("Trail poles stop bullets and leave the shoulder camera unobstructed"),
            bStopsBullet && Marker->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore);
        ++MarkerHits;
    }
    TestEqual(TEXT("The six trail poles have bullet collision"), MarkerHits, 6);

    TActorIterator<AColdLootPracticeTarget> PracticeTarget(World);
    if (TestTrue(TEXT("Slide Park has reactive practice barrels"), static_cast<bool>(PracticeTarget)))
    {
        const FVector BarrelLocation = PracticeTarget->GetActorLocation();
        const FVector Start = PracticeTarget->GetActorLocation() + FVector(-115, 14, 18);
        AColdLootBullet* Round = World->SpawnActor<AColdLootBullet>(Start, FRotator::ZeroRotator);
        Round->Launch(FVector::ForwardVector, 6000.0f, 20.0f, Pawn.WeaponDefinition->TracerMaterial);
        Round->FindComponentByClass<UProjectileMovementComponent>()->TickComponent(0.03f, LEVELTICK_All, nullptr);
        TestFalse(TEXT("The round ricochets off the practice barrel"),
            Round->FindComponentByClass<UProjectileMovementComponent>()->bShouldBounce);
        Round->Destroy();
        const TWeakObjectPtr<AColdLootPracticeTarget> HitBarrel(*PracticeTarget);
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, HitBarrel, BarrelLocation,
            StartTime = FPlatformTime::Seconds()]()
        {
            if (FPlatformTime::Seconds() - StartTime < 0.25)
            {
                return false;
            }
            TestTrue(TEXT("A bullet pushes the practice barrel along its flight path"),
                HitBarrel.IsValid() && HitBarrel->GetActorLocation().X > BarrelLocation.X + 5.0);
            return true;
        }));
    }

    Pawn.ResetFlat();
    Key(EKeys::RightMouseButton, IE_Pressed);
    Pawn.LookPitch(-60.0f);
    Advance(32);
    const FVector DownwardBore = Pawn.WeaponReceiver->GetSocketTransform(TEXT("Muzzle")).GetRotation().GetForwardVector();
    TestTrue(TEXT("The rifle keeps pointing into the snow when ADS looks steeply down"), DownwardBore.Z < -0.55);
    Key(EKeys::LeftMouseButton, IE_Pressed);
    Advance(3);
    TestTrue(TEXT("A steep downward shot fires even when the snow is close to the muzzle"), Pawn.RecoilGoal.Y > 0.0);
    Key(EKeys::LeftMouseButton, IE_Released);
    Key(EKeys::RightMouseButton, IE_Released);
    for (TActorIterator<AColdLootBullet> Bullet(World); Bullet; ++Bullet)
    {
        Bullet->Destroy();
    }
    Pawn.ResetFlat();

    Key(EKeys::C, IE_Pressed);
    Key(EKeys::C, IE_Released);
    Advance(15);
    Key(EKeys::LeftMouseButton, IE_Pressed);
    Advance(2);
    TestTrue(TEXT("LMB during slide does not fire"),
        Pawn.bSliding && Pawn.ShotRequestTime == 0.0f && !TActorIterator<AColdLootBullet>(World));
    Key(EKeys::LeftMouseButton, IE_Released);
    Pawn.ResetFlat();

    Pawn.LookYaw(180.0f);
    Pawn.LookPitch(11.0f);
    Key(EKeys::RightMouseButton, IE_Pressed);
    Advance(40);
    const FTransform MuzzleWithoutPole = Pawn.WeaponReceiver->GetSocketTransform(TEXT("Muzzle"));
    const FVector ReticleWithoutPole = Pawn.GetBoreReticlePoint();
    const FVector PoleLocation = MuzzleWithoutPole.GetLocation()
        + MuzzleWithoutPole.GetRotation().GetForwardVector() * 460.0;
    AActor* ForegroundPole = World->SpawnActor<AActor>();
    UBoxComponent* PoleCollision = NewObject<UBoxComponent>(ForegroundPole);
    ForegroundPole->SetRootComponent(PoleCollision);
    PoleCollision->SetBoxExtent(FVector(7, 7, 55));
    PoleCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PoleCollision->SetCollisionResponseToAllChannels(ECR_Block);
    PoleCollision->SetWorldLocation(PoleLocation);
    PoleCollision->RegisterComponent();
    Advance(24);
    TestTrue(TEXT("The pole leaves the reticle and firing line in place"),
        Pawn.GetBoreReticlePoint().Equals(ReticleWithoutPole, 0.3)
        && Pawn.WeaponReceiver->GetSocketTransform(TEXT("Muzzle")).Equals(MuzzleWithoutPole, 0.2));
    Key(EKeys::LeftMouseButton, IE_Pressed);
    Advance(1);
    TActorIterator<AColdLootBullet> ForegroundRound(World);
    if (TestTrue(TEXT("A round launches along the obstructed bore"), static_cast<bool>(ForegroundRound)))
    {
        auto* Flight = ForegroundRound->FindComponentByClass<UProjectileMovementComponent>();
        Flight->TickComponent(0.06f, LEVELTICK_All, nullptr);
        TestFalse(TEXT("The line of fire is blocked by the pole"), Flight->bShouldBounce);
        ForegroundRound->Destroy();
    }
    Key(EKeys::LeftMouseButton, IE_Released);
    Key(EKeys::RightMouseButton, IE_Released);
    ForegroundPole->Destroy();
    Pawn.ResetFlat();

    Pawn.LookPitch(13.0f);
    Key(EKeys::RightMouseButton, IE_Pressed);
    Advance(36);
    const FRotator BeforeBurst = Pawn.Camera->GetComponentRotation();
    Key(EKeys::LeftMouseButton, IE_Pressed);
    double LowestYaw = 0.0;
    double HighestYaw = 0.0;
    double FirstShotPitch = 0.0;
    double LastShotPitch = 0.0;
    TSet<AColdLootBullet*> BurstRounds;
    for (int32 Frame = 0; Frame < 180; ++Frame)
    {
        Advance(1);
        if (Frame >= 116)
        {
            const double Yaw = FMath::FindDeltaAngleDegrees(BeforeBurst.Yaw, Pawn.Camera->GetComponentRotation().Yaw);
            LowestYaw = FMath::Min(LowestYaw, Yaw);
            HighestYaw = FMath::Max(HighestYaw, Yaw);
        }
        for (TActorIterator<AColdLootBullet> Round(World); Round; ++Round)
        {
            if (!BurstRounds.Contains(*Round))
            {
                LastShotPitch = Round->FindComponentByClass<UProjectileMovementComponent>()->Velocity.Rotation().Pitch;
                if (BurstRounds.IsEmpty())
                {
                    FirstShotPitch = LastShotPitch;
                }
                BurstRounds.Add(*Round);
            }
        }
    }
    const double AfterBurstPitch = Pawn.Camera->GetComponentRotation().Pitch;
    TestTrue(TEXT("Sustained ADS recoil lifts the reticle and caps near 5.4 degrees"),
        FMath::IsNearlyEqual(AfterBurstPitch - BeforeBurst.Pitch, 5.4, 0.2) && LastShotPitch - FirstShotPitch > 3.6);
    TestTrue(TEXT("The view continues to weave left and right after reaching its climb limit"),
        LowestYaw < -0.45 && HighestYaw > 0.45 && BurstRounds.Num() > 40);
    Key(EKeys::LeftMouseButton, IE_Released);
    Pawn.LookPitch(-2.0f);
    Advance(1);
    TestTrue(TEXT("Moving the mouse down immediately counters accumulated recoil"),
        AfterBurstPitch - Pawn.Camera->GetComponentRotation().Pitch > 1.75);
    Advance(60);
    TestTrue(TEXT("Aim settles after release without overshooting the original mouse aim"),
        FMath::Abs(Pawn.Camera->GetComponentRotation().Pitch - BeforeBurst.Pitch) < 0.3);
    Key(EKeys::RightMouseButton, IE_Released);
    for (AColdLootBullet* Round : BurstRounds)
    {
        Round->Destroy();
    }
    Pawn.ResetFlat();

    const FRotator ShotView(0, 105, 0);
    Pawn.LookYaw(ShotView.Yaw);
    Pawn.LookPitch(20.0f);
    Advance(10);
    Key(EKeys::LeftMouseButton, IE_Pressed);
    constexpr float FastFrame = 1.0f / 137.0f;
    Advance(45, FastFrame);
    const int32 BeforeSecond = CountBullets();
    TestTrue(TEXT("Holding fire shoots after changing the aim direction"), BeforeSecond > 0);
    for (int32 Frame = 0; Frame < 137; ++Frame)
    {
        if (Frame % 8 == 0)
        {
            Pawn.LookYaw(Frame % 16 == 0 ? 28.0f : -28.0f);
        }
        Advance(1, FastFrame);
    }
    const int32 AfterSecond = CountBullets();
    TestEqual(TEXT("Fast reticle sweeps keep automatic fire at 15 rounds per second"), AfterSecond - BeforeSecond, 15);
    const FVector LastFacing = Pawn.BodyPivot->GetForwardVector().GetSafeNormal2D();
    Key(EKeys::LeftMouseButton, IE_Released);
    Pawn.LookYaw(40);
    Advance(74, FastFrame);
    TestEqual(TEXT("Releasing LMB stops the burst"), CountBullets(), AfterSecond);
    TestTrue(TEXT("Standing penguin follows mouse yaw after firing ends"),
        FVector::DotProduct(LastFacing, Pawn.BodyPivot->GetForwardVector()) < 0.9
        && FVector::DotProduct(Pawn.GetLookYaw().Vector(), Pawn.BodyPivot->GetForwardVector()) > 0.999);
    for (TActorIterator<AColdLootBullet> Bullet(World); Bullet; ++Bullet)
    {
        Bullet->Destroy();
    }
    Pawn.ResetFlat();

    if (FParse::Param(FCommandLine::Get(), TEXT("ColdLootWeaponPreview")))
    {
        Pawn.StartAim();
        const FString Image = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StarterWeaponADS.png"));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([&Pawn, Image, Frame = 0]() mutable
        {
            if (++Frame == 80)
            {
                FScreenshotRequest::RequestScreenshot(Image, false, false);
            }
            if (Frame == 87)
            {
                Pawn.StopAim();
                return true;
            }
            return false;
        }));
    }
    return true;
}

#endif
