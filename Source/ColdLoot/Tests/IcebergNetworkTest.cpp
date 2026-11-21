#if WITH_DEV_AUTOMATION_TESTS

#include "../ColdLootGameMode.h"
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
    Penguin->StartFire();
    Advance(1);
    const FTransform ShotMuzzle = Penguin->WeaponReceiver->GetSocketTransform(MuzzleName);
    if (TActorIterator<AColdLootBullet> Shot(World); TestTrue(TEXT("A shot launches"), static_cast<bool>(Shot)))
    {
        const auto* Flight = Shot->FindComponentByClass<UProjectileMovementComponent>();
        TestTrue(TEXT("The projectile starts at the muzzle and uses the new aim on the same frame"),
            FVector::Dist(Shot->GetActorLocation(), ShotMuzzle.GetLocation()) < 1.0
            && FVector::DotProduct(Flight->Velocity.GetSafeNormal(), ShotMuzzle.GetRotation().GetForwardVector()) > 0.99999);
        Shot->Destroy();
    }
    Penguin->StopFire();
    Penguin->StopAim();
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

    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzonePlaytestSmoke")))
    {
        Tap(EKeys::C);
        const float NormalSpeed = Penguin->Speed();
        Tap(EKeys::Q);
        const float BlueSpeed = Penguin->Speed();
        TestTrue(TEXT("Q activates the blue overlay and increases slide speed"),
            Penguin->IsPenguinFormActive() && !Penguin->IsEditorPenguinFormActive()
            && Penguin->PenguinMesh->GetOverlayMaterial()
            && Penguin->PenguinMesh->GetOverlayMaterial()->GetFName() == TEXT("M_PenguinForm")
            && NormalSpeed > 0.0f && BlueSpeed > NormalSpeed);
        Tap(EKeys::E);
        TestTrue(TEXT("E activates the red overlay and a stronger slide boost"),
            Penguin->IsEditorPenguinFormActive()
            && Penguin->PenguinMesh->GetOverlayMaterial()
            && Penguin->PenguinMesh->GetOverlayMaterial()->GetFName() == TEXT("M_EditorPenguinForm")
            && Penguin->Speed() > BlueSpeed);
        AddInfo(FString::Printf(TEXT("Slide speeds in normal, blue and red forms: %.0f / %.0f / %.0f cm/s"),
            NormalSpeed, BlueSpeed, Penguin->Speed()));
        Tap(EKeys::E);
        Tap(EKeys::R);

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
        const double EntrySpeed = Penguin->Speed();
        Tap(EKeys::C);
        TestTrue(TEXT("A moving slide starts with the current speed"),
            EntrySpeed > 65.0 && FMath::IsNearlyEqual(Penguin->Speed(), EntrySpeed, 1.0));
        Key(EKeys::W, IE_Released);
        if (bSprint)
        {
            Key(EKeys::LeftShift, IE_Released);
        }
        for (int32 Index = 0; Index < 30; ++Index)
        {
            Penguin->Tick(Frame);
        }
        return FVector2D(EntrySpeed, Penguin->Speed());
    };
    const FVector2D SlowGlide = TestGlide(false);
    const FVector2D FastGlide = TestGlide(true);
    TestTrue(TEXT("A fast slide loses less speed to friction than a slow slide on flat snow"),
        FastGlide.X > SlowGlide.X && FastGlide.Y > FastGlide.X * 0.88
        && FastGlide.X - FastGlide.Y < SlowGlide.X - SlowGlide.Y
        && FastGlide.Y < FastGlide.X);
    AddInfo(FString::Printf(TEXT("Slide after half a second: walk %.0f to %.0f, sprint %.0f to %.0f cm/s"),
        SlowGlide.X, SlowGlide.Y, FastGlide.X, FastGlide.Y));
    Tap(EKeys::R);
    Key(EKeys::LeftShift, IE_Pressed);
    Key(EKeys::W, IE_Pressed);
    auto Cross = [&](bool bReturning)
    {
        int32 SwimFrames = 0;
        bool bReachedLand = false;
        bool bStayedNearSurface = true;
        for (int32 Index = 0; Index < 3600; ++Index)
        {
            Penguin->Tick(Frame);
            const FVector Position = Penguin->GetActorLocation();
            if (Penguin->IsSwimming())
            {
                ++SwimFrames;
                bStayedNearSurface &= Penguin->GetSwimDepth() < 80 && Penguin->Speed() <= 1020;
            }
            else if (SwimFrames > 0 && (bReturning ? Position.X < 6400 : Position.X > 11700))
            {
                bReachedLand = true;
                break;
            }
        }
        TestTrue(bReturning ? TEXT("Penguin swims back and walks onto Base Island")
                            : TEXT("Penguin swims across and walks onto Expedition Island"), bReachedLand && bStayedNearSurface);
        TestTrue(TEXT("Direct swim is a short crossing"), SwimFrames > 100 && SwimFrames < 650);
        AddInfo(FString::Printf(TEXT("Swim crossing took %.1f seconds"), SwimFrames * Frame));
    };
    Cross(false);
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
    auto PutInChannel = [&]()
    {
        Tap(EKeys::R);
        Penguin->SetActorLocation(FVector(8400, 0, -18));
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
    PutInChannel();
    Arm->SetRelativeRotation(FRotator(-70, 0, 0));
    Key(EKeys::W, IE_Pressed);
    Advance(145);
    Key(EKeys::W, IE_Released);
    const FVector Bottom = Penguin->GetActorLocation();
    const auto* Camera = Penguin->FindComponentByClass<UCameraComponent>();
    AddInfo(FString::Printf(TEXT("Dive reached %s, camera treatment %.2f"), *Bottom.ToCompactString(),
        Camera ? Camera->PostProcessBlendWeight : -1.0f));
    TestTrue(TEXT("Pointing down and pushing forward dives along the body angle"), Penguin->IsSwimming()
        && Bottom.Z < -500 && Bottom.X > 8700
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
    Arm->SetRelativeRotation(FRotator(-48, 0, 0));
    Key(EKeys::W, IE_Pressed);
    Advance(65);
    Arm->SetRelativeRotation(FRotator(57, 0, 0));
    double BreachHeight = 0.0;
    bool bLaunched = false;
    bool bReturnedToWater = false;
    for (int32 Index = 0; Index < 205; ++Index)
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
    double ShoreX = 0.0;
    for (; ShoreX < 14000 && !Terrain.WaterSurfaceAt(ShoreX, 0); ShoreX += 50.0)
    {
    }
    const double RunStart = ShoreX - 950.0;
    Penguin->SetActorLocation(FVector(RunStart, 0, Terrain.HeightAt(RunStart, 0) + 1.0));
    Key(EKeys::LeftShift, IE_Pressed);
    Key(EKeys::W, IE_Pressed);
    Advance(39);
    Tap(EKeys::Q);
    Tap(EKeys::C);
    for (int32 Index = 0; Index < 180 && !Penguin->IsSwimming(); ++Index)
    {
        Advance(1);
    }
    const double EntrySpeed = Penguin->Speed();
    Key(EKeys::W, IE_Released);
    Key(EKeys::LeftShift, IE_Released);
    double Plunge = Penguin->GetSwimDepth();
    for (int32 Index = 0; Index < 44; ++Index)
    {
        Advance(1);
        Plunge = FMath::Max(Plunge, Penguin->GetSwimDepth());
    }
    AddInfo(FString::Printf(TEXT("Fast entry: %.1f m/s on contact, %.1f m/s after drag, %.2f m peak depth"),
        EntrySpeed / 100.0, Penguin->Speed() / 100.0, Plunge / 100.0));
    TestTrue(TEXT("A sprint slide pierces the surface with retained momentum and slows under drag"),
        Penguin->IsSwimming() && EntrySpeed > 770.0 && Penguin->Speed() < EntrySpeed * .66 && Plunge > 67.0);
    Tap(EKeys::Q);

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
