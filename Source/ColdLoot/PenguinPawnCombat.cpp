#include "PenguinPawn.h"

#include "ColdLootBullet.h"
#include "ColdLootTuning.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/SpringArmComponent.h"

float APenguinPawn::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (!HasAuthority() || IsKnockedOut() || Damage <= 0.0f
        || !DamageEvent.IsOfType(FPointDamageEvent::ClassID)
        || (!Cast<AColdLootBullet>(DamageCauser) && !Cast<APenguinPawn>(DamageCauser)))
    {
        return 0.0f;
    }

    // Health measures bullet hits, independent of weapon damage and ricochet falloff.
    const FColdLootTuning& Tuning = FColdLootTuning::ForWorld(GetWorld());
    const int32 HealthPerHit = FMath::RoundToInt((MaxHealth / 5) * Tuning.Get(EColdLootTuning::PlayerDamage));
    const float Applied = Super::TakeDamage(HealthPerHit, DamageEvent, EventInstigator, DamageCauser);
    if (Applied <= 0.0f)
    {
        return 0.0f;
    }
    CombatState.Health = FMath::Max(0, GetHealth() - HealthPerHit);
    if (IsKnockedOut())
    {
        const auto& Shot = static_cast<const FPointDamageEvent&>(DamageEvent);
        const FVector Direction = Shot.ShotDirection.GetSafeNormal2D();
        CombatState.KnockoutOrigin = GetActorLocation();
        CombatState.KnockoutRotation = BodyPivot->GetComponentRotation();
        CombatState.LaunchVelocity = (Direction.IsNearlyZero() ? GetActorForwardVector() : Direction) * 1150.0
            + FVector::UpVector * 690.0;
        CombatState.LaunchVelocity *= Tuning.Get(EColdLootTuning::Knockback);
        OnRep_CombatState();
    }
    ForceNetUpdate();
    return Applied;
}

void APenguinPawn::OnRep_CombatState()
{
    if (Terrain && IsKnockedOut() != bRagdollActive)
    {
        SetRagdollActive(IsKnockedOut());
    }
}

void APenguinPawn::SetRagdollActive(bool bActive)
{
    bRagdollActive = bActive;
    if (bActive)
    {
        SetActorLocation(CombatState.KnockoutOrigin, false, nullptr, ETeleportType::TeleportPhysics);
        BodyPivot->SetWorldRotation(CombatState.KnockoutRotation);
        BulletHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        bFireAfterPose = bAimAfterPose = bFireHeld = bAimHeld = false;
        ShotRequestTime = GroundSpeed = 0.0f;
        WeaponGrip->SetVisibility(false, true);
        PenguinMesh->SetCollisionProfileName(TEXT("Ragdoll"));
        PenguinMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
        PenguinMesh->SetSimulatePhysics(true);
        PenguinMesh->SetAllPhysicsLinearVelocity(CombatState.LaunchVelocity);
        PenguinMesh->SetAllPhysicsAngularVelocityInRadians(
            FVector::CrossProduct(FVector::UpVector, CombatState.LaunchVelocity.GetSafeNormal2D()) * 5.0);
        return;
    }

    PenguinMesh->SetSimulatePhysics(false);
    PenguinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PenguinMesh->AttachToComponent(BodyPivot, FAttachmentTransformRules::KeepRelativeTransform);
    PenguinMesh->SetRelativeTransform(StandingMeshTransform, false, nullptr, ETeleportType::TeleportPhysics);
    BulletHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CameraArm->SetRelativeLocation(CameraMountLocation);
    bReceivedNetworkPose = false;
}
