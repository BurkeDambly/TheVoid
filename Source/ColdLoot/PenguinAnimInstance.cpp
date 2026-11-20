#include "PenguinAnimInstance.h"

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Camera/CameraComponent.h"
#include "ColdLootWeaponDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PenguinPawn.h"

namespace
{
void Sample(FPoseContext& Output, const UAnimSequence* Sequence, double Time, bool bLooping = false)
{
    if (Sequence)
    {
        FAnimationPoseData PoseData(Output);
        Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time, false, {}, bLooping));
    }
    else
    {
        Output.ResetToRefPose();
    }
}

void Blend(FPoseContext& Output, FPoseContext& Source, FPoseContext& Target, float TargetWeight)
{
    const FAnimationPoseData SourceData(Source);
    const FAnimationPoseData TargetData(Target);
    FAnimationPoseData OutputData(Output);
    FAnimationRuntime::BlendTwoPosesTogether(SourceData, TargetData, 1.0f - TargetWeight, OutputData);
}
}

class FPenguinAnimProxy final : public FAnimInstanceProxy
{
public:
    explicit FPenguinAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        APenguinPawn* Penguin = Cast<APenguinPawn>(Instance->TryGetPawnOwner());
        Walk = Penguin ? Penguin->WalkAnimation.Get() : nullptr;
        Sprint = Penguin ? Penguin->SprintAnimation.Get() : nullptr;
        Slide = Penguin ? Penguin->SlideAnimation.Get() : nullptr;
        Swim = Penguin ? Penguin->SwimAnimation.Get() : nullptr;
        Hold = Penguin && Penguin->WeaponDefinition ? Penguin->WeaponDefinition->HoldPose.Get() : nullptr;
        bUseFittedFlipperTransforms = Hold && Penguin->WeaponDefinition->bUseFittedFlipperTransforms;
        if (bUseFittedFlipperTransforms)
        {
            HoldHingeOnFlipper = Penguin->WeaponDefinition->HoldHingeOnFlipper;
        }
        if (!Penguin)
        {
            return;
        }

        Phase = Penguin->GaitPhase;
        SprintWeight = FMath::SmoothStep(0.0f, 1.0f, Penguin->SprintBlend);
        MovingWeight = FMath::SmoothStep(0.0f, 65.0f, Penguin->GroundSpeed);
        Jiggle = MovingWeight * Penguin->UpperBodyJiggle;
        AimLeanWeight = Penguin->CanFire() ? Penguin->AimBlend : 0.0f;
        HoldWeight = FMath::Max(Penguin->SprintFireBlend,
            1.0f - FMath::SmoothStep(0.0f, 0.62f, Penguin->SprintBlend));
        bAimWeapon = Penguin->CanFire();
        if (bAimWeapon && HoldWeight > KINDA_SMALL_NUMBER)
        {
            const FTransform& MeshTransform = Penguin->PenguinMesh->GetComponentTransform();
            const FVector BodyForward = Penguin->BodyPivot->GetForwardVector();
            BodyForwardInMesh = MeshTransform.InverseTransformVectorNoScale(BodyForward);
            BodyRightInMesh = MeshTransform.InverseTransformVectorNoScale(Penguin->BodyPivot->GetRightVector());
            BodyUpInMesh = MeshTransform.InverseTransformVectorNoScale(Penguin->BodyPivot->GetUpVector());
            ViewPitch = Penguin->GetWeaponAimDirection().Rotation().Pitch;
            BoreOnFlipper = (Penguin->WeaponReceiver->GetSocketTransform(TEXT("Muzzle"), RTS_Component)
                * Penguin->WeaponReceiver->GetRelativeTransform() * Penguin->WeaponGrip->GetRelativeTransform())
                .GetRotation().GetForwardVector();
        }
        SlideTime = FMath::Max(Penguin->SlidePoseTime, 0.0f);
        SlideWeight = 0.0f;
        const bool bSwimPose = Penguin->IsSwimming() || Penguin->bWaterBreaching;
        SwimWeight = FMath::FInterpConstantTo(SwimWeight, bSwimPose ? 1.0f : 0.0f, DeltaSeconds, 5.5f);
        if (bSwimPose)
        {
            const float Effort = Penguin->SwimThrust;
            StrokeWeight = FMath::FInterpConstantTo(StrokeWeight, Effort, DeltaSeconds, 5.0f);
            StrokePhase = FMath::Fmod(StrokePhase + DeltaSeconds * FMath::Lerp(.75f, 1.36f, StrokeWeight), 1.0f);
        }
        else if (Slide && (Penguin->bSliding || Penguin->bRecovering))
        {
            const float EnterWeight = FMath::SmoothStep(0.0f, 0.21f, SlideTime);
            const float ExitWeight = FMath::SmoothStep(0.0f, 0.23f, Slide->GetPlayLength() - SlideTime);
            SlideWeight = EnterWeight * ExitWeight;
        }
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        if (!Swim || SwimWeight <= 0.0f)
        {
            EvaluateLand(Output);
        }
        else if (SwimWeight >= 1.0f)
        {
            EvaluateSwim(Output);
        }
        else
        {
            FPoseContext Grounded(Output);
            FPoseContext Swimming(Output);
            EvaluateLand(Grounded);
            EvaluateSwim(Swimming);
            Blend(Output, Grounded, Swimming, SwimWeight);
        }
        return true;
    }

