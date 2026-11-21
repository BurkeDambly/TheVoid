#if WITH_DEV_AUTOMATION_TESTS

#include "../ColdLootGameMode.h"
#include "../ColdLootTuning.h"
#include "../ColdLootBullet.h"
#include "../ColdLootWeaponDefinition.h"
#include "../IcebergIsland.h"
#include "../IcebergNetworkTerrain.h"
#include "../PenguinPawn.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIcebergWeaponTest, "ColdLoot.IcebergNetwork.FiringLine",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FIcebergWeaponTest::RunTest(const FString&)
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
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    APenguinPawn* Penguin = Controller ? Cast<APenguinPawn>(Controller->GetPawn()) : nullptr;
    if (!TestTrue(TEXT("The player has the starter weapon"), Penguin && Penguin->HasWeapon()))
    {
        return false;
    }
    AColdLootGameState* GameState = World->GetGameState<AColdLootGameState>();
    if (!TestNotNull(TEXT("The game exposes playtest tuning"), GameState))
    {
        return false;
    }
    const auto BulletSpeed = EColdLootTuning::BulletSpeed;
    const float OriginalBulletScale = GameState->GetTuning().Get(BulletSpeed);
    const double ModeSpeed = World->GetNetMode() == NM_Standalone ? 2.5 : 1.0;

    auto Advance = [Penguin](int32 Frames)
    {
        constexpr float Step = 1.0f / 60.0f;
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            Penguin->Tick(Step);
            Penguin->CameraArm->TickComponent(Step, LEVELTICK_All, nullptr);
            Penguin->PenguinMesh->TickAnimation(Step, false);
            Penguin->PenguinMesh->RefreshBoneTransforms();
        }
    };
    const FName MuzzleName(TEXT("Muzzle"));
    Penguin->ResetFlat();
    Penguin->LookPitch(9.0f);
    Advance(8);
    TestTrue(TEXT("Hip camera extends its authored boom by ten percent"),
        FMath::IsNearlyEqual(Penguin->CameraArm->TargetArmLength, 559.02f, 1.0f));
    Penguin->LookYaw(-30.0f);
    Advance(1);
    TestTrue(TEXT("The hip-fire pose follows mouse yaw in one frame"),
        Penguin->BodyPivot->GetForwardVector().Equals(Penguin->GetLookYaw().Vector(), 0.001));
    const FQuat HipHead = Penguin->PenguinMesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component).GetRotation();

    Penguin->StartAim();
    Advance(45);
    TestTrue(TEXT("ADS uses the longer shoulder-camera boom"),
        FMath::IsNearlyEqual(Penguin->CameraArm->TargetArmLength, 580.8f, 0.1f));
    const FQuat AimHead = Penguin->PenguinMesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component).GetRotation();
    TestTrue(TEXT("ADS leans the head sideways"), HipHead.AngularDistance(AimHead) > FMath::DegreesToRadians(6.0));

    Penguin->LookYaw(24.0f);
    Penguin->LookPitch(3.0f);
    Advance(1);
    const FTransform Muzzle = Penguin->WeaponReceiver->GetSocketTransform(MuzzleName);
    const FVector Bore = Muzzle.GetRotation().GetForwardVector();
    TestTrue(TEXT("The body and bore track mouse input during ADS in one frame"),
        FVector::DotProduct(Penguin->BodyPivot->GetForwardVector(),
            Penguin->Camera->GetForwardVector().GetSafeNormal2D()) > 0.9999
        && FVector::DotProduct(Bore, Penguin->Camera->GetForwardVector()) > 0.9999
        && FMath::Abs(FVector::DotProduct(Bore, Penguin->BodyPivot->GetRightVector())) < 0.002);

    AActor* Target = World->SpawnActor<AActor>();
    UBoxComponent* Collider = NewObject<UBoxComponent>(Target);
    Target->SetRootComponent(Collider);
    Collider->SetBoxExtent(FVector(14.0));
    Collider->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collider->SetCollisionResponseToAllChannels(ECR_Block);
    Collider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    const FVector CameraLocation = Penguin->Camera->GetComponentLocation();
    const FVector CameraDirection = Penguin->Camera->GetForwardVector();
    const double MuzzleDepth = FVector::DotProduct(Muzzle.GetLocation() - CameraLocation, CameraDirection);
    Collider->SetWorldLocation(CameraLocation + CameraDirection * (MuzzleDepth + 160.0)
        + Penguin->Camera->GetRightVector() * 55.0);
    Collider->RegisterComponent();
    Advance(1);
    TestTrue(TEXT("An object beside the crosshair does not turn the penguin"),
        Penguin->WeaponReceiver->GetSocketTransform(MuzzleName).Equals(Muzzle, 0.3));

    const FTransform HoldInBody = Muzzle.GetRelativeTransform(Penguin->BodyPivot->GetComponentTransform());
    for (const double Range : {160.0, 900.0, 4500.0})
    {
        Collider->SetWorldLocation(CameraLocation + CameraDirection * (MuzzleDepth + Range));
        Advance(1);
        const FTransform AimedMuzzle = Penguin->WeaponReceiver->GetSocketTransform(MuzzleName);
        FHitResult Hit;
        const bool bHits = World->LineTraceSingleByChannel(Hit, AimedMuzzle.GetLocation(),
            AimedMuzzle.GetLocation() + AimedMuzzle.GetRotation().GetForwardVector() * (Range + 400.0),
            AColdLootBullet::CollisionChannel, FCollisionQueryParams(SCENE_QUERY_STAT(FiringLineTest), true, Penguin));
        TestTrue(FString::Printf(TEXT("The actual barrel line hits the crosshair target at %.1f m"), Range / 100.0),
            bHits && Hit.GetActor() == Target);
        TestTrue(TEXT("Close-range correction turns the whole body with the same flipper pose"),
            AimedMuzzle.GetRelativeTransform(Penguin->BodyPivot->GetComponentTransform()).Equals(HoldInBody, 0.1)
            && Penguin->Camera->GetForwardVector().Equals(CameraDirection, 0.0001));
    }

    Collider->SetWorldLocation(CameraLocation + CameraDirection * (MuzzleDepth + 160.0));
    Penguin->StartFire();
    Advance(1);
    Penguin->StopFire();
    if (TActorIterator<AColdLootBullet> Shot(World); TestTrue(TEXT("A shot launches at the close target"), static_cast<bool>(Shot)))
    {
        auto* Flight = Shot->FindComponentByClass<UProjectileMovementComponent>();
        TestTrue(TEXT("The projectile starts at the current game mode's speed"),
            FMath::IsNearlyEqual(Flight->Velocity.Size(),
                Penguin->WeaponDefinition->BulletSpeed * ModeSpeed * OriginalBulletScale, 1.0));
        Flight->TickComponent(0.04f, LEVELTICK_All, nullptr);
        TestFalse(TEXT("The projectile collides with the close target under the crosshair"), Flight->bShouldBounce);
        Shot->Destroy();
    }
    Target->Destroy();

    Penguin->ResetFlat();
    Penguin->LookPitch(-32.0f);
    Advance(1);
    FHitResult SnowHit;
    const FVector SnowView = Penguin->Camera->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(CloseSnowAimTest), true, Penguin);
    const bool bSeesSnow = World->LineTraceSingleByChannel(SnowHit, SnowView,
        SnowView + Penguin->Camera->GetForwardVector() * 3000.0, AColdLootBullet::CollisionChannel, Query);
    const FTransform DownMuzzle = Penguin->WeaponReceiver->GetSocketTransform(MuzzleName);
    TestTrue(TEXT("Looking down lines up the bore with the nearby snow under the crosshair"),
        bSeesSnow && FVector::CrossProduct(SnowHit.ImpactPoint - DownMuzzle.GetLocation(),
            DownMuzzle.GetRotation().GetForwardVector()).Size() < 2.0);

    Penguin->ResetFlat();
    Penguin->LookPitch(9.0f);
    Advance(1);
    Penguin->LookYaw(21.0f);
    Penguin->LookPitch(3.0f);
    TestTrue(TEXT("Solo tuning allows a live bullet speed change"), GameState->SetTuning(BulletSpeed, 1.35f));
    Penguin->StartFire();
    Advance(1);
    const FTransform ShotMuzzle = Penguin->WeaponReceiver->GetSocketTransform(MuzzleName);
    if (TActorIterator<AColdLootBullet> Shot(World); TestTrue(TEXT("A shot launches"), static_cast<bool>(Shot)))
    {
        const auto* Flight = Shot->FindComponentByClass<UProjectileMovementComponent>();
        TestTrue(TEXT("The projectile starts at the muzzle and uses the new aim on the same frame"),
            FVector::Dist(Shot->GetActorLocation(), ShotMuzzle.GetLocation()) < 1.0
            && FVector::DotProduct(Flight->Velocity.GetSafeNormal(), ShotMuzzle.GetRotation().GetForwardVector()) > 0.99999);
        TestTrue(TEXT("The next shot uses the live bullet speed setting"),
            FMath::IsNearlyEqual(Flight->Velocity.Size(), Penguin->WeaponDefinition->BulletSpeed * ModeSpeed * 1.35, 1.0));
        Shot->Destroy();
    }
    Penguin->StopFire();
    Penguin->StopAim();
    Penguin->ResetFlat();
    GameState->SetTuning(BulletSpeed, OriginalBulletScale);

    const auto WalkSpeed = EColdLootTuning::WalkSpeed;
    const float OriginalWalkScale = GameState->GetTuning().Get(WalkSpeed);
    const float DefaultWalkSpeed = Penguin->GetGaitSpeed(false) / OriginalWalkScale;
    GameState->SetTuning(WalkSpeed, 1.5f);
    Penguin->MoveForward(1.0f);
    Advance(60);
    TestTrue(TEXT("The next walk reaches the live movement speed setting"),
        FMath::IsNearlyEqual(Penguin->Speed(), DefaultWalkSpeed * 1.5f, 1.0f));
    Penguin->MoveForward(0.0f);
    GameState->SetTuning(WalkSpeed, OriginalWalkScale);
    Penguin->ResetFlat();

    AColdLootHUD* HUD = Cast<AColdLootHUD>(Controller->GetHUD());
    if (TestNotNull(TEXT("The player has the Cold Loot HUD"), HUD))
    {
        Penguin->ToggleTuningMenu();
        TestTrue(TEXT("The tuning menu opens for the solo player"), HUD->IsTuningMenuOpen());
        HUD->ToggleTuningMenu();
        TestFalse(TEXT("The tuning menu resumes gameplay"), HUD->IsTuningMenuOpen());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenguinWaddleVisibilityTest, "ColdLoot.IcebergNetwork.WaddleVisibility",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPenguinWaddleVisibilityTest::RunTest(const FString&)
{
    APenguinPawn* Penguin = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
        {
            APlayerController* Player = Context.World()->GetFirstPlayerController();
            Penguin = Player ? Cast<APenguinPawn>(Player->GetPawn()) : nullptr;
            break;
        }
    }
    if (!TestTrue(TEXT("Player has both waddle clips and a weapon"),
        Penguin && Penguin->WalkWaddleAnimation && Penguin->SprintWaddleAnimation && Penguin->HasWeapon()))
    {
        return false;
    }

    Penguin->ResetFlat();
    Penguin->GroundSpeed = 200.0f;
    const bool bWasVisible = Penguin->WeaponGrip->IsVisible();
    const auto MeasureSway = [Penguin](bool bVisible, float Phase)
    {
        Penguin->WeaponGrip->SetVisibility(bVisible, true);
        const auto SampleRotation = [Penguin](float SamplePhase)
        {
            Penguin->GaitPhase = SamplePhase;
            Penguin->PenguinMesh->TickAnimation(1.0f / 60.0f, false);
            Penguin->PenguinMesh->RefreshBoneTransforms();
            return Penguin->PenguinMesh->GetSocketTransform(TEXT("Root"), RTS_Component).GetRotation();
        };
        const FQuat First = SampleRotation(Phase);
        return FMath::RadiansToDegrees(First.AngularDistance(SampleRotation(Phase + 0.5f)));
    };
    Penguin->SprintBlend = 0.0f;
    TestTrue(TEXT("Walk stays steady with the weapon visible"), MeasureSway(true, 0.3f) < 1.0);
    TestTrue(TEXT("Walk waddles with the weapon hidden"), MeasureSway(false, 0.3f) > 10.0);
    Penguin->SprintBlend = 1.0f;
    TestTrue(TEXT("Sprint stays steady with the weapon visible"), MeasureSway(true, 0.12f) < 1.0);
    TestTrue(TEXT("Sprint waddles with the weapon hidden"), MeasureSway(false, 0.12f) > 10.0);
    Penguin->WeaponGrip->SetVisibility(bWasVisible, true);
    Penguin->ResetFlat();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIcebergNetworkTest, "ColdLoot.IcebergNetwork.Crossing",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FIcebergNetworkTest::RunTest(const FString& Parameters)
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
    if (!TestNotNull(TEXT("Iceberg Network game world is running"), World))
    {
        return false;
    }
    TActorIterator<AIcebergNetworkTerrain> TerrainIt(World);
    APlayerController* Controller = World->GetFirstPlayerController();
    APenguinPawn* Penguin = Controller ? Cast<APenguinPawn>(Controller->GetPawn()) : nullptr;
    if (!TestTrue(TEXT("A penguin is possessed in the island network"), TerrainIt && Penguin
        && Penguin->IslandTerrain() == *TerrainIt && Controller->PlayerInput && Penguin->InputComponent))
    {
        return false;
    }
    const AIcebergNetworkTerrain& Terrain = **TerrainIt;
    TestNotNull(TEXT("Underwater fog is referenced by the level"), Terrain.UnderwaterFog.Get());
    TestNotNull(TEXT("The ocean mesh uses its wave material"), Terrain.OceanMaterial.Get());
    const double FirstWave = Terrain.WaveOffset(8400, 0, 0.0);
    const double LaterWave = Terrain.WaveOffset(8400, 0, 1.0);
    TestTrue(TEXT("The physical waterline undulates in real time"), FMath::Abs(FirstWave - LaterWave) > 3.0
        && FMath::Abs(FirstWave) <= 27.0 && FMath::Abs(LaterWave) <= 27.0);
    auto* HUD = Cast<AColdLootHUD>(Controller->GetHUD());
    auto* Arm = Penguin->FindComponentByClass<USpringArmComponent>();
    if (!TestTrue(TEXT("HUD and camera are available"), HUD && Arm))
    {
        return false;
    }
    TestEqual(TEXT("Both authored icebergs are present on the map"), Terrain.Zones().Num(), 2);
    int32 IndependentLevels = 0;
    for (TActorIterator<AIcebergIsland> It(World); It; ++It)
    {
        const FString Package = It->GetLevel()->GetOutermost()->GetName();
        IndependentLevels += Package.EndsWith(It->bBaseIsland ? TEXT("BaseIsland") : TEXT("ExpeditionIsland"));
    }
    TestEqual(TEXT("Each iceberg is in its own level"), IndependentLevels, 2);
    TestTrue(TEXT("The future bunker site is flat and dry"), Terrain.NormalAt(0, -1200).Z > 0.99
        && Terrain.HeightAt(0, -1200) > 500);
    TestTrue(TEXT("The channel separates both islands"), !Terrain.WaterSurfaceAt(0, 0).IsSet()
        && Terrain.WaterSurfaceAt(8400, 0).IsSet() && !Terrain.WaterSurfaceAt(16800, 0).IsSet());

    const FVector Spawn = Penguin->GetActorLocation();
    TestTrue(TEXT("Spawn is on Base Island"), Spawn.X < 0 && Spawn.Z > 500 && !Penguin->IsSwimming());
    UPlayerInput& Input = *Controller->PlayerInput;
    const TArray<UInputComponent*> Stack{Penguin->InputComponent};
    constexpr float Frame = 1.0f / 60;
    auto Key = [&](const FKey& Button, EInputEvent Event)
    {
        Input.InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Button, Event,
            Event == IE_Pressed ? 1.0f : 0.0f, false, FPlatformTime::Cycles64()));
        Input.ProcessInputStack(Stack, Frame, false);
    };
    auto Tap = [&](const FKey& Button)
    {
        Key(Button, IE_Pressed);
        Key(Button, IE_Released);
    };

    Tap(EKeys::C);
    const float NormalSpeed = Penguin->Speed();
    Tap(EKeys::Q);
    Tap(EKeys::E);
    TestTrue(TEXT("Q and E leave the default slide and materials alone"),
        Penguin->bSliding && NormalSpeed > 0.0f && Penguin->Speed() == NormalSpeed
        && !Penguin->PenguinMesh->GetOverlayMaterial());
    Tap(EKeys::R);

    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzonePlaytestSmoke")))
    {
        Tap(EKeys::F2);
        if (!TestTrue(TEXT("F2 opens the graphics menu and pauses the game"),
            HUD->IsGraphicsMenuOpen() && World->IsPaused()))
        {
            return false;
        }
        auto* Settings = GEngine->GetGameUserSettings();
        auto Click = [&](FName Tag)
        {
            TArray<TSharedRef<SWidget>> Widgets{HUD->GraphicsMenu.ToSharedRef()};
            while (!Widgets.IsEmpty())
            {
                const auto Widget = Widgets.Pop();
                if (Widget->GetTag() == Tag && Widget->GetType() == TEXT("SButton"))
                {
                    StaticCastSharedRef<SButton>(Widget)->SimulateClick();
                    return true;
                }
                FChildren* Children = Widget->GetChildren();
                for (int32 Index = 0; Index < Children->Num(); ++Index)
                {
                    Widgets.Add(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)));
                }
            }
            return false;
        };
        const auto OriginalQuality = Settings->ScalabilityQuality;
        const FIntPoint OriginalResolution = Settings->GetScreenResolution();
        const EWindowMode::Type OriginalMode = Settings->GetFullscreenMode();
        TestTrue(TEXT("The Low graphics button applies the Low preset"),
            Click(TEXT("Low")) && Settings->GetOverallScalabilityLevel() == 0);
        TestTrue(TEXT("The High graphics button applies the High preset"),
            Click(TEXT("High")) && Settings->GetOverallScalabilityLevel() == 2);
        TestTrue(TEXT("The Borderless button enables borderless fullscreen"),
            Click(TEXT("Borderless")) && Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen);
        TestTrue(TEXT("The Windowed button returns to windowed mode"),
            Click(TEXT("Windowed")) && Settings->GetFullscreenMode() == EWindowMode::Windowed);
        Settings->ScalabilityQuality = OriginalQuality;
        Settings->SetScreenResolution(OriginalResolution);
        Settings->SetFullscreenMode(OriginalMode);
        Settings->ApplySettings(false);
        Settings->ConfirmVideoMode();
        Settings->SaveSettings();
        Tap(EKeys::F2);
        TestTrue(TEXT("F2 closes the menu and resumes gameplay"), !HUD->IsGraphicsMenuOpen() && !World->IsPaused());
        return true;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzoneDaylightPreview")))
    {
        ACameraActor* Observer = World->SpawnActor<ACameraActor>(
            Spawn + FVector(-425, 75, 165), FRotator(7.0, -12.0, 0.0));
        Observer->GetCameraComponent()->SetFieldOfView(88.0f);
        Controller->SetViewTarget(Observer);
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([FrameIndex = 0]() mutable
        {
            if (++FrameIndex == 105)
            {
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ProjectSavedDir() / TEXT("Iceberg Network Daylight.png"), false, false);
            }
            return FrameIndex >= 115;
        }));
        return true;
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzoneWeaponFitPreview")))
    {
        if (!TestTrue(TEXT("The player uses the fitted flipper pose"), Penguin->WeaponDefinition
            && Penguin->WeaponDefinition->bUseFittedFlipperTransforms))
        {
            return false;
        }
        const FVector View = Spawn + FVector(250, 435, 155);
        const FRotator Rotation = (Spawn + FVector(32, 0, 92) - View).Rotation();
        ACameraActor* Observer = World->SpawnActor<ACameraActor>(View, Rotation);
        Observer->GetCameraComponent()->SetFieldOfView(48.0f);
        APointLight* Fill = World->SpawnActor<APointLight>(Spawn + FVector(185, 240, 185), FRotator::ZeroRotator);
        UPointLightComponent* Light = CastChecked<UPointLightComponent>(Fill->GetLightComponent());
        Light->SetIntensityUnits(ELightUnits::Lumens);
        Light->SetIntensity(540.0f);
        Light->SetAttenuationRadius(750.0f);
        Light->SetCastShadows(false);
        Controller->SetViewTarget(Observer);
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
            [this, Controller, Penguin, Observer, Fill, HingeAtRest = FVector::ZeroVector, FrameIndex = 0]() mutable
            {
                const FString Folder = FPaths::ProjectSavedDir() / TEXT("AimFitReview");
                const FVector Hinge = Penguin->PenguinMesh->GetSocketTransform(TEXT("Flipper_R"), RTS_ParentBoneSpace)
                    .TransformPosition(Penguin->WeaponDefinition->HoldHingeOnFlipper);
                switch (++FrameIndex)
                {
                case 45:
                    HingeAtRest = Hinge;
                    FScreenshotRequest::RequestScreenshot(Folder / TEXT("Base Island - Fitted Hold.png"), false, false);
                    break;
                case 54:
                    Controller->SetViewTarget(Penguin);
                    Observer->Destroy();
                    Fill->Destroy();
                    Controller->PlayerInput->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0),
                        EKeys::RightMouseButton, IE_Pressed, 1.0f, false, FPlatformTime::Cycles64()));
                    Controller->PlayerInput->ProcessInputStack({Penguin->InputComponent}, 1.0f / 60.0f, false);
                    break;
                case 102:
                    TestTrue(TEXT("The fitted weapon is aimed through the player camera"), Penguin->IsAiming());
                    TestTrue(TEXT("The flipper hinge stays at its attachment through aiming"),
                        Hinge.Equals(HingeAtRest, 0.1));
                    FScreenshotRequest::RequestScreenshot(Folder / TEXT("Base Island - Fitted Aim.png"), false, false);
                    break;
                default:
                    break;
                }
                return FrameIndex >= 112;
            }));
        return true;
    }

    auto TestGlide = [&](bool bSprint)
    {
        Tap(EKeys::R);
        Penguin->SetActorLocation(FVector(-700, -1200, Terrain.HeightAt(-700, -1200) + 1.0));
        if (bSprint)
        {
            Key(EKeys::LeftShift, IE_Pressed);
        }
        Key(EKeys::W, IE_Pressed);
        for (int32 Index = 0; Index < 36; ++Index)
        {
            Penguin->Tick(Frame);
        }
        const double GaitSpeed = Penguin->Speed();
        TestTrue(bSprint ? TEXT("Sprint speed is restored to about 6.4 m/s") : TEXT("Walk speed is about 3.4 m/s"),
            FMath::IsNearlyEqual(GaitSpeed, bSprint ? 643.0 : 341.0, 10.0));
        Tap(EKeys::C);
        const double EntrySpeed = Penguin->Speed();
        TestTrue(TEXT("The moving slide boosts as soon as C is pressed"), EntrySpeed > GaitSpeed + 1500.0);
        Key(EKeys::W, IE_Released);
        if (bSprint)
        {
            Key(EKeys::LeftShift, IE_Released);
        }
        for (int32 Index = 0; Index < 90; ++Index)
        {
            Penguin->Tick(Frame);
        }
        return FVector2D(EntrySpeed, Penguin->Speed());
    };
    const FVector2D SlowGlide = TestGlide(false);
    const FVector2D FastGlide = TestGlide(true);
    TestTrue(TEXT("The sprint entry gives a stronger boost and settles into a fast flat glide"),
        FastGlide.X > SlowGlide.X && FastGlide.Y > 2000.0 && FastGlide.Y < FastGlide.X * 0.85);
    AddInfo(FString::Printf(TEXT("Slide after 1.5 seconds: walk %.0f to %.0f, sprint %.0f to %.0f cm/s"),
        SlowGlide.X, SlowGlide.Y, FastGlide.X, FastGlide.Y));
    Tap(EKeys::R);
    Key(EKeys::LeftShift, IE_Pressed);
    Key(EKeys::W, IE_Pressed);
    auto Cross = [&](bool bReturning)
    {
        int32 SwimFrames = 0;
        bool bReachedLand = false;
        double MaxDepth = 0.0;
        double CruiseSpeed = 0.0;
        for (int32 Index = 0; Index < 3600; ++Index)
        {
            Penguin->Tick(Frame);
            const FVector Position = Penguin->GetActorLocation();
            if (Penguin->IsSwimming())
            {
                ++SwimFrames;
                MaxDepth = FMath::Max(MaxDepth, Penguin->GetSwimDepth());
                if (SwimFrames > 42)
                {
                    CruiseSpeed = FMath::Max(CruiseSpeed, static_cast<double>(Penguin->Speed()));
                }
            }
            else if (SwimFrames > 0 && (bReturning ? Position.X < 6400 : Position.X > 11700))
            {
                bReachedLand = true;
                break;
            }
        }
        TestTrue(bReturning ? TEXT("Penguin swims back and walks onto Base Island")
                            : TEXT("Penguin swims across and walks onto Expedition Island"), bReachedLand && MaxDepth < 80);
        TestTrue(TEXT("The faster swim cruises near 19 m/s and clears the channel in under 2.5 seconds"),
            SwimFrames > 50 && SwimFrames < 150 && CruiseSpeed > 1750 && CruiseSpeed < 2150);
        AddInfo(FString::Printf(TEXT("Swim crossing took %.1f seconds, max depth %.1f cm, peak cruising speed %.0f cm/s"),
            SwimFrames * Frame, MaxDepth, CruiseSpeed));
    };
    Cross(false);
    Penguin->ResetAt(13300.0);
    Arm->SetRelativeRotation(FRotator(-9, 180, 0));
    Cross(true);
    Key(EKeys::W, IE_Released);
    Key(EKeys::LeftShift, IE_Released);
    Tap(EKeys::R);
    TestTrue(TEXT("R returns to the base spawn"), Penguin->GetActorLocation().Equals(Spawn, 1.0));

    auto Advance = [&](int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Penguin->Tick(Frame);
            Arm->TickComponent(Frame, LEVELTICK_All, nullptr);
        }
    };
    for (bool bSprint : {false, true})
    {
        Penguin->ResetAt(-700.0, -1200.0);
        Key(EKeys::W, IE_Pressed);
        if (bSprint)
        {
            Key(EKeys::LeftShift, IE_Pressed);
        }
        Advance(36);
        Key(EKeys::D, IE_Pressed);
        Advance(2);
        const double Turn = Penguin->Heading.Rotation().Yaw;
        TestTrue(bSprint ? TEXT("A sprinting turn covers fifteen degrees in two frames")
            : TEXT("A walking turn covers thirty degrees in two frames"),
            FMath::IsNearlyEqual(Turn, bSprint ? 15.0 : 30.0, 0.1));
        Key(EKeys::W, IE_Released);
        Key(EKeys::D, IE_Released);
        if (bSprint)
        {
            Key(EKeys::LeftShift, IE_Released);
        }
    }
    for (bool bTestSlide : {false, true})
    {
        Tap(EKeys::R);
        Penguin->ResetAt(-3100.0, -1400.0);
        Arm->SetRelativeRotation(FRotator(-9.0, 90.0, 0));
        Key(EKeys::W, IE_Pressed);
        Key(EKeys::LeftShift, IE_Pressed);
        Advance(bTestSlide ? 23 : 45);
        if (bTestSlide)
        {
            Tap(EKeys::C);
        }
        TestTrue(TEXT("The grounding check starts in the requested movement state"),
            bTestSlide ? Penguin->bSliding : Penguin->bSprinting && !Penguin->bSliding);
        auto& SprintMesh = *Penguin->PenguinMesh;
        double LowestTorso = 1000.0;
        double LowestHead = 1000.0;
        double WorstGrade = 0.0;
        const int32 SampleFrames = bTestSlide ? 83 : 145;
        for (int32 Index = 0; Index < SampleFrames; ++Index)
        {
            if (!bTestSlide && Index == 120)
            {
                Key(EKeys::LeftShift, IE_Released);
            }
            Advance(1);
            SprintMesh.TickAnimation(Frame, false);
            SprintMesh.RefreshBoneTransforms();
            if (Index % 8 != 0 || Penguin->bSlideAirborne || Penguin->IsSwimming())
            {
                continue;
            }
            const FVector Torso = SprintMesh.GetSocketLocation(TEXT("Body_Jiggle"));
            LowestTorso = FMath::Min(LowestTorso, Torso.Z - Terrain.HeightAt(Torso.X, Torso.Y));
            const FVector Head = SprintMesh.GetSocketLocation(TEXT("Head_Slide"));
            LowestHead = FMath::Min(LowestHead, Head.Z - Terrain.HeightAt(Head.X, Head.Y));
            WorstGrade = FMath::Max(WorstGrade, FMath::Abs(Penguin->Grade()));
        }
        AddInfo(FString::Printf(TEXT("%s on %.1f degree slope, lowest torso %.1f cm, lowest head %.1f cm"),
            bTestSlide ? TEXT("Sprint slide") : TEXT("Sprint"), WorstGrade, LowestTorso, LowestHead));
        TestTrue(bTestSlide ? TEXT("A sprint slide keeps the body above the snow")
            : TEXT("Sprinting and returning to a walk keep the torso and head clear of the snow"),
            WorstGrade > 15.0 && LowestTorso > 25.0 && LowestHead > (bTestSlide ? 40.0 : 65.0));
        Key(EKeys::LeftShift, IE_Released);
        Key(EKeys::W, IE_Released);
    }
    auto PutInChannel = [&](double Y = 0.0)
    {
        Tap(EKeys::R);
        Penguin->SetActorLocation(FVector(8400, Y, -18));
        Advance(1);
    };
    PutInChannel();
    Key(EKeys::W, IE_Pressed);
    Advance(38);
    const double SurfaceDepth = Penguin->GetSwimDepth();
    TestTrue(TEXT("The level paddle keeps the body low at the surface"),
        Penguin->IsSwimming() && SurfaceDepth > 30 && SurfaceDepth < 65);
    USkeletalMeshComponent* Mesh = Penguin->PenguinMesh;
    Mesh->TickAnimation(.22f, false);
    Mesh->RefreshBoneTransforms();
    const USkeletalMesh* SwimMesh = Mesh->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* RenderData = SwimMesh ? SwimMesh->GetResourceForRendering() : nullptr;
    double BeakClearance = -1000.0;
    if (RenderData && RenderData->LODRenderData.Num() > 0)
    {
        TArray<FFinalSkinVertex> Vertices;
        Mesh->GetCPUSkinnedVertices(Vertices, 0);
        for (const FSkelMeshRenderSection& Section : RenderData->LODRenderData[0].RenderSections)
        {
            if (!SwimMesh->GetMaterials().IsValidIndex(Section.MaterialIndex)
                || !SwimMesh->GetMaterials()[Section.MaterialIndex].MaterialSlotName.ToString().Contains(TEXT("Beak"))
                || Section.BaseVertexIndex + Section.NumVertices > static_cast<uint32>(Vertices.Num()))
            {
                continue;
            }
            for (uint32 Index = Section.BaseVertexIndex; Index < Section.BaseVertexIndex + Section.NumVertices; ++Index)
            {
                const FVector Point = Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[Index].Position));
                BeakClearance = FMath::Max(BeakClearance, Point.Z - Terrain.WaterSurfaceAt(Point.X, Point.Y).Get(0.0));
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Beak clears the waves by %.2f m during a level swim"), BeakClearance / 100.0));
    TestTrue(TEXT("The animated beak reaches the waterline during a level swim"),
        BeakClearance > -12.0 && BeakClearance < 19.0);
    Key(EKeys::W, IE_Released);
    const FVector BeforeSinking = Penguin->GetActorLocation();
    Advance(120);
    AddInfo(FString::Printf(TEXT("Idle sinking: %.2f m deeper, %.2f m of forward coast"),
        (Penguin->GetSwimDepth() - SurfaceDepth) / 100.0,
        (Penguin->GetActorLocation().X - BeforeSinking.X) / 100.0));
    TestTrue(TEXT("Releasing W sinks below the surface without stopping immediately"),
        Penguin->IsSwimming() && Penguin->GetSwimDepth() > SurfaceDepth + 80
        && Penguin->GetActorLocation().X > BeforeSinking.X + 50);
    PutInChannel(4200.0);
    Arm->SetRelativeRotation(FRotator(-60, 90, 0));
    Key(EKeys::W, IE_Pressed);
    Advance(145);
    Key(EKeys::W, IE_Released);
    const FVector Bottom = Penguin->GetActorLocation();
    const auto* Camera = Penguin->FindComponentByClass<UCameraComponent>();
    AddInfo(FString::Printf(TEXT("Dive reached %s, camera elevation %.0f cm and treatment %.2f"),
        *Bottom.ToCompactString(), Camera ? Camera->GetComponentLocation().Z : 0.0,
        Camera ? Camera->PostProcessBlendWeight : -1.0f));
    TestTrue(TEXT("Pointing down and pushing forward dives along the body angle"), Penguin->IsSwimming()
        && Bottom.Z < -500 && Bottom.Y > 4800.0
        && Penguin->PenguinMesh->GetAttachParent()->GetForwardVector().Z < -.70);
    TestTrue(TEXT("Diving cannot push the penguin through the seabed"),
        Bottom.Z >= Terrain.HeightAt(Bottom.X, Bottom.Y) + 36);
    TestTrue(TEXT("The submerged camera applies the water treatment"), Camera && Camera->PostProcessBlendWeight > 0.8f);
    Advance(160);
    const FVector BeforeStrafe = Penguin->GetActorLocation();
    Key(EKeys::A, IE_Pressed);
    Advance(40);
    Key(EKeys::A, IE_Released);
    TestTrue(TEXT("A alone adds no sideways thrust underwater"),
        (Penguin->GetActorLocation() - BeforeStrafe).Size2D() < 20.0);

    Arm->SetRelativeRotation(FRotator(20, -90, 0));
    Key(EKeys::W, IE_Pressed);
    Advance(250);
    AddInfo(FString::Printf(TEXT("Resurface: %.2f m depth, %.2f camera effect, nose direction %s"),
        Penguin->GetSwimDepth() / 100.0, Camera ? Camera->PostProcessBlendWeight : -1.0f,
        *Penguin->PenguinMesh->GetAttachParent()->GetForwardVector().ToCompactString()));
    TestTrue(TEXT("A gradual climb puts the beak at the waterline"), Penguin->IsSwimming()
        && Penguin->GetSwimDepth() > 95 && Penguin->GetSwimDepth() < 130
        && Camera && Camera->PostProcessBlendWeight < 0.08f);
    Key(EKeys::W, IE_Released);

    PutInChannel();
    Arm->SetRelativeRotation(FRotator(-48, 90, 0));
    Key(EKeys::W, IE_Pressed);
    Advance(65);
    Arm->SetRelativeRotation(FRotator(57, 90, 0));
    double BreachHeight = 0.0;
    bool bLaunched = false;
    bool bReturnedToWater = false;
    for (int32 Index = 0; Index < 360; ++Index)
    {
        Advance(1);
        if (!Penguin->IsSwimming())
        {
            if (!bLaunched)
            {
                Key(EKeys::W, IE_Released);
                bLaunched = true;
            }
            const FVector Position = Penguin->GetActorLocation();
            BreachHeight = FMath::Max(BreachHeight, Position.Z - Terrain.WaterSurfaceAt(Position.X, Position.Y).Get(0.0));
        }
        else if (bLaunched)
        {
            bReturnedToWater = true;
            break;
        }
    }
    Key(EKeys::W, IE_Released);
    AddInfo(FString::Printf(TEXT("Breach cleared the wave by %.2f m"), BreachHeight / 100.0));
    TestTrue(TEXT("A fast upward swim breaches the waves and falls back into the channel"),
        bLaunched && BreachHeight > 75.0 && bReturnedToWater);

    if (!TestTrue(TEXT("The powered swim still uses the dedicated clip"), Mesh && Penguin->SwimAnimation
        && Penguin->SwimAnimation != Penguin->SlideAnimation && Penguin->SwimAnimation->GetPlayLength() > .75f))
    {
        return false;
    }
    auto PoseTick = [&]()
    {
        Advance(1);
        Mesh->TickAnimation(Frame, false);
        Mesh->RefreshBoneTransforms();
    };
    const FName Flipper(TEXT("Flipper_L"));
    const FQuat FlipperStart = Mesh->GetSocketTransform(Flipper, RTS_Component).GetRotation();
    double StrokeArc = 0.0;
    Key(EKeys::W, IE_Pressed);
    for (int32 Index = 0; Index < 51; ++Index)
    {
        PoseTick();
        StrokeArc = FMath::Max(StrokeArc,
            FlipperStart.AngularDistance(Mesh->GetSocketTransform(Flipper, RTS_Component).GetRotation()));
    }
    Key(EKeys::W, IE_Released);
    TestTrue(TEXT("Forward thrust still animates a flipper stroke"), StrokeArc > FMath::DegreesToRadians(38.0));

    Tap(EKeys::R);
    Tap(EKeys::C);
    Key(EKeys::RightMouseButton, IE_Pressed);
    TestTrue(TEXT("ADS immediately interrupts a grounded slide"), Penguin->IsAiming() && !Penguin->bSliding);
    Key(EKeys::RightMouseButton, IE_Released);
    Tap(EKeys::C);
    Tap(EKeys::SpaceBar);
    TestTrue(TEXT("An immediate boost stays grounded"), Penguin->Speed() > 0.0f && !Penguin->bSlideAirborne);
    Advance(10);
    Tap(EKeys::SpaceBar);
    TestTrue(TEXT("The first 0.2 seconds after the boost keep it grounded"),
        Penguin->SlideGroundLockRemaining > 0.0f && !Penguin->bSlideAirborne
        && !Penguin->TrySlideLaunch(Frame, Penguin->GetActorLocation(), FVector::ForwardVector, 0.6));
    Advance(3);
    Tap(EKeys::SpaceBar);
    TestTrue(TEXT("Space jumps without dropping the slide pose"),
        Penguin->bSliding && Penguin->bSlideAirborne && Penguin->SlideVerticalSpeed > 0.0);
    const double JumpLift = Penguin->SlideVerticalSpeed;
    Advance(1);
    TestTrue(TEXT("Jumping uses the 20 percent higher gravity of 1176 cm/s squared"),
        FMath::IsNearlyEqual(JumpLift - Penguin->SlideVerticalSpeed, 1176.0 * Frame, 0.01));
    const float JumpSpeed = Penguin->Speed();
    Key(EKeys::LeftMouseButton, IE_Pressed);
    TestTrue(TEXT("Fire cancels an airborne slide while preserving the jump"),
        Penguin->CanFire() && !Penguin->bSliding && Penguin->bSlideAirborne
        && Penguin->Speed() == JumpSpeed && Penguin->ShotRequestTime > 0.0f);
    Key(EKeys::LeftMouseButton, IE_Released);
    Tap(EKeys::R);

    const FVector AirStart = Penguin->GetActorLocation();
    Tap(EKeys::SpaceBar);
    Key(EKeys::D, IE_Pressed);
    Advance(14);
    Key(EKeys::D, IE_Released);
    const FVector SteeredPosition = Penguin->GetActorLocation();
    const float SteeredSpeed = Penguin->Speed();
    Advance(5);
    TestTrue(TEXT("Air control builds sideways momentum and coasts when released"),
        Penguin->bSlideAirborne && SteeredPosition.Y > AirStart.Y + 20.0
        && Penguin->GetActorLocation().Y > SteeredPosition.Y + 10.0
        && FMath::IsNearlyEqual(Penguin->Speed(), SteeredSpeed, 0.1f));
    Tap(EKeys::R);

    Key(EKeys::RightMouseButton, IE_Pressed);
    Key(EKeys::D, IE_Pressed);
    Arm->SetRelativeRotation(FRotator(-9, 30, 0));
    Advance(15);
    TestTrue(TEXT("ADS faces the camera aim and strafes without leaning into the strafe"),
        FVector::DotProduct(Penguin->Facing, Penguin->GetLookYaw().Vector()) > 0.99
        && FMath::Abs(FVector::DotProduct(Penguin->Heading, Penguin->Facing)) < 0.02
        && FMath::IsNearlyZero(Penguin->TurnLean));
    Key(EKeys::D, IE_Released);
    Key(EKeys::RightMouseButton, IE_Released);

    for (const FKey& Button : {EKeys::A, EKeys::D})
    {
        Penguin->ResetAt(-700.0, -1200.0);
        Tap(EKeys::C);
        Advance(22);
        Penguin->GroundSpeed = 0.0f;
        Key(Button, IE_Pressed);
        double Turn = 0.0;
        constexpr int32 TurnFrames = 48;
        for (int32 Index = 0; Index < TurnFrames; ++Index)
        {
            const double Before = Penguin->Heading.Rotation().Yaw;
            Advance(1);
            Turn += FMath::FindDeltaAngleDegrees(Before, Penguin->Heading.Rotation().Yaw);
        }
        Key(Button, IE_Released);
        TestTrue(Button == EKeys::A ? TEXT("Holding A keeps steering past 90 degrees left")
            : TEXT("Holding D keeps steering past 90 degrees right"),
            FMath::IsNearlyEqual(Turn, (Button == EKeys::A ? -130.0 : 130.0) * TurnFrames * Frame, 0.1));
        TestTrue(TEXT("A held turn banks 20 degrees in its direction around the ground origin"),
            FMath::IsNearlyEqual(Penguin->TurnLean, Button == EKeys::A ? -20.0f : 20.0f, 0.1f)
            && Penguin->BodyPivot->GetComponentLocation().Equals(Penguin->GetActorLocation(), 0.1));
    }

    for (const double Yaw : {0.0, -90.0, 180.0})
    {
        Penguin->ResetAt(-700.0, -1200.0);
        Tap(EKeys::C);
        Advance(22);
        Arm->SetRelativeRotation(FRotator(-9.0, Yaw, 0.0));
        const float CoastSpeed = Penguin->Speed();
        Key(EKeys::W, IE_Pressed);
        Key(EKeys::S, IE_Pressed);
        Advance(30);
        Key(EKeys::S, IE_Released);
        Key(EKeys::W, IE_Released);
        TestTrue(TEXT("S brakes the slide regardless of camera angle"),
            Penguin->bSliding && Penguin->Speed() < CoastSpeed - 750.0f);
    }

    double RunY = -1400.0;
    for (double Y = -1300.0; Y <= 1200.0; Y += 100.0)
    {
        if (Terrain.NormalAt(-3100.0, Y).Z < Terrain.NormalAt(-3100.0, RunY).Z)
        {
            RunY = Y;
        }
    }
    Penguin->ResetAt(-3100.0, RunY);
    Penguin->Facing = Terrain.NormalAt(-3100.0, RunY).GetSafeNormal2D();
    Tap(EKeys::C);
    Advance(22);
    const float DownhillEntrySpeed = Penguin->Speed();
    Advance(12);
    TestTrue(TEXT("A slide gains speed from a downhill grade without throttle"),
        Penguin->bSliding && Penguin->Speed() > DownhillEntrySpeed + 30.0f);

    auto* Ring = FindObjectFast<UProceduralMeshComponent>(TerrainIt.operator->(), TEXT("PracticeRing"));
    const bool bHasRing = TestTrue(TEXT("The channel has a visible nonblocking practice ring"),
        Ring && Ring->GetProcMeshSection(0) && Ring->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    for (TActorIterator<AIcebergIsland> It(World); It; ++It)
    {
        auto* Ramp = FindObjectFast<UProceduralMeshComponent>(*It, TEXT("PracticeJumpRamp"));
        const auto* Section = Ramp ? Ramp->GetProcMeshSection(0) : nullptr;
        if (!TestNotNull(TEXT("The island has a practice ramp"), Section))
        {
            continue;
        }
        const FVector Lip = Ramp->GetComponentTransform().TransformPosition(
            (Section->ProcVertexBuffer[2].Position + Section->ProcVertexBuffer[3].Position) * .5);
        const double Direction = It->bBaseIsland ? 1.0 : -1.0;
        const FVector RampTop = Lip - FVector(Direction * 280.0, 0, 0);
        FHitResult Hit;
        TestTrue(TEXT("The visible ramp matches the walkable surface"),
            World->LineTraceSingleByChannel(Hit, RampTop + FVector::UpVector * 400.0,
                RampTop - FVector::UpVector * 400.0, ECC_Visibility) && Hit.GetComponent() == Ramp
            && FMath::IsNearlyEqual(Hit.ImpactPoint.Z, Terrain.HeightAt(RampTop.X, RampTop.Y), 2.0));

        const FVector Origin = Terrain.GetActorLocation();
        Penguin->ResetAt(Lip.X - Origin.X - Direction * 4200.0, Lip.Y - Origin.Y);
        Arm->SetRelativeRotation(FRotator(-9.0, It->bBaseIsland ? 0.0 : 180.0, 0.0));
        Key(EKeys::W, IE_Pressed);
        Key(EKeys::LeftShift, IE_Pressed);
        Advance(45);
        Tap(EKeys::C);
        for (int32 Index = 0; Index < 400 && (Lip.X - Penguin->GetActorLocation().X) * Direction > 175.0
            && !Penguin->bSlideAirborne && !Penguin->IsSwimming(); ++Index)
        {
            Advance(1);
        }
        Tap(EKeys::SpaceBar);
        const FVector Launch = Penguin->GetActorLocation();
        const float LaunchSpeed = Penguin->Speed();
        const double LaunchLift = Penguin->SlideVerticalSpeed;
        Key(EKeys::W, IE_Released);
        Key(EKeys::LeftShift, IE_Released);
        double RingClearance = TNumericLimits<double>::Max();
        for (int32 Index = 0; Index < 330 && Penguin->bSlideAirborne; ++Index)
        {
            const FVector Before = Penguin->GetActorLocation();
            Advance(1);
            if (bHasRing)
            {
                const FTransform RingTransform = Ring->GetComponentTransform();
                const FVector From = RingTransform.InverseTransformPosition(Before);
                const FVector To = RingTransform.InverseTransformPosition(Penguin->GetActorLocation());
                if (From.X * To.X <= 0.0 && !FMath::IsNearlyEqual(From.X, To.X))
                {
                    const FVector Through = FMath::Lerp(From, To, From.X / (From.X - To.X));
                    RingClearance = FVector2D(Through.Y, Through.Z).Size();
                }
            }
        }
        const FVector Landing = Penguin->GetActorLocation();
        const bool bLandedAcross = It->bBaseIsland ? Landing.X > 8400 : Landing.X < 8400;
        TestTrue(It->bBaseIsland ? TEXT("A slide jump flies from Base to Expedition Island")
            : TEXT("A slide jump flies from Expedition to Base Island"),
            LaunchSpeed > 1800.0f && bLandedAcross && !Penguin->IsSwimming()
            && Penguin->bSliding && !Penguin->bSlideAirborne
            && Penguin->WaterSkips == 0 && !Terrain.WaterSurfaceAt(Landing.X, Landing.Y));
        TestTrue(TEXT("The ramp flight passes through the ring opening"), RingClearance < 340.0);
        AddInfo(FString::Printf(TEXT("Ramp %s lip X %.0f, flew from X %.0f to %.0f, launch velocity %.0f forward / %.0f up, ring offset %.0f cm"),
            *It->IslandName, Lip.X, Launch.X, Landing.X, LaunchSpeed, LaunchLift, RingClearance));
    }

    auto ChannelApproach = [&](float ForwardSpeed, double FallSpeed)
    {
        const FVector2D Target(8400, -2000);
        const double Surface = Terrain.WaterSurfaceAt(Target.X, Target.Y).GetValue();
        Tap(EKeys::R);
        Penguin->SetActorLocation(FVector(Target, Surface + 10.0));
        Penguin->GroundSpeed = ForwardSpeed;
        Penguin->bSliding = Penguin->bSlideAirborne = true;
        Penguin->SlideVerticalSpeed = -FallSpeed;
    };
    ChannelApproach(3000.0f, 100.0);
    Penguin->bSliding = false;
    Penguin->Heading = FVector::RightVector;
    Penguin->AddActorWorldOffset(FVector(0, 0, 250));
    Tap(EKeys::C);
    TestTrue(TEXT("C starts a midair dive without an instant pitch or velocity change"),
        Penguin->bDiving && Penguin->bSlideAirborne && Penguin->SlideVerticalSpeed == -100.0
        && FMath::IsNearlyZero(Penguin->DivePitch) && FMath::IsNearlyZero(Penguin->SlidePoseTime));
    Advance(10);
    TestTrue(TEXT("The dive pitches down at a constant rate"),
        FMath::IsNearlyEqual(Penguin->DivePitch, -40.0f, 0.1f));
    Advance(14);
    TestTrue(TEXT("The dive stops rotating when the penguin faces straight down"),
        Penguin->bDiving && FMath::IsNearlyEqual(Penguin->DivePitch, -90.0f, 0.1f)
        && Penguin->BodyPivot->GetForwardVector().Equals(-FVector::UpVector, 0.001));
    for (int32 Index = 0; Index < 30 && !Penguin->IsSwimming(); ++Index)
    {
        PoseTick();
    }
    const FTransform EntryHead = Mesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component);
    const FQuat EntryBody = Penguin->BodyPivot->GetComponentQuat();
    for (int32 Index = 0; Index < 21; ++Index)
    {
        PoseTick();
    }
    TestTrue(TEXT("A dive enters without skipping and holds its entry pose for the first 0.4 seconds"),
        Penguin->IsHoldingWaterEntryPose() && Penguin->WaterSkips == 0
        && Mesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component).Equals(EntryHead, 0.01)
        && Penguin->BodyPivot->GetComponentQuat().Equals(EntryBody, 0.01));
    for (int32 Index = 0; Index < 18; ++Index)
    {
        PoseTick();
    }
    TestTrue(TEXT("The held entry pose blends into swimming"),
        Penguin->IsSwimming() && !Penguin->IsHoldingWaterEntryPose()
        && !Mesh->GetSocketTransform(TEXT("Head_Slide"), RTS_Component).Equals(EntryHead, 0.1));

    for (const int32 ExpectedSkips : {1, 2})
    {
        ChannelApproach(ExpectedSkips == 1 ? 1000.0f : 1600.0f, 100.0);
        for (int32 Index = 0; Index < 140 && !Penguin->IsSwimming(); ++Index)
        {
            Advance(1);
        }
        TestTrue(ExpectedSkips == 1 ? TEXT("A shallow approach near the speed threshold skips once")
            : TEXT("A faster shallow approach skips twice before settling into swimming"),
            Penguin->WaterSkips == ExpectedSkips && Penguin->IsSwimming());
    }

    ChannelApproach(1600.0f, 650.0);
    for (int32 Index = 0; Index < 15 && !Penguin->IsSwimming(); ++Index)
    {
        Advance(1);
    }
    const double EntrySpeed = Penguin->Speed();
    TestTrue(TEXT("A steep entry keeps its momentum at contact and does not skip"),
        Penguin->IsSwimming() && Penguin->WaterSkips == 0 && Penguin->SwimVelocity.X > 1500.0);
    Advance(6);
    TestTrue(TEXT("The first tenth of a second in water preserves most entry speed"), Penguin->Speed() > EntrySpeed * .80);
    Advance(42);
    TestTrue(TEXT("Water drag then slows the swimmer"), Penguin->Speed() < EntrySpeed * .66);

    const bool bDivePreview = FParse::Param(FCommandLine::Get(), TEXT("ColdzoneSwimPreview"));
    if (bDivePreview || FParse::Param(FCommandLine::Get(), TEXT("ColdzoneSurfacePreview")))
    {
        PutInChannel();
        Arm->SetRelativeRotation(FRotator(bDivePreview ? -64 : -13, bDivePreview ? -25 : 12, 0));
        if (bDivePreview)
        {
            Key(EKeys::W, IE_Pressed);
            for (int32 Index = 0; Index < 77; ++Index)
            {
                PoseTick();
            }
            Key(EKeys::W, IE_Released);
        }
        else
        {
            Key(EKeys::W, IE_Pressed);
            for (int32 Index = 0; Index < 38; ++Index)
            {
                PoseTick();
            }
            Key(EKeys::W, IE_Released);
        }
        const TCHAR* Preview = bDivePreview ? TEXT("ColdzoneSwimPreview.png") : TEXT("ColdzoneSurfacePreview.png");
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / Preview, false, false);
        return true;
    }
    Tap(EKeys::R);
    Tap(EKeys::M);
    TestTrue(TEXT("M opens the two-island map"), HUD->IsIslandMapOpen());
    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzoneNetworkPreview")))
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("ColdzoneIcebergNetworkMap.png"), false, false);
    }
    return true;
}

#endif
