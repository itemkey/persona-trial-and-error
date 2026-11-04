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
struct FPersonaGaitPlayer : FAnimNode_BlendSpacePlayer_Standalone
{
    bool ExplicitPhase=false;
    virtual bool IsEvaluator() const override { return ExplicitPhase; }
    virtual bool ShouldTeleportToTime() const override { return ExplicitPhase; }
};
struct FPersonaAnimProxy : FAnimInstanceProxy
{
    FPersonaGaitPlayer Walk, CrouchWalk;
    FAnimNode_SequencePlayer_Standalone Jump;
    FAnimNode_SequencePlayer_Standalone Hands;
    FAnimNode_SequencePlayer_Standalone Attack, Hit, HitReference, Land, Stance, Climb, Death;
    bool bNPC=false,bTransition=false;
    float GaitPhase=0,PoseDelta=.016f,TransitionAge=1,TransitionDuration=.2f;
    int32 LastAttackSequence=0;
    TArray<FTransform> LastPose,OlderPose,PoseOffsets;
    TArray<FVector> PoseVelocities,AngularVelocities;
    TArray<FTransform> DeathSourcePose;
    FVector FootAnchors[2]={FVector::ZeroVector,FVector::ZeroVector}; bool FootPlanted[2]={false,false};
    FTransform WorldMesh;
    bool FeetHaveGround[2]={false,false}; float FeetGroundZ[2]={0,0},FootContactWeight[2]={0,0}; FVector FeetNormals[2]={FVector::UpVector,FVector::UpVector};
    bool bHasCrouch=false,bClimbing=false,bDying=false;
    float StanceAge=1,ClimbWeight=0,DeathWeight=0;
    FVector FeetOffset[2]={FVector::ZeroVector,FVector::ZeroVector};
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
        CrouchWalk.SetBlendSpace(A->CrouchLocomotion); CrouchWalk.SetLoop(true);
        Stance.SetSequence(A->CrouchEnterClip); Stance.SetLoopAnimation(false);
        Climb.SetSequence(A->ClimbClip); Climb.SetLoopAnimation(false);
        Death.SetSequence(A->DeathClips.IsEmpty() ? nullptr : A->DeathClips[0].Get()); Death.SetLoopAnimation(false);
        Jump.SetSequence(A->JumpClip); Jump.SetLoopAnimation(false);
        Hands.SetSequence(A->HandsClip); Hands.SetLoopAnimation(true);
        Attack.SetSequence(A->AttackClips.IsEmpty() ? nullptr : A->AttackClips[0].Get()); Attack.SetLoopAnimation(false);
        Hit.SetSequence(A->HitClip); Hit.SetLoopAnimation(false);
        HitReference.SetSequence(A->HitClip); HitReference.SetPlayRate(0);
        Land.SetSequence(A->LandClip); Land.SetLoopAnimation(false);
        FAnimationInitializeContext Context(this);
        Walk.Initialize_AnyThread(Context); Jump.Initialize_AnyThread(Context); Hands.Initialize_AnyThread(Context);
        Attack.Initialize_AnyThread(Context); Hit.Initialize_AnyThread(Context); Land.Initialize_AnyThread(Context); CrouchWalk.Initialize_AnyThread(Context); Stance.Initialize_AnyThread(Context); Climb.Initialize_AnyThread(Context); Death.Initialize_AnyThread(Context);
        HitReference.Initialize_AnyThread(Context);
    }
    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        auto A = CastChecked<UPersonaAnimInstance>(Instance);
        if(auto C=Cast<APersonaCharacter>(Instance->TryGetPawnOwner())) bNPC=C->bNPC;
        PoseDelta=FMath::Max(.001f,Delta); TransitionAge+=Delta;
        Speed = bNPC ? FMath::FInterpTo(Speed,A->Speed,Delta,12) : A->Speed;
        Direction=bNPC ? FMath::FixedTurn(Direction,A->Direction,Delta*540) : A->Direction;
        const float CrouchStride=FMath::Lerp(144.f,100.f,FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Direction))));
        if(bNPC && A->Speed>2) GaitPhase=FMath::Fmod(GaitPhase+Delta*A->Speed/(A->bCrouched ? CrouchStride : FMath::Lerp(160.f,360.f,FMath::Clamp((A->Speed-220)/930,0.f,1.f))),1.f);
        bHasCrouch=A->CrouchLocomotion!=nullptr;
        if(bCrouched!=A->bCrouched) { bTransition=true; StanceAge=0; Stance.SetSequence(A->bCrouched ? A->CrouchEnterClip : A->CrouchExitClip); Stance.SetAccumulatedTime(0); }
        StanceAge+=Delta; bCrouched=A->bCrouched;
        if (A->bFalling && !bFalling) Jump.SetAccumulatedTime(0);
        if(!A->bFalling && bFalling) { Land.SetAccumulatedTime(0); LandWeight=1; }
        LandWeight=FMath::Max(0.f,LandWeight-Delta*3);
        bFalling = A->bFalling;
        if (auto C = Cast<APersonaCharacter>(Instance->TryGetPawnOwner()))
        {
            TransitionDuration=FMath::Clamp(C->CombatSettings()->PoseTransitionTime,.05f,.5f);
            bHands = Instance->GetSkelMeshComponent() == C->FirstPersonArms;
            FrozenPose=(bHands || !C->bDead) ? TArray<FTransform>() : !C->CorpsePose.IsEmpty() ? C->CorpsePose : C->DisplayCorpsePose;
            if(bClimbing!=C->Traversal.Active) bTransition=true;
            bClimbing=C->Traversal.Active; ClimbWeight=FMath::FInterpTo(ClimbWeight,bClimbing ? 1.f : 0.f,Delta,10);
            if(bClimbing && A->ClimbClip) { Climb.SetPlayRate(0); Climb.SetAccumulatedTime(FMath::Clamp((C->ServerTime()-C->Traversal.Started)/C->Traversal.Duration,0.f,1.f)*A->ClimbClip->GetPlayLength()); }
            const bool Dying=C->bDead && A->DeathClips.IsValidIndex(C->DeathState.Variant) && C->DeathState.BlendDelay>0 && !C->bRagdollStarted && FrozenPose.IsEmpty();
            if(Dying && !bDying) DeathSourcePose=C->CaptureCorpsePose();
            bDying=Dying;
            DeathWeight=0;
            if(bDying) { Death.SetSequence(A->DeathClips[C->DeathState.Variant]); Death.SetPlayRate(0); const float Age=C->ServerTime()-C->DeathState.Start; Death.SetAccumulatedTime(FMath::Max(0.f,Age)); DeathWeight=FMath::Clamp(Age/.12f,0.f,1.f); }
            const float Now=C->ServerTime();
            const auto& State=C->AttackState;
            if(State.Sequence!=LastAttackSequence) { bTransition=true; LastAttackSequence=State.Sequence; }
            bHasAttack=A->AttackClips.IsValidIndex(State.Variant) && A->AttackClips[State.Variant]!=nullptr;
            AttackWeight=0;
            if(State.Playing && bHasAttack)
            {
                auto Clip=A->AttackClips[State.Variant].Get(); Attack.SetSequence(Clip);
                const float Duration=State.Windup+State.Active+State.Recovery, Elapsed=FMath::Max(0.f,Now-State.Start);
                const auto Profile=C->CombatSettings();
                const float Contact=Profile->StrikeFractions.IsValidIndex(State.Variant) ? Profile->StrikeFractions[State.Variant] : Profile->ClipStrikeFraction;
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
            // A new landing or an explicit placement needs fresh world-space
            // anchors. Never pull a foot back to the previous platform.
            if(bNPC && (bFalling || bClimbing || C->bDead ||
                FVector::DistSquared(Mesh.GetLocation(),WorldMesh.GetLocation())>FMath::Square(100.f)))
                for(int32 Side=0;Side<2;++Side) { FootPlanted[Side]=false; FootContactWeight[Side]=0; }
            WorldMesh=Mesh;
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
            if(!bHands)
            {
                for(int32 Side=0;Side<2;++Side)
                {
                    const FName Bone=Side==0 ? TEXT("foot_l") : TEXT("foot_r");
                    const FVector Foot=C->GetMesh()->GetBoneLocation(Bone); FHitResult Ground;
                    FCollisionQueryParams Q(SCENE_QUERY_STAT(StanceFeet),false,C);
                    FVector Offset=FVector::ZeroVector;
                    bool Plant=false;
                    FeetHaveGround[Side]=!C->bDead && !bClimbing && !bFalling && C->GetWorld()->LineTraceSingleByChannel(Ground,Foot+FVector(0,0,18),Foot-FVector(0,0,38),ECC_Visibility,Q);
                    if(FeetHaveGround[Side]) { FeetGroundZ[Side]=Ground.ImpactPoint.Z; FeetNormals[Side]=Ground.ImpactNormal; }
                    if(!bNPC && FeetHaveGround[Side])
                    {
                        const float Sole=bNPC ? 11.34f : 5.f;
                        const float Phase=FMath::Fmod(GaitPhase+Side*.5f,1.f);
                        Plant=!bNPC || Speed<5 || (bCrouched ? Phase<.62f : Foot.Z-Ground.ImpactPoint.Z<Sole+3);
                        if(Plant)
                        {
                            if(!FootPlanted[Side] || FVector::Dist2D(FootAnchors[Side],Foot)>12) FootAnchors[Side]=Foot;
                            FootAnchors[Side].Z=Ground.ImpactPoint.Z+Sole;
                            const FVector Target=bNPC ? FootAnchors[Side] : FVector(Foot.X,Foot.Y,Ground.ImpactPoint.Z+Sole);
                            Offset=Mesh.InverseTransformVectorNoScale((Target-Foot).GetClampedToMaxSize(12));
                        }
                    }
                    if(!bNPC) FootPlanted[Side]=Plant;
                    FeetOffset[Side]=FMath::VInterpTo(FeetOffset[Side],Offset,Delta,12);
                    if(bClimbing)
                    {
                        const FVector Forward=(C->Traversal.End-C->Traversal.Start).GetSafeNormal2D();
                        const FVector Across=FVector::CrossProduct(FVector::UpVector,Forward)*(Side==0 ? -22.f : 22.f);
                        HandTargets[Side]=Mesh.InverseTransformPosition(C->Traversal.Ledge-Forward*35+Across+FVector(0,0,5));
                        ElbowTargets[Side]=HandTargets[Side]-FVector(0,20,30);
                    }
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
        Attack.CacheBones_AnyThread(Context); Hit.CacheBones_AnyThread(Context); Land.CacheBones_AnyThread(Context); CrouchWalk.CacheBones_AnyThread(Context); Stance.CacheBones_AnyThread(Context); Climb.CacheBones_AnyThread(Context); Death.CacheBones_AnyThread(Context);
        HitReference.CacheBones_AnyThread(Context);
    }
    virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override
    {
        // The bundled locomotion asset uses Direction (X), Speed (Y).
        Walk.SetPosition(FVector(Direction, Speed, 0)); CrouchWalk.SetPosition(FVector(Direction,Speed*(bNPC ? 1.f : 1.25f),0));
        Walk.SetPlayRate(bNPC ? 0.f : FMath::Max(1.f, Speed / 600.f));
        Walk.ExplicitPhase=CrouchWalk.ExplicitPhase=bNPC;
        if(bNPC) { CrouchWalk.SetPlayRate(0); Walk.SetAccumulatedTime(GaitPhase); CrouchWalk.SetAccumulatedTime(GaitPhase); }
        Walk.Update_AnyThread(Context); Jump.Update_AnyThread(Context); Hands.Update_AnyThread(Context);
        Attack.Update_AnyThread(Context); Hit.Update_AnyThread(Context); Land.Update_AnyThread(Context); CrouchWalk.Update_AnyThread(Context); Stance.Update_AnyThread(Context); Climb.Update_AnyThread(Context); Death.Update_AnyThread(Context);
        HitReference.Update_AnyThread(Context);
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
        if(!bHands && bHasCrouch && CrouchAlpha>.001f)
        {
            FPoseContext Crouch(this); CrouchWalk.Evaluate_AnyThread(Crouch);
            FAnimationPoseData Result(Output),Other(Crouch); FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-CrouchAlpha);
        }
        if(!bHands && StanceAge<.32f && Speed<25 && Stance.GetSequence())
        {
            FPoseContext Pose(this); Stance.Evaluate_AnyThread(Pose);
            const float Weight=FMath::Min(FMath::Clamp(StanceAge/.08f,0.f,1.f),FMath::Clamp((.32f-StanceAge)/.08f,0.f,1.f));
            FAnimationPoseData Result(Output),Other(Pose); FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-Weight);
        }
        if(!bHands && ClimbWeight>.001f && Climb.GetSequence())
        {
            FPoseContext Pose(this); Climb.Evaluate_AnyThread(Pose);
            FAnimationPoseData Result(Output),Other(Pose); FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-ClimbWeight);
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
                for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
                {
                    const auto Name=Output.Pose.GetBoneContainer().GetReferenceSkeleton().GetBoneName(Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Index).GetInt());
                    const auto Region=PersonaBodyRegion(Name);
                    const bool Legs=Region==EPersonaBodyRegion::LeftLeg || Region==EPersonaBodyRegion::RightLeg || Name==TEXT("pelvis");
                    const float Weight=bNPC && Legs ? AttackWeight*(1-FMath::Clamp(Speed/80.f,0.f,1.f)) : AttackWeight;
                    if(Name!=TEXT("root")) Output.Pose[Index].BlendWith(Pose.Pose[Index],Weight);
                }
            }
            if(bHasHit && HitWeight>.001f)
            {
                FPoseContext Pose(this),Reference(this); Hit.Evaluate_AnyThread(Pose); HitReference.Evaluate_AnyThread(Reference);
                for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
                {
                    const FName Name=Output.Pose.GetBoneContainer().GetReferenceSkeleton().GetBoneName(
                        Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Index).GetInt());
                    if(PersonaBodyRegion(Name)==HitRegion && Name!=TEXT("root") && Name!=TEXT("pelvis"))
                    {
                        if(bNPC)
                        {
                            FQuat Delta=Reference.Pose[Index].GetRotation().Inverse()*Pose.Pose[Index].GetRotation();
                            if(Delta.W<0) Delta=Delta*-1;
                            const FVector Limited=Delta.ToRotationVector().GetClampedToMaxSize(FMath::DegreesToRadians(18.f));
                            Delta=Limited.IsNearlyZero() ? FQuat::Identity : FQuat(Limited.GetSafeNormal(),Limited.Size());
                            Output.Pose[Index].SetRotation((Output.Pose[Index].GetRotation()*FQuat::Slerp(FQuat::Identity,Delta,HitWeight)).GetNormalized());
                        }
                        else Output.Pose[Index].BlendWith(Pose.Pose[Index],HitWeight);
                    }
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
        if (bHands || CarryAlpha > .001f || ClimbWeight>.001f)
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
                const float Alpha = bHands ? 1.f : FMath::Max(CarryAlpha,ClimbWeight);
                Output.Pose[Upper].BlendWith(NewUpper, Alpha); Output.Pose[Lower].BlendWith(NewLower, Alpha); Output.Pose[Hand].BlendWith(NewHand, Alpha);
            }
        }
        if(!bHands && DeathWeight>.001f)
        {
            FPoseContext Pose(this); Death.Evaluate_AnyThread(Pose);
            if(bNPC && DeathSourcePose.Num()==Output.Pose.GetNumBones()) for(FCompactPoseBoneIndex I:Output.Pose.ForEachBoneIndex()) Output.Pose[I]=DeathSourcePose[I.GetInt()];
            FAnimationPoseData Result(Output),Other(Pose); FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,Other,1-DeathWeight);
        }
        if(bNPC && !bHands && !bDying)
        {
            const int32 Count=Output.Pose.GetNumBones();
            if(bTransition && LastPose.Num()==Count)
            {
                TransitionAge=0; PoseOffsets.SetNum(Count); PoseVelocities.SetNumZeroed(Count); AngularVelocities.SetNumZeroed(Count);
                for(FCompactPoseBoneIndex I:Output.Pose.ForEachBoneIndex())
                {
                    const int32 N=I.GetInt();
                    PoseOffsets[N]=FTransform((LastPose[N].GetRotation()*Output.Pose[I].GetRotation().Inverse()).GetNormalized(),LastPose[N].GetLocation()-Output.Pose[I].GetLocation());
                    if(OlderPose.Num()==Count)
                    {
                        PoseVelocities[N]=((LastPose[N].GetLocation()-OlderPose[N].GetLocation())/PoseDelta).GetClampedToMaxSize(100);
                        FQuat Q=LastPose[N].GetRotation()*OlderPose[N].GetRotation().Inverse(); if(Q.W<0) Q=Q*-1;
                        AngularVelocities[N]=(Q.ToRotationVector()/PoseDelta).GetClampedToMaxSize(8);
                    }
                }
            }
            bTransition=false;
            const float S=FMath::Clamp(TransitionAge/TransitionDuration,0.f,1.f),A=(1+2*S)*FMath::Square(1-S),B=S*FMath::Square(1-S)*TransitionDuration;
            if(PoseOffsets.Num()==Count && S<1)
                for(FCompactPoseBoneIndex I:Output.Pose.ForEachBoneIndex())
                {
                    const int32 N=I.GetInt();
                    Output.Pose[I].AddToTranslation(PoseOffsets[N].GetLocation()*A+PoseVelocities[N]*B);
                    const FVector W=AngularVelocities[N]*B;
                    const FQuat V=W.IsNearlyZero() ? FQuat::Identity : FQuat(W.GetSafeNormal(),W.Size());
                    Output.Pose[I].SetRotation((V*FQuat::Slerp(FQuat::Identity,PoseOffsets[N].GetRotation(),A)*Output.Pose[I].GetRotation()).GetNormalized());
                }
            OlderPose=LastPose; LastPose.SetNum(Count);
            for(FCompactPoseBoneIndex I:Output.Pose.ForEachBoneIndex()) LastPose[I.GetInt()]=Output.Pose[I];
        }
        if(!bHands && !bDying && !bClimbing && AirAlpha<.1f)
        {
            for(int32 Side=0;Side<2;++Side)
            {
                FCSPose<FCompactPose> Pose; Pose.InitPose(Output.Pose);
                auto Bone=[&](const FString& Name) { int32 I=Output.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(FName(Name)); return I<0 ? FCompactPoseBoneIndex(INDEX_NONE) : Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(I)); };
                const FString S=Side==0 ? TEXT("_l") : TEXT("_r");
                auto Hip=Bone(TEXT("thigh")+S),Knee=Bone(TEXT("calf")+S),Foot=Bone(TEXT("foot")+S);
                if(Hip==INDEX_NONE || Knee==INDEX_NONE || Foot==INDEX_NONE) continue;
                auto A=Pose.GetComponentSpaceTransform(Hip),B=Pose.GetComponentSpaceTransform(Knee),C=Pose.GetComponentSpaceTransform(Foot); const FQuat Rotation=C.GetRotation();
                const FVector Axis=(C.GetLocation()-A.GetLocation()).GetSafeNormal();
                FVector Bend=B.GetLocation()-A.GetLocation(); Bend=(Bend-Axis*FVector::DotProduct(Bend,Axis)).GetSafeNormal();
                if(Bend.IsNearlyZero()) Bend=FVector(0,1,0);
                FVector Target=C.GetLocation()+FeetOffset[Side];
                if(bNPC)
                {
                    const FVector Raw=WorldMesh.TransformPosition(C.GetLocation());
                    const float Phase=FMath::Fmod(GaitPhase+Side*.5f,1.f);
                    const float Contact=bCrouched ? .62f : FMath::Lerp(.5f,.3f,FMath::Clamp((Speed-220)/930,0.f,1.f));
                    const bool Plant=FeetHaveGround[Side] && (Speed<5 || Phase<Contact) && Raw.Z<FeetGroundZ[Side]+22;
                    if(Plant && !FootPlanted[Side]) FootAnchors[Side]=Raw;
                    if(Plant) FootAnchors[Side].Z=FeetGroundZ[Side]+11.34f;
                    FootPlanted[Side]=Plant;
                    FootContactWeight[Side]=FMath::FInterpTo(FootContactWeight[Side],Plant ? 1.f : 0.f,PoseDelta,25);
                    FVector Grounded=Raw;
                    if(FeetHaveGround[Side] && Raw.Z<FeetGroundZ[Side]+19) Grounded.Z=FeetGroundZ[Side]+11.34f;
                    const FVector Desired=WorldMesh.InverseTransformPosition(FMath::Lerp(Grounded,FootAnchors[Side],FootContactWeight[Side]));
                    Target=C.GetLocation()+(Desired-C.GetLocation()).GetClampedToMaxSize(45);
                }
                AnimationCore::SolveTwoBoneIK(A,B,C,B.GetLocation()+Bend*50,Target,false,1,1); C.SetRotation(bNPC && FeetHaveGround[Side] ? WorldMesh.GetRotation().Inverse()*FQuat::FindBetweenNormals(FVector::UpVector,FeetNormals[Side])*WorldMesh.GetRotation()*Rotation : Rotation);
                Output.Pose[Hip]=A.GetRelativeTransform(Pose.GetComponentSpaceTransform(Output.Pose.GetParentBoneIndex(Hip)));
                Output.Pose[Knee]=B.GetRelativeTransform(A); Output.Pose[Foot]=C.GetRelativeTransform(B);
            }
        }
        return true;
    }
};
void UPersonaAnimInstance::NativeInitializeAnimation()
{
    if(auto C=Cast<APersonaCharacter>(TryGetPawnOwner()); C && C->bNPC)
    {
        Locomotion=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Persona/Animation/NPC/BS_Locomotion.BS_Locomotion"));
        CrouchLocomotion=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Persona/Animation/NPC/BS_Crouch.BS_Crouch"));
        CrouchEnterClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/NPC/CrouchEnter.CrouchEnter"));
        CrouchExitClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/NPC/CrouchExit.CrouchExit"));
        AttackClips.Reset(); DeathClips.Reset();
        for(const FString Name:{FString(TEXT("MM_Attack_01")),FString(TEXT("MM_Attack_02")),FString(TEXT("MM_Attack_03")),FString(TEXT("MM_ChargedAttack"))})
            AttackClips.Add(LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/NPC/P_")+Name+TEXT(".P_")+Name)));
        for(const FString Name:{FString(TEXT("MM_Death_Back_01")),FString(TEXT("MM_Death_Front_01")),FString(TEXT("MM_Death_Right_01")),FString(TEXT("MM_Death_Left_01"))})
            DeathClips.Add(LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/NPC/P_")+Name+TEXT(".P_")+Name)));
        HitClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/NPC/P_MM_HitReact_Front_Med_01.P_MM_HitReact_Front_Med_01"));
        LandClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/NPC/P_MM_Land.P_MM_Land"));
        JumpClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/NPC/P_MM_Jump.P_MM_Jump"));
    }
    if (!Locomotion) Locomotion = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_BS_Idle_Walk_Run.P_BS_Idle_Walk_Run"));
    if (!JumpClip) JumpClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MM_Jump.P_MM_Jump"));
    if (!HandsClip) HandsClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MF_Pistol_Idle_ADS.P_MF_Pistol_Idle_ADS"));
    if(AttackClips.IsEmpty())
        for(const FString Name:{FString(TEXT("MM_Attack_01")),FString(TEXT("MM_Attack_02")),FString(TEXT("MM_Attack_03")),FString(TEXT("MM_ChargedAttack"))})
            AttackClips.Add(LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/Retargeted/P_")+Name+TEXT(".P_")+Name)));
    if(!HitClip) HitClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Retargeted/P_MM_HitReact_Front_Med_01.P_MM_HitReact_Front_Med_01"));
    if(!LandClip) LandClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Retargeted/P_MM_Land.P_MM_Land"));
    if(!CrouchLocomotion) CrouchLocomotion=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Persona/Animation/Stances/BS_Crouch.BS_Crouch"));
    if(!CrouchEnterClip) CrouchEnterClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Stances/CrouchEnter.CrouchEnter"));
    if(!CrouchExitClip) CrouchExitClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Stances/CrouchExit.CrouchExit"));
    if(!ClimbClip) ClimbClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Persona/Animation/Stances/Climb.Climb"));
    if(DeathClips.IsEmpty()) for(const FString Name:{FString(TEXT("MM_Death_Back_01")),FString(TEXT("MM_Death_Front_01")),FString(TEXT("MM_Death_Right_01")),FString(TEXT("MM_Death_Left_01"))})
        DeathClips.Add(LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/Retargeted/P_")+Name+TEXT(".P_")+Name)));
    Super::NativeInitializeAnimation();
}
FAnimInstanceProxy* UPersonaAnimInstance::CreateAnimInstanceProxy() { return new FPersonaAnimProxy(this); }
void UPersonaAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
void UPersonaAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    const auto& Proxy=GetProxyOnGameThread<FPersonaAnimProxy>();
    GaitPhase=Proxy.GaitPhase; FootContact[0]=Proxy.FootPlanted[0]; FootContact[1]=Proxy.FootPlanted[1];
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
