#include "PersonaCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "TwoBoneIK.h"
#include "PersonaInteraction.h"
#include "Camera/CameraComponent.h"

// A native graph keeps the first foundation reproducible. Assets and animation
// inputs remain editable in the derived Animation Blueprint.
struct FPersonaAnimProxy : FAnimInstanceProxy
{
    FAnimNode_BlendSpacePlayer_Standalone Walk;
    FAnimNode_SequencePlayer_Standalone Jump;
    FAnimNode_SequencePlayer_Standalone Hands;
    FAnimNode_SequencePlayer_Standalone Attack, Hit, Land;
    float AttackWeight=0, HitWeight=0, LandWeight=0, TurnLean=0, LastYaw=0;
    float AccelerationLean=0;
    FVector PreviousVelocity=FVector::ZeroVector;
    EPersonaBodyRegion HitRegion=EPersonaBodyRegion::Torso;
    bool bHasAttack=false, bHasHit=false, bHasLand=false;
    float Speed = 0, Direction = 0, CrouchAlpha = 0, AirAlpha = 0;
    bool bHands = false, bFalling = false, bCrouched = false;
    float CarryAlpha = 0;
    bool bTargetsInitialized = false;
    FVector HandTargets[2], ElbowTargets[2];
    TArray<FTransform> FrozenPose;
    FPersonaAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void Initialize(UAnimInstance* Instance) override
    {
        FAnimInstanceProxy::Initialize(Instance);
        auto A = CastChecked<UPersonaAnimInstance>(Instance);
        A->NativeInitializeAnimation();
        Walk.SetBlendSpace(A->Locomotion); Walk.SetLoop(true);
        Jump.SetSequence(A->JumpClip); Jump.SetLoopAnimation(false);
        Hands.SetSequence(A->HandsClip); Hands.SetLoopAnimation(true);
        Attack.SetSequence(A->AttackClips.IsEmpty() ? nullptr : A->AttackClips[0].Get()); Attack.SetLoopAnimation(false);
        Hit.SetSequence(A->HitClip); Hit.SetLoopAnimation(false);
        Land.SetSequence(A->LandClip); Land.SetLoopAnimation(false);
        FAnimationInitializeContext Context(this);
        Walk.Initialize_AnyThread(Context); Jump.Initialize_AnyThread(Context); Hands.Initialize_AnyThread(Context);
        Attack.Initialize_AnyThread(Context); Hit.Initialize_AnyThread(Context); Land.Initialize_AnyThread(Context);
    }
    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        auto A = CastChecked<UPersonaAnimInstance>(Instance);
        Speed = A->Speed; Direction = A->Direction; bCrouched = A->bCrouched;
        if (A->bFalling && !bFalling) Jump.SetAccumulatedTime(0);
        if(!A->bFalling && bFalling) { Land.SetAccumulatedTime(0); LandWeight=1; }
        LandWeight=FMath::Max(0.f,LandWeight-Delta*3);
        bFalling = A->bFalling;
        if (auto C = Cast<APersonaCharacter>(Instance->TryGetPawnOwner()))
        {
            bHands = Instance->GetSkelMeshComponent() == C->FirstPersonArms;
            FrozenPose=bHands ? TArray<FTransform>() : C->CorpsePose;
            const float Now=C->ServerTime();
            const auto& State=C->AttackState;
            bHasAttack=A->AttackClips.IsValidIndex(State.Variant) && A->AttackClips[State.Variant]!=nullptr;
            AttackWeight=0;
            if(State.Playing && bHasAttack)
            {
                auto Clip=A->AttackClips[State.Variant].Get(); Attack.SetSequence(Clip);
                const float Duration=State.Windup+State.Active+State.Recovery, Elapsed=FMath::Max(0.f,Now-State.Start);
                const float Contact=State.Variant==3 ? C->CombatSettings()->HeavyClipStrikeFraction : C->CombatSettings()->ClipStrikeFraction;
                float ClipTime;
                if(Elapsed<State.Windup) ClipTime=(Contact-.05f)*Elapsed/State.Windup;
                else if(Elapsed<State.Windup+State.Active) ClipTime=Contact-.05f+.1f*(Elapsed-State.Windup)/State.Active;
                else ClipTime=Contact+.05f+(1-Contact-.05f)*(Elapsed-State.Windup-State.Active)/State.Recovery;
                Attack.SetAccumulatedTime(FMath::Clamp(ClipTime,0.f,1.f)*Clip->GetPlayLength());
                Attack.SetPlayRate(0);
                AttackWeight=FMath::Min(FMath::Clamp(Elapsed/.08f,0.f,1.f),FMath::Clamp((Duration-Elapsed)/.15f,0.f,1.f));
            }
            bHasHit=A->HitClip!=nullptr; bHasLand=A->LandClip!=nullptr; HitRegion=C->Reaction.Region;
            const float HitElapsed=Now-C->Reaction.Start;
            HitWeight=C->Reaction.Sequence && HitElapsed>=0 && HitElapsed<C->Reaction.Duration
                ? FMath::Sin(HitElapsed/C->Reaction.Duration*PI)*.55f : 0;
            if(bHasHit) { Hit.SetAccumulatedTime(FMath::Clamp(HitElapsed/C->Reaction.Duration,0.f,1.f)*A->HitClip->GetPlayLength()); Hit.SetPlayRate(0); }
            const float Yaw=C->GetActorRotation().Yaw;
            const float LeanTarget=FMath::Clamp(FMath::FindDeltaAngleDegrees(LastYaw,Yaw)/FMath::Max(Delta,.001f)*.015f,-5.f,5.f)*FMath::Clamp(Speed/600.f,0.f,1.f);
            TurnLean=FMath::FInterpTo(TurnLean,LeanTarget,Delta,8); LastYaw=Yaw;
            const FVector Acceleration=(C->GetVelocity()-PreviousVelocity)/FMath::Max(Delta,.001f);
            const float ForwardAcceleration=FVector::DotProduct(Acceleration,C->GetActorForwardVector());
            AccelerationLean=FMath::FInterpTo(AccelerationLean,bFalling ? 0.f : FMath::Clamp(ForwardAcceleration*.001f,-3.f,3.f),Delta,12);
            PreviousVelocity=C->GetVelocity();
            CarryAlpha = C->CarryAlpha;
            const FTransform Mesh = Instance->GetSkelMeshComponent()->GetComponentTransform();
            const FTransform View = bHands ? C->Camera->GetComponentTransform() : FTransform(C->GetBaseAimRotation(), C->GetPawnViewLocation());
            const FTransform Carry = C->GetCarryTransform(bHands);
            for (int32 Side = 0; Side < 2; ++Side)
            {
                const float Sign = Side == 0 ? -1.f : 1.f;
                // Restrained shoulder/elbow arcs, opposite on each side. Keep
                // the wrists below the sightline, including at maximum sprint.
                const float Phase = C->HandMotionTime + Side*PI;
                const float Swing = FMath::Sin(Phase) * C->HandMoveAlpha;
                const FVector Rest(22 + C->HandMoveAlpha * (C->bSprinting ? 10.f : 7.f) + Swing*(C->bSprinting ? 3.0f : 2.0f),
                    Sign*(26-C->HandMoveAlpha*2) + Swing*.45f,
                    -74 + C->HandMoveAlpha*(C->bSprinting ? 46.f : 47.f) + FMath::Cos(Phase)*C->HandMoveAlpha*1.1f + C->HandAirOffset);
                auto Item = C->Interaction->Held;
                const FVector Contact = Item ? Item->Grip(Side==0) : FVector(0,Sign*23,-4);
                if (bHands)
                {
                    // Entire solve in camera-local space: no dependency on the
                    // world camera transform from a different tick or net frame.
                    const FTransform LocalMesh = C->FirstPersonArms->GetRelativeTransform();
                    const FTransform LocalCarry = Item ? Item->CarryLocal() : FTransform(FVector(65,0,-23));
                    const FVector Held = LocalCarry.TransformPositionNoScale(Contact);
                    const FVector Desired = LocalMesh.InverseTransformPosition(FMath::Lerp(Rest,Held,CarryAlpha));
                    HandTargets[Side] = bTargetsInitialized ? FMath::VInterpTo(HandTargets[Side],Desired,Delta,18.f) : Desired;
                    const FVector Elbow(-8,Sign*34,-48 + Swing*.8f);
                    ElbowTargets[Side] = LocalMesh.InverseTransformPosition(Elbow);
                }
                else
                {
                    HandTargets[Side] = Mesh.InverseTransformPosition(Carry.TransformPositionNoScale(Contact));
                    ElbowTargets[Side] = Mesh.InverseTransformPosition(View.TransformPositionNoScale(FVector(-5,Sign*45,-48)));
                }
            }
            bTargetsInitialized = true;
        }
        AirAlpha = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Delta, 10);
        CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouched ? 1.f : 0.f, Delta, 10);
    }
    virtual void CacheBones() override
    {
        FAnimInstanceProxy::CacheBones();
        FAnimationCacheBonesContext Context(this);
        Walk.CacheBones_AnyThread(Context); Jump.CacheBones_AnyThread(Context); Hands.CacheBones_AnyThread(Context);
        Attack.CacheBones_AnyThread(Context); Hit.CacheBones_AnyThread(Context); Land.CacheBones_AnyThread(Context);
    }
    virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override
    {
        // The bundled locomotion asset uses Direction (X), Speed (Y).
        Walk.SetPosition(FVector(Direction, Speed, 0));
        Walk.SetPlayRate(FMath::Max(1.f, Speed / 600.f));
        Walk.Update_AnyThread(Context); Jump.Update_AnyThread(Context); Hands.Update_AnyThread(Context);
        Attack.Update_AnyThread(Context); Hit.Update_AnyThread(Context); Land.Update_AnyThread(Context);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        if(!FrozenPose.IsEmpty())
        {
            Output.ResetToRefPose();
            for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
            { int32 MeshIndex=Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Index).GetInt(); if(FrozenPose.IsValidIndex(MeshIndex)) Output.Pose[Index]=FrozenPose[MeshIndex]; }
            return true;
        }
        // Fixed first-person shoulder base: full-body jump/spine motion must not
        // push an attached viewmodel through the near plane.
        if (bHands) Output.ResetToRefPose(); else Walk.Evaluate_AnyThread(Output);
        if (!bHands && AirAlpha > .001f)
        {
            FPoseContext Air(this); Jump.Evaluate_AnyThread(Air);
            FAnimationPoseData Result(Output), Other(Air);
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result, Other, 1-AirAlpha);
        }
        if (!bHands && CrouchAlpha > .001f)
        {
            // Solve in mesh component space: PERSONA's local bone axes differ
            // from Manny, so fixed local Euler rotations lift the feet sideways.
            auto IndexOf = [&](FName Name)
            {
                int32 MeshIndex = Output.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(Name);
                return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
                    : Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            };
            const auto Pelvis = IndexOf(TEXT("pelvis"));
            if (Pelvis != INDEX_NONE)
            {
                FCSPose<FCompactPose> Original; Original.InitPose(Output.Pose);
                const auto Parent = Output.Pose.GetParentBoneIndex(Pelvis);
                const FVector Drop(0, 0, -40 * CrouchAlpha);
                Output.Pose[Pelvis].AddToTranslation(Parent != INDEX_NONE
                    ? Original.GetComponentSpaceTransform(Parent).InverseTransformVector(Drop) : Drop);
                FCSPose<FCompactPose> Lowered; Lowered.InitPose(Output.Pose);
                for (const FString Side : {FString(TEXT("l")), FString(TEXT("r"))})
                {
                    const auto Thigh = IndexOf(FName(TEXT("thigh_") + Side));
                    const auto Calf = IndexOf(FName(TEXT("calf_") + Side));
                    const auto Foot = IndexOf(FName(TEXT("foot_") + Side));
                    if (Thigh == INDEX_NONE || Calf == INDEX_NONE || Foot == INDEX_NONE) continue;
                    FTransform Hip = Lowered.GetComponentSpaceTransform(Thigh);
                    FTransform Knee = Lowered.GetComponentSpaceTransform(Calf);
                    FTransform Ankle = Lowered.GetComponentSpaceTransform(Foot);
                    const FTransform Planted = Original.GetComponentSpaceTransform(Foot);
                    AnimationCore::SolveTwoBoneIK(Hip, Knee, Ankle,
                        Hip.GetLocation() + FVector(0,100,0), Planted.GetLocation(), false, 1, 1);
                    Ankle.SetRotation(Planted.GetRotation());
                    Output.Pose[Thigh] = Hip.GetRelativeTransform(Lowered.GetComponentSpaceTransform(Output.Pose.GetParentBoneIndex(Thigh)));
                    Output.Pose[Calf] = Knee.GetRelativeTransform(Hip);
                    Output.Pose[Foot] = Ankle.GetRelativeTransform(Knee);
                }
            }
        }
        if(!bHands)
        {
            if(bHasLand && LandWeight>.001f && AirAlpha<.1f)
            {
                FPoseContext Pose(this); Land.Evaluate_AnyThread(Pose);
                FAnimationPoseData Result(Output), Other(Pose);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-LandWeight*.45f);
            }
            if(bHasAttack && AttackWeight>.001f)
            {
                FPoseContext Pose(this); Attack.Evaluate_AnyThread(Pose);
                FAnimationPoseData Result(Output), Other(Pose);
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-AttackWeight);
            }
            if(bHasHit && HitWeight>.001f)
            {
                FPoseContext Pose(this); Hit.Evaluate_AnyThread(Pose);
                for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
                {
                    const FName Name=Output.Pose.GetBoneContainer().GetReferenceSkeleton().GetBoneName(
                        Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Index).GetInt());
                    if(PersonaBodyRegion(Name)==HitRegion) Output.Pose[Index].BlendWith(Pose.Pose[Index],HitWeight);
                }
            }
            const int32 SpineIndex=Output.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(TEXT("spine_01"));
            if(SpineIndex!=INDEX_NONE)
            {
                const auto Spine=Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineIndex));
                FCSPose<FCompactPose> ComponentPose; ComponentPose.InitPose(Output.Pose);
                auto Transform=ComponentPose.GetComponentSpaceTransform(Spine);
                Transform.SetRotation(FQuat(FVector(0,1,0),FMath::DegreesToRadians(TurnLean))*Transform.GetRotation());
                Transform.SetRotation(FQuat(FVector(1,0,0),FMath::DegreesToRadians(-AccelerationLean))*Transform.GetRotation());
                Output.Pose[Spine]=Transform.GetRelativeTransform(ComponentPose.GetComponentSpaceTransform(Output.Pose.GetParentBoneIndex(Spine)));
            }
        }
        if (bHands || CarryAlpha > .001f)
        {
            FCSPose<FCompactPose> Pose; Pose.InitPose(Output.Pose);
            auto Bone = [&](const FString& Name) {
                const int32 Index = Output.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(FName(Name));
                return Index == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
            };
            for (int32 Side = 0; Side < 2; ++Side)
            {
                const FString Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
                const auto Upper = Bone(TEXT("upperarm") + Suffix), Lower = Bone(TEXT("lowerarm") + Suffix), Hand = Bone(TEXT("hand") + Suffix);
                if (Upper == INDEX_NONE || Lower == INDEX_NONE || Hand == INDEX_NONE) continue;
                FTransform Shoulder = Pose.GetComponentSpaceTransform(Upper), Elbow = Pose.GetComponentSpaceTransform(Lower), Wrist = Pose.GetComponentSpaceTransform(Hand);
                const FQuat WristRelative = Elbow.GetRotation().Inverse() * Wrist.GetRotation();
                AnimationCore::SolveTwoBoneIK(Shoulder, Elbow, Wrist, ElbowTargets[Side], HandTargets[Side], false, 1, 1);
                Wrist.SetRotation(Elbow.GetRotation() * WristRelative);
                const FTransform NewUpper = Shoulder.GetRelativeTransform(Pose.GetComponentSpaceTransform(Output.Pose.GetParentBoneIndex(Upper)));
                const FTransform NewLower = Elbow.GetRelativeTransform(Shoulder), NewHand = Wrist.GetRelativeTransform(Elbow);
                const float Alpha = bHands ? 1.f : CarryAlpha;
                Output.Pose[Upper].BlendWith(NewUpper, Alpha); Output.Pose[Lower].BlendWith(NewLower, Alpha); Output.Pose[Hand].BlendWith(NewHand, Alpha);
            }
        }
        return true;
    }
};
void UPersonaAnimInstance::NativeInitializeAnimation()
{
    if (!Locomotion) Locomotion = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_BS_Idle_Walk_Run.P_BS_Idle_Walk_Run"));
    if (!JumpClip) JumpClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MM_Jump.P_MM_Jump"));
    if (!HandsClip) HandsClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MF_Pistol_Idle_ADS.P_MF_Pistol_Idle_ADS"));
    if(AttackClips.IsEmpty())
        for(const FString Name:{FString(TEXT("MM_Attack_01")),FString(TEXT("MM_Attack_02")),FString(TEXT("MM_Attack_03")),FString(TEXT("MM_ChargedAttack"))})
            AttackClips.Add(LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/Retargeted/P_")+Name+TEXT(".P_")+Name)));
    if(!HitClip) HitClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Retargeted/P_MM_HitReact_Front_Med_01.P_MM_HitReact_Front_Med_01"));
    if(!LandClip) LandClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Retargeted/P_MM_Land.P_MM_Land"));
    Super::NativeInitializeAnimation();
}
FAnimInstanceProxy* UPersonaAnimInstance::CreateAnimInstanceProxy() { return new FPersonaAnimProxy(this); }
void UPersonaAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
void UPersonaAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    auto Mesh = GetSkelMeshComponent(); if (!Mesh) return;
    const FTransform Component = Mesh->GetComponentTransform();
    for (const FString Side : {FString(TEXT("l")), FString(TEXT("r"))})
    {
        const FVector Shoulder = Component.InverseTransformPosition(Mesh->GetBoneLocation(FName(TEXT("upperarm_") + Side)));
        const FVector Elbow = Component.InverseTransformPosition(Mesh->GetBoneLocation(FName(TEXT("lowerarm_") + Side)));
        const FVector ArmDirection = (Elbow-Shoulder).GetSafeNormal();
        const float Down = FMath::Clamp(-ArmDirection.Z, 0.f, 1.f);
        const FString Suffix = Side == TEXT("l") ? TEXT("_L") : TEXT("_R");
        for (const FString Prefix : {FString(TEXT("P04_Pictogram_ArmsDown")), FString(TEXT("P05_ShoulderHeight_ArmsDown")), FString(TEXT("P06_AxillaSmooth_ArmsDown"))})
            Mesh->SetMorphTarget(FName(Prefix+Suffix), Down);
    }
}
