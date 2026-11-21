#if WITH_DEV_AUTOMATION_TESTS

#include "../FrozenBasinTerrain.h"
#include "../ColdLootGameMode.h"
#include "../PenguinPawn.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrozenBasinRoamTest, "ColdLoot.FrozenBasin.Roam",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FFrozenBasinRoamTest::RunTest(const FString& Parameters)
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
    if (!TestNotNull(TEXT("Play world exists"), World))
    {
        return false;
    }

    TActorIterator<AFrozenBasinTerrain> TerrainIt(World);
    TActorIterator<APenguinPawn> PenguinIt(World);
    APlayerController* Controller = World->GetFirstPlayerController();
    if (!TestTrue(TEXT("Basin has its terrain and the configured, possessed penguin"),
        TerrainIt && PenguinIt && Controller && Controller->GetPawn() == *PenguinIt
        && Controller->PlayerInput && PenguinIt->InputComponent && PenguinIt->PenguinMesh->GetSkeletalMeshAsset()
        && PenguinIt->WalkAnimation && PenguinIt->SprintAnimation && PenguinIt->SlideAnimation && PenguinIt->HasWeapon()))
    {
        return false;
    }
    AFrozenBasinTerrain& Terrain = **TerrainIt;
    APenguinPawn& Penguin = **PenguinIt;
    UPlayerInput& Input = *Controller->PlayerInput;
    const TArray<UInputComponent*> Stack{Penguin.InputComponent};
    constexpr float Frame = 1.0f / 60.0f;
    auto Key = [&Input, &Stack](const FKey& Button, EInputEvent Event)
    {
        const float Value = Event == IE_Pressed ? 1.0f : 0.0f;
        Input.InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Button, Event,
            Value, false, FPlatformTime::Cycles64()));
        Input.ProcessInputStack(Stack, Frame, false);
    };
    auto Advance = [&Penguin](int32 Frames)
    {
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            Penguin.Tick(Frame);
        }
    };

    const FVector Home = Penguin.GetActorLocation();
    const FVector Summit(-450, 0, Terrain.HeightAt(-450, 0) + 1);
    TestTrue(TEXT("Spawn is on the 610 meter summit"), Home.Equals(Summit, 1.0) && Summit.Z > 60900);
    int32 MeshActors = 0;
    for (TActorIterator<AStaticMeshActor> MeshIt(World); MeshIt; ++MeshIt)
    {
        ++MeshActors;
    }
    TestEqual(TEXT("Sea is the only placed static mesh actor"), MeshActors, 1);
    FHitResult SurfaceHit;
    TestEqual(TEXT("The island has twelve mapped locations"), Terrain.Zones().Num(), 12);
    for (int32 Index = 0; Index < Terrain.Zones().Num(); ++Index)
    {
        const FVector Location = Terrain.ZoneLocation(Index);
        TestTrue(TEXT("Each marked location lies on the island"), Location.Z > 0 && Terrain.KeepInside(Location) == Location);
    }
    const FVector Lake = Terrain.ZoneLocation(6);
    TestTrue(TEXT("Frozen lake has terrain collision for weapons"), World->LineTraceSingleByChannel(SurfaceHit,
        Lake + FVector(0, 0, 500), Lake - FVector(0, 0, 500), ECC_GameTraceChannel1)
        && SurfaceHit.GetActor() == &Terrain);
    const FVector Quarry = Terrain.ZoneLocation(2);
    TestTrue(TEXT("Quarry has a floor below the terraces"), Terrain.HeightAt(Quarry.X, Quarry.Y - 14400) > Quarry.Z + 3700);
    const FVector Basin = Terrain.ZoneLocation(3);
    TestTrue(TEXT("Frost Basin has a level ice sheet"), FMath::IsNearlyEqual(Basin.Z, 10500.0, 1.0)
        && FMath::IsNearlyEqual(Terrain.HeightAt(Basin.X - 7000, Basin.Y + 4400), Basin.Z, 1.0));
    TestTrue(TEXT("Frost Basin has three high sides and an open coastal mouth"),
        Terrain.HeightAt(-80400, Basin.Y) > Basin.Z + 3800
        && Terrain.HeightAt(Basin.X, 28200) > Basin.Z + 3400
        && Terrain.HeightAt(Basin.X, 61100) > Basin.Z + 3400
        && Terrain.HeightAt(-127000, Basin.Y) < Basin.Z + 3500);
    FHitResult Overhead;
    TestFalse(TEXT("Frost Basin is open to the sky"), World->LineTraceSingleByChannel(Overhead,
        Basin + FVector(0, 0, 200), Basin + FVector(0, 0, 24000), ECC_Visibility));
    TestTrue(TEXT("Shore keeps the penguin inside the island"), Terrain.KeepInside(FVector(260000, 0, 0)).X < 195000
        && Terrain.KeepInside(FVector(146000, 0, 0)).X == 146000);
    TestEqual(TEXT("The map shows seven graded routes"), Terrain.NavigationRoutes().Num(), 7);
    TestTrue(TEXT("Switchback is gentler than the mountain face"), Terrain.NormalAt(9850, 1150).Z > 0.88);

    auto* HUD = Cast<AColdLootHUD>(Controller->GetHUD());
    if (!TestNotNull(TEXT("Island map belongs to the player HUD"), HUD))
    {
        return false;
    }
    Key(EKeys::M, IE_Pressed);
    Key(EKeys::M, IE_Released);
    TestTrue(TEXT("M opens the live island map"), HUD->IsIslandMapOpen());
