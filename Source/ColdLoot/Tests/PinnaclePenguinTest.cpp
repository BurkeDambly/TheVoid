#if WITH_DEV_AUTOMATION_TESTS

#include "../IcebergIsland.h"
#include "../PinnaclePenguin.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPinnaclePenguinTest, "ColdLoot.IcebergNetwork.PinnacleWander",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPinnaclePenguinTest::RunTest(const FString& Parameters)
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
    if (!TestNotNull(TEXT("Iceberg Network is running"), World))
    {
        return false;
    }

    struct FCharacter
    {
        TWeakObjectPtr<APinnaclePenguin> Penguin;
        FVector LastPosition;
        double Distance = 0.0;
        bool bPoseAdvanced = false;
        bool bPausedAfterWalking = false;
    };
    TArray<FCharacter> Characters;
    TSet<FName> MeshNames;
    for (APinnaclePenguin* Penguin : TActorRange<APinnaclePenguin>(World))
    {
        auto* Mesh = Penguin->FindComponentByClass<USkeletalMeshComponent>();
        if (!TestTrue(TEXT("NPC is placed in the Base Island sublevel with a mesh and shuffle"),
            Penguin->GetLevel()->GetOutermost()->GetName().EndsWith(TEXT("BaseIsland"))
            && Penguin->Island && Penguin->Island->bBaseIsland && Penguin->ShuffleAnimation
            && Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetSingleNodeInstance()))
        {
            return false;
        }
        MeshNames.Add(Mesh->GetSkeletalMeshAsset()->GetFName());
        Characters.Add({Penguin, Penguin->GetActorLocation()});
    }
    if (!TestTrue(TEXT("Both faction NPCs are present"), Characters.Num() == 2
        && MeshNames.Contains(TEXT("SK_Whiteout")) && MeshNames.Contains(TEXT("SK_Forgekeeper"))))
    {
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Characters, Deadline = World->GetTimeSeconds() + 12.0]() mutable
        {
            bool bFinished = true;
            for (FCharacter& Character : Characters)
            {
                APinnaclePenguin* Penguin = Character.Penguin.Get();
                if (!TestNotNull(TEXT("NPC stays loaded"), Penguin))
                {
                    return true;
                }
                const FVector Position = Penguin->GetActorLocation();
                if (!TestTrue(TEXT("NPC follows the dry island surface"),
                    Position.Z > 100.0 && FMath::IsNearlyEqual(Position.Z,
                        Penguin->Island->HeightAt(Position.X, Position.Y), 0.1)))
                {
                    return true;
                }
                const auto* Gait = Penguin->FindComponentByClass<USkeletalMeshComponent>()->GetSingleNodeInstance();
                if (Gait->IsPlaying())
                {
                    Character.Distance += FVector::Dist2D(Position, Character.LastPosition);
                    Character.bPoseAdvanced |= Gait->GetCurrentTime() > 0.0f;
                }
                else if (Character.Distance >= 80.0 && Character.bPoseAdvanced)
                {
                    Character.bPausedAfterWalking = true;
                }
                Character.LastPosition = Position;
                bFinished &= Character.bPausedAfterWalking;
                if (Penguin->GetWorld()->GetTimeSeconds() >= Deadline && !Character.bPausedAfterWalking)
                {
                    AddError(TEXT("NPC did not shuffle and pause within 12 seconds"));
                    return true;
                }
            }
            return bFinished;
        }));
    return true;
}

#endif
