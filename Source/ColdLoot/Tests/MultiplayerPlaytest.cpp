#if WITH_DEV_AUTOMATION_TESTS

#include "../ColdLootBullet.h"
#include "../ColdLootTuning.h"
#include "../ColdLootWeaponDefinition.h"
#include "../PenguinPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FColdLootMultiplayerPlaytest, "ColdLoot.IcebergNetwork.MultiplayerPlaytest",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FColdLootMultiplayerPlaytest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("ColdLootMultiplayerSmoke")))
    {
        return true;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
        [this, Started = FPlatformTime::Seconds(), Shots = 0, bSawKnockout = false, KnockoutAt = 0.0,
         GuestAimAt = 0.0, bGuestFiring = false, bCheckedGuestShot = false, bAskedReset = false,
         bStartedSwim = false, bCheckedSurfaceSwim = false, bConfiguredTuning = false]() mutable
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - Started > 30.0)
            {
                AddError(FString::Printf(TEXT("Multiplayer playtest timed out. Host shots: %d. Checked guest shot: %d. Saw knockout: %d. Surface swim: %d."),
                    Shots, bCheckedGuestShot, bSawKnockout, bCheckedSurfaceSwim));
                return true;
            }
            UWorld* World = GEngine->GetCurrentPlayWorld();
            if (!World || World->GetNetMode() == NM_Standalone)
            {
                return false;
            }
            APlayerController* Controller = World->GetFirstPlayerController();
            APenguinPawn* Local = Controller ? Cast<APenguinPawn>(Controller->GetPawn()) : nullptr;
            if (!Local)
            {
                return false;
            }
            APenguinPawn* Other = nullptr;
            for (TActorIterator<APenguinPawn> It(World); It; ++It)
            {
                if (*It != Local && !It->IsNetStartupActor())
                {
                    Other = *It;
                    break;
                }
            }
            if (!Other)
            {
                return false;
            }
            AColdLootGameState* GameState = World->GetGameState<AColdLootGameState>();
            if (!GameState)
            {
                return false;
            }
            constexpr float BulletSpeedScale = 1.75f;
            const auto BulletSpeed = EColdLootTuning::BulletSpeed;

            if (World->GetNetMode() == NM_Client)
            {
                if (!bConfiguredTuning)
                {
                    if (GameState->GetTuning().Get(BulletSpeed) != BulletSpeedScale)
                    {
                        return false;
                    }
                    TestFalse(TEXT("Guests cannot edit the session's tuning"), GameState->SetTuning(BulletSpeed, 0.5f));
                    TestEqual(TEXT("The guest receives the host's swim speed"), GameState->GetTuning().Get(EColdLootTuning::SwimSpeed), 1.25f);
                    bConfiguredTuning = true;
                }
                if (GuestAimAt == 0.0)
                {
                    Local->CameraArm->SetRelativeRotation(FRotator(-9.0, 0.0, 0.0));
                    GuestAimAt = Now;
                }
                if (!bGuestFiring && Now - GuestAimAt > 0.3)
                {
                    Local->StartFire();
                    bGuestFiring = true;
                }
                if (Local->IsKnockedOut())
                {
                    if (!bSawKnockout)
                    {
                        Local->StopFire();
                        TestTrue(TEXT("Guest plays knockout physics"), Local->PenguinMesh->IsAnySimulatingPhysics());
                        TestNotNull(TEXT("Guest can see the host mesh"), Other->PenguinMesh->GetSkeletalMeshAsset());
                        bSawKnockout = true;
                        KnockoutAt = Now;
                    }
                    if (!bAskedReset && Now - KnockoutAt > 0.5)
                    {
                        bAskedReset = true;
                        Local->ResetFlat();
                    }
                }
                if (bAskedReset && Local->GetHealth() == APenguinPawn::MaxHealth && Local->IsSwimming())
                {
                    if (!bCheckedSurfaceSwim && Local->GetActorLocation().X > 8750.0 && !Local->IsHoldingWaterEntryPose())
                    {
                        TestNotNull(TEXT("Guest's water entry pose advances into a valid swim animation"),
                            Local->SwimAnimation.Get());
                        TestTrue(TEXT("Guest paddles near the surface with level aim"),
                            Local->GetSwimDepth() < 85.0);
                        bCheckedSurfaceSwim = true;
                    }
                    const FRotator Aim(bCheckedSurfaceSwim ? -55.0 : -9.0, 0.0, 0.0);
                    Local->CameraArm->SetRelativeRotation(Aim);
                    Local->ServerUpdateInput(1.0f, 0.0f, Aim, false, false);
                    return bCheckedSurfaceSwim && Local->GetSwimDepth() > 180.0;
                }
                return false;
            }

            if (!Other->GetController())
            {
                return false;
            }
            if (!bConfiguredTuning)
            {
                TestTrue(TEXT("The host can tune bullet speed"), GameState->SetTuning(BulletSpeed, BulletSpeedScale));
                TestTrue(TEXT("The host can tune swim speed"), GameState->SetTuning(EColdLootTuning::SwimSpeed, 1.25f));
                bConfiguredTuning = true;
            }
            if (!bCheckedGuestShot)
            {
                for (TActorIterator<AColdLootBullet> Bullet(World); Bullet; ++Bullet)
                {
                    if (Bullet->GetOwner() == Other)
                    {
                        const FVector Direction = Bullet->GetActorForwardVector();
                        const UProjectileMovementComponent* Flight = Bullet->FindComponentByClass<UProjectileMovementComponent>();
                        TestTrue(TEXT("The host applies the new projectile speed to guest fire"), Flight
                            && FMath::IsNearlyEqual(Flight->Velocity.Size(), Other->WeaponDefinition->BulletSpeed * BulletSpeedScale, 1.0));
                        if (!TestTrue(TEXT("Guest projectile travels forward and down with negative camera pitch"),
                            Direction.X > 0.85 && Direction.Z < -0.03))
                        {
                            AddError(FString::Printf(TEXT("Guest projectile direction: %s"), *Direction.ToString()));
                            return true;
                        }
                        bCheckedGuestShot = true;
                        break;
                    }
                }
                if (!bCheckedGuestShot)
                {
                    return false;
                }
            }
            if (Shots == 5)
            {
                if (!bSawKnockout && Other->IsKnockedOut())
                {
                    TestTrue(TEXT("Host simulates the guest ragdoll"), Other->PenguinMesh->IsAnySimulatingPhysics());
                    TestFalse(TEXT("Knocked-out penguin cannot fire"), Other->CanFire());
                    bSawKnockout = true;
                }
                if (bSawKnockout && Other->GetHealth() == APenguinPawn::MaxHealth)
                {
                    if (!bStartedSwim)
                    {
                        Other->SetActorLocation(FVector(8400.0, 0.0, -18.0));
                        bStartedSwim = true;
                    }
                    bCheckedSurfaceSwim |= Other->IsSwimming() && Other->GetActorLocation().X > 8680.0
                        && Other->GetSwimDepth() < 85.0;
                    return bCheckedSurfaceSwim && Other->GetSwimDepth() > 180.0;
                }
                return false;
            }
            if (Other->GetHealth() == APenguinPawn::MaxHealth - Shots * 20)
            {
                TestEqual(TEXT("Next bullet lands on a live penguin"), Other->IsKnockedOut(), false);
                const FVector Direction = FVector::ForwardVector;
                const FVector Origin = Other->BulletHitbox->GetComponentLocation() - Direction * 125.0;
                FActorSpawnParameters Params;
                Params.Owner = Local;
                Params.Instigator = Local;
                AColdLootBullet* Bullet = World->SpawnActor<AColdLootBullet>(Origin, Direction.Rotation(), Params);
                if (!TestNotNull(TEXT("Test shot spawned"), Bullet))
                {
                    return true;
                }
                Bullet->Launch(Direction, 1200.0f, 20.0f, nullptr);
                ++Shots;
            }
            return false;
        }));
    return true;
}

#endif