#if WITH_EDITOR
    Key(EKeys::Down, IE_Pressed);
    Key(EKeys::Down, IE_Released);
    Key(EKeys::Enter, IE_Pressed);
    Key(EKeys::Enter, IE_Released);
    TestTrue(TEXT("The editor can visit a selected zone from the map"), !HUD->IsIslandMapOpen()
        && Penguin.GetActorLocation().Equals(Terrain.ZoneLocation(1) + FVector(0, 0, 1), 1.0));
#else
    Key(EKeys::M, IE_Pressed);
    Key(EKeys::M, IE_Released);
#endif
    Key(EKeys::R, IE_Pressed);
    Key(EKeys::R, IE_Released);

    Key(EKeys::W, IE_Pressed);
    Advance(120);
    TestTrue(TEXT("Walk crosses the summit"), Penguin.GetActorLocation().X > Home.X + 250 && Penguin.Speed() > 80);
    Key(EKeys::LeftShift, IE_Pressed);
    Advance(120);
    TestTrue(TEXT("Sprint moves toward the rim"), Penguin.GetActorLocation().X > Home.X + 1100 && Penguin.Speed() > 400);
    Key(EKeys::W, IE_Released);
    Key(EKeys::LeftShift, IE_Released);
    Key(EKeys::R, IE_Pressed);
    Key(EKeys::R, IE_Released);
    TestTrue(TEXT("R returns to the summit"), Penguin.GetActorLocation().Equals(Home, 0.1));

    // Point the same player controls downhill on the trail rather than simulating a different movement system.
    const FVector Chute(65700, 45900, Terrain.HeightAt(65700, 45900) + 1);
    Penguin.SetActorLocation(Chute);
    USpringArmComponent* Arm = Penguin.FindComponentByClass<USpringArmComponent>();
    if (!TestNotNull(TEXT("Penguin has its existing camera boom"), Arm))
    {
        return false;
    }
    const FVector Downhill = FVector(54200, 53000, 0) - FVector(Chute.X, Chute.Y, 0);
    Arm->SetRelativeRotation(FRotator(-9, Downhill.Rotation().Yaw, 0));
    Key(EKeys::LeftShift, IE_Pressed);
    Key(EKeys::W, IE_Pressed);
    Advance(54);
    Key(EKeys::C, IE_Pressed);
    Key(EKeys::C, IE_Released);
    Advance(195);
    TestTrue(TEXT("Existing slide controls carry the penguin downhill"),
        Penguin.SlideDistance() > 2500 && Penguin.GetActorLocation().Z < Chute.Z - 700);
    Key(EKeys::W, IE_Released);
    Key(EKeys::LeftShift, IE_Released);
    Key(EKeys::R, IE_Pressed);
    Key(EKeys::R, IE_Released);
    if (FParse::Param(FCommandLine::Get(), TEXT("ColdzoneMapPreview")))
    {
        Key(EKeys::M, IE_Pressed);
        Key(EKeys::M, IE_Released);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("ColdzoneIslandMap.png"), false, false);
    }
    return true;
}

#endif
