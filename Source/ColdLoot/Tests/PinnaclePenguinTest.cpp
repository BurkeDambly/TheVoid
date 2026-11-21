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
        float LastYaw = 0.0f;
        FTransform LastFeet[2];
        double Distance = 0.0;
        bool bSampledFeet = false;
        bool bVerifiedTurn = false;
        bool bPoseAdvanced = false;
        bool bPausedAfterWalking = false;
    };
    TArray<FCharacter> Characters;
    TSet<FName> MeshNames;
    for (APinnaclePenguin* Penguin : TActorRange<APinnaclePenguin>(World))
    {
        auto* Mesh = Penguin->FindComponentByClass<UPinnaclePenguinMeshComponent>();
        if (!TestTrue(TEXT("NPC is placed in the Base Island sublevel with a mesh and shuffle"),
            Penguin->GetLevel()->GetOutermost()->GetName().EndsWith(TEXT("BaseIsland"))
            && Penguin->Island && Penguin->Island->bBaseIsland && Penguin->ShuffleAnimation
            && Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetSingleNodeInstance()
            && Mesh->GetFootBoneIndex(0) != INDEX_NONE && Mesh->GetFootBoneIndex(1) != INDEX_NONE))
        {
            return false;
        }
        MeshNames.Add(Mesh->GetSkeletalMeshAsset()->GetFName());
        FCharacter& Character = Characters.AddDefaulted_GetRef();
        Character.Penguin = Penguin;
        Character.LastPosition = Penguin->GetActorLocation();
        Character.LastYaw = Penguin->GetActorRotation().Yaw;
    }
    if (!TestTrue(TEXT("All five faction NPCs are present"), Characters.Num() == 5
        && MeshNames.Contains(TEXT("SK_Whiteout")) && MeshNames.Contains(TEXT("SK_Forgekeeper"))
        && MeshNames.Contains(TEXT("SK_Stormstitch")) && MeshNames.Contains(TEXT("SK_Sunmarshal"))
        && MeshNames.Contains(TEXT("SK_Nightglass"))))
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
                const auto* Mesh = Penguin->FindComponentByClass<UPinnaclePenguinMeshComponent>();
                const auto* Gait = Mesh->GetSingleNodeInstance();
                const float Yaw = Penguin->GetActorRotation().Yaw;
                const bool bTurned = Character.bSampledFeet && !Gait->IsPlaying()
                    && Position.Equals(Character.LastPosition, 0.01)
                    && FMath::Abs(FMath::FindDeltaAngleDegrees(Character.LastYaw, Yaw)) > 0.1f;
                for (int32 Side = 0; Side < 2; ++Side)
                {
                    const FTransform Foot = Mesh->GetBoneTransform(Mesh->GetFootBoneIndex(Side));
                    if (bTurned && !TestTrue(FString::Printf(TEXT("%s keeps foot %d planted while turning"),
                        *Mesh->GetSkeletalMeshAsset()->GetName(), Side),
                        Foot.GetLocation().Equals(Character.LastFeet[Side].GetLocation(), 0.1)
                        && Foot.GetRotation().AngularDistance(Character.LastFeet[Side].GetRotation()) < 0.002f))
                    {
                        return true;
                    }
                    Character.LastFeet[Side] = Foot;
                }
                Character.LastYaw = Yaw;
                Character.bSampledFeet = true;
                Character.bVerifiedTurn |= bTurned;
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
                const bool bChecked = Character.bPausedAfterWalking && Character.bVerifiedTurn;
                bFinished &= bChecked;
                if (Penguin->GetWorld()->GetTimeSeconds() >= Deadline && !bChecked)
                {
                    AddError(FString::Printf(TEXT("%s did not complete a planted turn, shuffle, and pause within 12 seconds"),
                        *Mesh->GetSkeletalMeshAsset()->GetName()));
                    return true;
                }
            }
            return bFinished;
        }));
    return true;
}

#endif
