#if WITH_DEV_AUTOMATION_TESTS

#include "../ArcticFish.h"
#include "../ColdLootBullet.h"
#include "../IcebergIsland.h"
#include "../IcebergNetworkTerrain.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcticFishTest, "ColdLoot.IcebergNetwork.ArcticFish",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FArcticFishTest::RunTest(const FString&)
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
    if (!TestNotNull(TEXT("A playable Iceberg Network world"), World))
    {
        return false;
    }
    TActorIterator<AIcebergNetworkTerrain> Networks(World);
    if (!TestTrue(TEXT("The map has an island network"), static_cast<bool>(Networks)))
    {
        return false;
    }
    const AIcebergNetworkTerrain& Network = **Networks;
    TArray<AArcticFish*> Fish;
    for (TActorIterator<AArcticFish> It(World); It; ++It)
    {
        Fish.Add(*It);
    }
    if (!TestEqual(TEXT("All twelve fish are assigned to the channel"), Fish.Num(), 12))
    {
        return false;
    }
    FVector RampLips[2];
    int32 RampCount = 0;
    for (TActorIterator<AIcebergIsland> It(World); It; ++It)
    {
        if (const TOptional<FVector> Lip = It->PracticeRampLip(); Lip && RampCount < UE_ARRAY_COUNT(RampLips))
        {
            RampLips[It->bBaseIsland ? 0 : 1] = *Lip;
            ++RampCount;
        }
    }
    if (!TestEqual(TEXT("Both practice ramps border the channel"), RampCount, 2))
    {
        return false;
    }
    const auto Depth = [&Network](const AArcticFish& Target)
    {
        const FVector P = Target.GetActorLocation();
        return P.Z - Network.WaterSurfaceAt(P.X, P.Y).Get(P.Z + 1000.0);
    };
    for (AArcticFish* Target : Fish)
    {
        USkeletalMeshComponent* Visual = Target->FindComponentByClass<USkeletalMeshComponent>();
        if (!TestTrue(TEXT("The fish uses the imported skeletal mesh and swim animation"),
            Visual && Visual->GetSkeletalMeshAsset() == Network.ArcticFishMesh && Visual->IsPlaying()))
        {
            return false;
        }
        Visual->RefreshBoneTransforms();
        const FVector ToHead = Visual->GetBoneLocation(TEXT("Head")) - Visual->GetBoneLocation(TEXT("Tail"));
        TestTrue(TEXT("The animation preserves the imported fish's bone spacing in centimeters"),
            ToHead.Size() > 40.0 && ToHead.Size() < 60.0);
        TestTrue(TEXT("The fish model points along its movement"), ToHead.CosineAngle2D(Target->GetActorForwardVector()) > 0.65);
    }

    TSet<AArcticFish*> Breaching;
    TSet<AArcticFish*> Submerged;
    TSet<AArcticFish*> RampJumpers[2];
    int32 MinimumAirborne[2] = {Fish.Num(), Fish.Num()};
    const FVector ShoreShot = RampLips[0] + FVector::UpVector * 160.0;
    const FVector IntoChannel = (RampLips[1] - RampLips[0]).GetSafeNormal2D();
    const FVector AlongShore = FVector::CrossProduct(FVector::UpVector, IntoChannel);
    const double ChannelLength = FVector::Dist2D(RampLips[0], RampLips[1]);
    const FCollisionQueryParams ShotQuery(SCENE_QUERY_STAT(ArcticFishShoreShot), true);
    bool bRoutesClearSeabed = true;
    bool bAllFishStayInChannel = true;
    for (int32 Step = 0; Step < 126; ++Step)
    {
        int32 Airborne[2]{};
        for (AArcticFish* Target : Fish)
        {
            Target->Tick(0.1f);
            const FVector Position = Target->GetActorLocation();
            bRoutesClearSeabed &= Position.Z - Network.HeightAt(Position.X, Position.Y) >= 30.0;
            const FVector FromBase = Position - RampLips[0];
            const double AlongChannel = FVector::DotProduct(FromBase, IntoChannel);
            bAllFishStayInChannel &= AlongChannel > 0.0 && AlongChannel < ChannelLength
                && FMath::Abs(FVector::DotProduct(FromBase, AlongShore)) < 700.0;
            const double Clearance = Depth(*Target);
            if (Clearance > 80.0)
            {
                Breaching.Add(Target);
            }
            if (Clearance < -45.0)
            {
                Submerged.Add(Target);
            }
            if (Clearance <= 30.0)
            {
                continue;
            }
            for (int32 Ramp = 0; Ramp < 2; ++Ramp)
            {
                const FVector Offset = Position - RampLips[Ramp];
                const double Ahead = FVector::DotProduct(Offset, Ramp == 0 ? IntoChannel : -IntoChannel);
                if (Ahead > 0.0 && Ahead < 1900.0 && FMath::Abs(FVector::DotProduct(Offset, AlongShore)) < 700.0)
                {
                    RampJumpers[Ramp].Add(Target);
                    ++Airborne[Ramp];
                }
            }
        }
        for (int32 Ramp = 0; Ramp < 2; ++Ramp)
        {
            MinimumAirborne[Ramp] = FMath::Min(MinimumAirborne[Ramp], Airborne[Ramp]);
        }
    }
    TestTrue(TEXT("The routes clear the ice below the fish"), bRoutesClearSeabed);
    TestTrue(TEXT("Every fish stays between the two ramps"), bAllFishStayInChannel);
    TestEqual(TEXT("All twelve fish jump above the water"), Breaching.Num(), 12);
    TestEqual(TEXT("All twelve fish swim underneath the water"), Submerged.Num(), 12);
    for (int32 Ramp = 0; Ramp < 2; ++Ramp)
    {
        TestTrue(TEXT("A full school jumps directly off this ramp"), RampJumpers[Ramp].Num() >= AArcticFish::ChannelSchoolSize);
        TestTrue(TEXT("At least two fish visibly clear the surface at once"), MinimumAirborne[Ramp] >= 2);
    }

    AArcticFish* Jumper = nullptr;
    for (int32 Step = 0; Step < 45 && !Jumper; ++Step)
    {
        for (AArcticFish* Target : RampJumpers[0])
        {
            Target->Tick(0.1f);
            FHitResult Hit;
            if (Depth(*Target) > 80.0 && World->LineTraceSingleByChannel(Hit, ShoreShot, Target->GetActorLocation(),
                AColdLootBullet::CollisionChannel, ShotQuery) && Hit.GetActor() == Target)
            {
                Jumper = Target;
                break;
            }
        }
    }
    if (!TestNotNull(TEXT("A breaching fish has a clear firing line from the base ramp"), Jumper))
    {
        return false;
    }
    const FVector Direction = (Jumper->GetActorLocation() - ShoreShot).GetSafeNormal();
    AColdLootBullet* Bullet = World->SpawnActor<AColdLootBullet>(ShoreShot, Direction.Rotation());
    if (!TestNotNull(TEXT("A starter-style projectile"), Bullet))
    {
        return false;
    }
    Bullet->Launch(Direction, 10000.0f, 20.0f, nullptr);
    Bullet->FindComponentByClass<UProjectileMovementComponent>()->TickComponent(0.25f, LEVELTICK_All, nullptr);
    UBoxComponent* Collision = Jumper->FindComponentByClass<UBoxComponent>();
    TestEqual(TEXT("A real projectile takes the leaping fish out of play"),
        Collision->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    Jumper->Tick(4.0f);
    TestEqual(TEXT("The fish stays out of play during its respawn delay"),
        Collision->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    Jumper->Tick(5.1f);
    TestEqual(TEXT("The fish returns to its route and can be shot again"),
        Collision->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
    Bullet->Destroy();
    return true;
}

#endif