private:
    void EvaluateSwim(FPoseContext& Output) const
    {
        if (StrokeWeight <= 0.0f)
        {
            Sample(Output, Swim, 0.0, true);
        }
        else if (StrokeWeight >= 1.0f)
        {
            Sample(Output, Swim, StrokePhase * Swim->GetPlayLength(), true);
        }
        else
        {
            FPoseContext Glide(Output);
            FPoseContext Paddle(Output);
            Sample(Glide, Swim, 0.0, true);
            Sample(Paddle, Swim, StrokePhase * Swim->GetPlayLength(), true);
            Blend(Output, Glide, Paddle, StrokeWeight);
        }
    }

    void EvaluateLand(FPoseContext& Output) const
    {
        if (SlideWeight >= 1.0f && Slide)
        {
            Sample(Output, Slide, SlideTime);
        }
        else if (SlideWeight > 0.0f && Slide)
        {
            FPoseContext Grounded(Output);
            FPoseContext Sliding(Output);
            EvaluateGrounded(Grounded);
            Sample(Sliding, Slide, SlideTime);
            Blend(Output, Grounded, Sliding, SlideWeight);
        }
        else
        {
            EvaluateGrounded(Output);
        }
    }

    void EvaluateGait(FPoseContext& Output) const
    {
        const double WalkTime = Walk ? Phase * Walk->GetPlayLength() : 0.0;
        const double SprintTime = Sprint ? Phase * Sprint->GetPlayLength() : 0.0;
        if (!Sprint || SprintWeight <= 0.0f)
        {
            Sample(Output, Walk, WalkTime, true);
        }
        else if (!Walk || SprintWeight >= 1.0f)
        {
            Sample(Output, Sprint, SprintTime, true);
        }
        else
        {
            // The source clips share a normalized left-right stride cycle.
            FPoseContext Walking(Output);
            FPoseContext Sprinting(Output);
            Sample(Walking, Walk, WalkTime, true);
            Sample(Sprinting, Sprint, SprintTime, true);
            Blend(Output, Walking, Sprinting, SprintWeight);
        }
    }

    void EvaluateGrounded(FPoseContext& Output) const
    {
        if (MovingWeight <= 0.0f)
        {
            Sample(Output, Walk, 0.0);
        }
        else if (MovingWeight < 1.0f)
        {
            FPoseContext Idle(Output);
            FPoseContext Moving(Output);
            Sample(Idle, Walk, 0.0);
            EvaluateGait(Moving);
            Blend(Output, Idle, Moving, MovingWeight);
        }
        else
        {
            EvaluateGait(Output);
        }

        if (Jiggle > KINDA_SMALL_NUMBER || AimLeanWeight > KINDA_SMALL_NUMBER)
        {
            ApplyUpperBodyMotion(Output.Pose);
        }
        if (Hold && HoldWeight > KINDA_SMALL_NUMBER)
        {
            ApplyWeaponHold(Output);
        }
    }

    void ApplyWeaponHold(FPoseContext& Output) const
    {
        static const FName FlipperName(TEXT("Flipper_R"));
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        const auto Flipper = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(FlipperName)));
        if (!Output.Pose.IsValidIndex(Flipper))
        {
            return;
        }
        const auto Parent = Output.Pose.GetParentBoneIndex(Flipper);
        if (!Output.Pose.IsValidIndex(Parent))
        {
            return;
        }
        FPoseContext Fitted(Output);
        Sample(Fitted, Hold, 0.0);
        const FTransform& Root = Output.Pose[Parent];
        const FQuat FittedInMesh = Fitted.Pose[Parent].GetRotation() * Fitted.Pose[Flipper].GetRotation();
        FTransform& Current = Output.Pose[Flipper];
        FQuat DesiredInMesh = FittedInMesh;
        if (bAimWeapon)
        {
            const FVector FittedBore = FittedInMesh.RotateVector(BoreOnFlipper);
            const FVector FlatBore = FVector::VectorPlaneProject(FittedBore, BodyUpInMesh).GetSafeNormal();
            const FQuat ForwardHold = FQuat::FindBetweenNormals(FlatBore, BodyForwardInMesh) * FittedInMesh;
            const double FittedPitch = FMath::RadiansToDegrees(FMath::Atan2(
                FVector::DotProduct(FittedBore, BodyUpInMesh), FVector::DotProduct(FittedBore, FlatBore)));
            DesiredInMesh = FQuat(BodyRightInMesh, FMath::DegreesToRadians(FittedPitch - ViewPitch)) * ForwardHold;
        }
        const FQuat DesiredLocal = Root.GetRotation().Inverse() * DesiredInMesh;
        Current.SetRotation(FQuat::Slerp(Current.GetRotation(), DesiredLocal, HoldWeight).GetNormalized());
        if (bUseFittedFlipperTransforms)
        {
            const FTransform& Authored = Fitted.Pose[Flipper];
            // Keep the authored wing-to-body contact fixed as the hold follows the view.
            const FVector HingeOffset = Authored.GetScale3D() * HoldHingeOnFlipper;
            const FVector FittedLocation = Authored.TransformPosition(HoldHingeOnFlipper)
                - DesiredLocal.RotateVector(HingeOffset);
            Current.SetTranslation(FMath::Lerp(Current.GetTranslation(), FittedLocation, HoldWeight));
            Current.SetScale3D(FMath::Lerp(Current.GetScale3D(), Authored.GetScale3D(), HoldWeight));

            static const FName FreeFlipperName(TEXT("Flipper_L"));
            const auto FreeFlipper = Bones.MakeCompactPoseIndex(
                FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(FreeFlipperName)));
            if (Output.Pose.IsValidIndex(FreeFlipper))
            {
                FTransform& Free = Output.Pose[FreeFlipper];
                Free.Blend(Free, Fitted.Pose[FreeFlipper], HoldWeight);
            }
        }
    }

    void ApplyUpperBodyMotion(FCompactPose& Pose) const
    {
        static const FName BodyName(TEXT("Body_Jiggle"));
        static const FName HeadName(TEXT("Head_Slide"));
        const FBoneContainer& Bones = Pose.GetBoneContainer();
        const auto BodyIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(BodyName)));
        if (!Pose.IsValidIndex(BodyIndex))
        {
            return;
        }
        const auto ParentIndex = Pose.GetParentBoneIndex(BodyIndex);
        if (!Pose.IsValidIndex(ParentIndex))
        {
            return;
        }

        // Delay the upper body behind the step and landing beats. Work in the
        // torso's parent space so the extra motion never changes the root or feet.
        const double StepPhase = Phase * UE_TWO_PI;
        const double Roll = FMath::Sin(StepPhase - UE_PI * 0.25) * FMath::Lerp(1.8, 2.8, SprintWeight) * Jiggle;
        const double Pitch = FMath::Sin(2.0 * StepPhase - UE_PI * 0.45) * FMath::Lerp(0.8, 1.5, SprintWeight) * Jiggle;
        const double Bounce = FMath::Sin(2.0 * StepPhase - UE_PI * 0.65) * FMath::Lerp(0.40, 0.85, SprintWeight) * Jiggle;

        const FQuat ParentRotation = Pose[ParentIndex].GetRotation();
        const FQuat RollDelta(ParentRotation.UnrotateVector(FVector::RightVector), FMath::DegreesToRadians(Roll));
        const FQuat PitchDelta(ParentRotation.UnrotateVector(FVector::ForwardVector), FMath::DegreesToRadians(Pitch));
        // Lean toward the weapon through the torso and head while the flippers stay on their hold.
        const FVector LeanAxis = ParentRotation.UnrotateVector(BodyForwardInMesh);
        const FQuat TorsoLean(LeanAxis, FMath::DegreesToRadians(-5.0 * AimLeanWeight));
        const FQuat GaitDelta = RollDelta * PitchDelta;
        const FQuat TorsoDelta = TorsoLean * GaitDelta;
        const FVector Offset = ParentRotation.UnrotateVector(FVector(0, 0, Bounce));

        FTransform& Body = Pose[BodyIndex];
        const FVector Hinge = Body.GetTranslation();
        Body.SetRotation((TorsoDelta * Body.GetRotation()).GetNormalized());
        Body.AddToTranslation(Offset);

        const auto HeadIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Bones.GetPoseBoneIndexForBoneName(HeadName)));
        if (Pose.IsValidIndex(HeadIndex) && Pose.GetParentBoneIndex(HeadIndex) == ParentIndex)
        {
            FTransform& Head = Pose[HeadIndex];
            Head.SetTranslation(Hinge + TorsoDelta.RotateVector(Head.GetTranslation() - Hinge) + Offset);
            const FQuat HeadLean(LeanAxis, FMath::DegreesToRadians(-8.0 * AimLeanWeight));
            const FQuat HeadDelta = HeadLean * FQuat::Slerp(FQuat::Identity, GaitDelta, 0.55);
            Head.SetRotation((HeadDelta * Head.GetRotation()).GetNormalized());
        }
    }

    const UAnimSequence* Walk = nullptr;
    const UAnimSequence* Sprint = nullptr;
    const UAnimSequence* Slide = nullptr;
    const UAnimSequence* Swim = nullptr;
    const UAnimSequence* Hold = nullptr;
    float Phase = 0.0f;
    float SprintWeight = 0.0f;
    float MovingWeight = 0.0f;
    float Jiggle = 0.0f;
    float AimLeanWeight = 0.0f;
    float SlideTime = 0.0f;
    float SlideWeight = 0.0f;
    float SwimWeight = 0.0f;
    float StrokeWeight = 0.0f;
    float StrokePhase = 0.0f;
    float HoldWeight = 0.0f;
    bool bAimWeapon = false;
    bool bUseFittedFlipperTransforms = false;
    FVector HoldHingeOnFlipper = FVector::ZeroVector;
    double ViewPitch = 0.0;
    FVector BodyForwardInMesh = FVector::ForwardVector;
    FVector BodyRightInMesh = FVector::RightVector;
    FVector BodyUpInMesh = FVector::UpVector;
    FVector BoreOnFlipper = FVector::ForwardVector;
};

FAnimInstanceProxy* UPenguinAnimInstance::CreateAnimInstanceProxy()
{
    return new FPenguinAnimProxy(this);
}

void UPenguinAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete Proxy;
}
