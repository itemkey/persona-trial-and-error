#include "PersonaCharacter.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"

TArray<FTransform> APersonaCharacter::CaptureCorpsePose() const
{
    // BoneSpaceTransforms can still contain the animation pose before the
    // physics blend. Snapshot the displayed component pose to avoid a jump
    // when simulation stops or the pose is reproduced by another machine.
    const auto BodyMesh=GetMesh();
    const auto& ComponentPose=BodyMesh->GetComponentSpaceTransforms();
    const auto& Ref=BodyMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    TArray<FTransform> Result; Result.SetNum(ComponentPose.Num());
    for(int32 I=0;I<ComponentPose.Num();++I)
    {
        const int32 Parent=Ref.GetParentIndex(I);
        Result[I]=Parent>=0 ? ComponentPose[I].GetRelativeTransform(ComponentPose[Parent]) : ComponentPose[I];
    }
    return Result;
}

void APersonaCharacter::BeginRagdoll()
{
    if(!HasAuthority() || bRagdollStarted || !bDead || !CorpsePose.IsEmpty()) return;
    bRagdollStarted=true; HitAge=0; CorpseStableTime=0; CorpseRestAnchors.Reset(); PhysicalAnimation->SetStrengthMultiplyer(0);
    auto BodyMesh=GetMesh(); BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    // Latch the displayed pose before evaluating with bRagdollStarted set.
    // Otherwise the graph leaves its death intro, evaluates idle, and visibly
    // snaps upright for the first physics frame.
    const auto BeforePose=BodyMesh->GetComponentSpaceTransforms();
    DisplayCorpsePose=CaptureCorpsePose();
    BodyMesh->TickAnimation(0,false); BodyMesh->RefreshBoneTransforms();
    RagdollStartPoseError=0; RagdollStartAngleError=0;
    const auto& AfterPose=BodyMesh->GetComponentSpaceTransforms();
    if(bNPC && BeforePose.Num()==AfterPose.Num()) for(int32 I=0;I<AfterPose.Num();++I)
    {
        RagdollStartPoseError=FMath::Max(RagdollStartPoseError,float(FVector::Distance(BeforePose[I].GetLocation(),AfterPose[I].GetLocation())));
        RagdollStartAngleError=FMath::Max(RagdollStartAngleError,float(FMath::RadiansToDegrees(BeforePose[I].GetRotation().AngularDistance(AfterPose[I].GetRotation()))));
    }
    BodyMesh->SetAllBodiesPhysicsBlendWeight(1); BodyMesh->SetSimulatePhysics(true); BodyMesh->SetAllBodiesSimulatePhysics(true);
    if(bNPC && DeathState.BlendDelay>0) BodyMesh->SetAllBodiesPhysicsBlendWeight(0);
    BodyMesh->SetAllPhysicsLinearVelocity(DeathState.Velocity);
    BodyMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    BodyMesh->WakeAllRigidBodies();
    if(!DeathState.Impulse.IsNearlyZero())
    {
        const FVector At=BodyMesh->GetBoneTransform(DeathState.Bone).TransformPosition(DeathState.BoneLocalPoint);
        BodyMesh->AddImpulseAtLocation(DeathState.Impulse,At,DeathState.Bone);
    }
}
void APersonaCharacter::TickCorpse(float Delta)
{
    if(!CorpsePose.IsEmpty())
    {
        // Character/Blueprint initialization can reset a detached mesh after
        // its initial RepNotify. A frozen body keeps the saved world frame.
        if(!HasAuthority() && !GetMesh()->GetComponentTransform().Equals(CorpseMeshTransform,.01f))
            GetMesh()->SetWorldTransform(CorpseMeshTransform,false,nullptr,ETeleportType::TeleportPhysics);
        return;
    }
    if(!HasAuthority())
    {
        if(CorpseFrames.IsEmpty()) return;
        const float At=ServerTime()-.1f;
        while(CorpseFrames.Num()>2 && CorpseFrames[1].ServerTime<=At) CorpseFrames.RemoveAt(0);
        const auto& A=CorpseFrames[0]; const auto& B=CorpseFrames[FMath::Min(1,CorpseFrames.Num()-1)];
        const float Alpha=B.ServerTime>A.ServerTime ? FMath::Clamp((At-A.ServerTime)/(B.ServerTime-A.ServerTime),0.f,1.f) : 1;
        DisplayCorpsePose=A.Pose;
        if(A.Pose.Num()==B.Pose.Num()) for(int32 I=0;I<DisplayCorpsePose.Num();++I) DisplayCorpsePose[I].Blend(A.Pose[I],B.Pose[I],Alpha);
        FTransform Frame; Frame.Blend(A.Mesh,B.Mesh,Alpha); GetMesh()->SetWorldTransform(Frame);
        if(B.Final && At>=B.ServerTime) { CorpsePose=B.Pose; CorpseMeshTransform=B.Mesh; OnRep_Corpse(); CorpseFrames.Reset(); }
        return;
    }
    DeathAge+=Delta;
    if(!bRagdollStarted)
    {
        if(ServerTime()-DeathState.Start>=DeathState.BlendDelay) BeginRagdoll();
        else return;
    }
    if(bNPC && DeathState.BlendDelay>0)
        GetMesh()->SetAllBodiesPhysicsBlendWeight(FMath::SmoothStep(0.f,FMath::Max(.01f,CombatSettings()->RagdollBlendTime),ServerTime()-DeathState.Start-DeathState.BlendDelay));
    float MaxSpeed=0;
    for(auto Body:GetMesh()->Bodies) if(Body && Body->IsValidBodyInstance())
        MaxSpeed=FMath::Max(MaxSpeed,float(Body->GetUnrealWorldVelocity().Size()));
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CorpseSupport),false,this); FHitResult Floor;
    const FVector Pelvis=GetMesh()->GetBoneLocation(TEXT("pelvis"));
    const bool Supported=GetWorld()->LineTraceSingleByChannel(Floor,Pelvis,Pelvis-FVector(0,0,55),ECC_Visibility,Q) && Floor.ImpactNormal.Z>.45f;
    // Joint solvers can report alternating angular velocity while a limb barely
    // moves. Require a bounded pose over a supported window, rather than waiting
    // forever for every instantaneous angular velocity to reach zero.
    bool Resting=Supported && MaxSpeed<15 && CorpseRestAnchors.Num()==GetMesh()->Bodies.Num();
    for(int32 I=0;Resting && I<GetMesh()->Bodies.Num();++I)
        if(auto Body=GetMesh()->Bodies[I]; Body && Body->IsValidBodyInstance())
        {
            const auto At=Body->GetUnrealWorldTransform();
            Resting=FVector::DistSquared(At.GetLocation(),CorpseRestAnchors[I].GetLocation())<FMath::Square(2.f)
                && At.GetRotation().AngularDistance(CorpseRestAnchors[I].GetRotation())<FMath::DegreesToRadians(3.f);
        }
    if(Resting) CorpseStableTime+=Delta;
    else
    {
        CorpseStableTime=0; CorpseRestAnchors.SetNum(GetMesh()->Bodies.Num());
        for(int32 I=0;I<GetMesh()->Bodies.Num();++I)
            if(auto Body=GetMesh()->Bodies[I]; Body && Body->IsValidBodyInstance()) CorpseRestAnchors[I]=Body->GetUnrealWorldTransform();
    }
    CorpseSendTime+=Delta;
    if(CorpseSendTime>=.05f)
    {
        CorpseSendTime=0; CorpseFrame.Mesh=GetMesh()->GetComponentTransform(); CorpseFrame.Pose=CaptureCorpsePose();
        CorpseFrame.ServerTime=ServerTime(); CorpseFrame.Generation=DeathState.Generation; ++CorpseFrame.Sequence;
        ForceNetUpdate();
    }
    if(DeathAge>.8f && CorpseStableTime>.8f) FreezeCorpse();
}
void APersonaCharacter::OnRep_CorpseFrame()
{
    if(HasAuthority() || !bDead || !CorpsePose.IsEmpty() || CorpseFrame.Pose.IsEmpty()) return;
    if(CorpseFrame.Generation!=DeathState.Generation || CorpseFrame.Sequence<=LastCorpseSequence) return;
    LastCorpseSequence=CorpseFrame.Sequence;
    GetMesh()->SetSimulatePhysics(false); GetMesh()->SetAllBodiesSimulatePhysics(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    if(CorpseFrames.IsEmpty())
    {
        FPersonaCorpseFrame Initial; Initial.Pose=CaptureCorpsePose(); Initial.Mesh=GetMesh()->GetComponentTransform();
        Initial.ServerTime=CorpseFrame.ServerTime-.1f; Initial.Generation=CorpseFrame.Generation;
        CorpseFrames.Add(Initial);
    }
    CorpseFrames.Add(CorpseFrame);
    if(CorpseFrames.Num()>8) CorpseFrames.RemoveAt(0);
}
TArray<FPersonaBodyMotion> APersonaCharacter::CaptureBodyMotion() const
{
    TArray<FPersonaBodyMotion> Result;
    for(auto Body:GetMesh()->Bodies) if(Body && Body->IsValidBodyInstance() && Body->BodySetup.IsValid())
    {
        FPersonaBodyMotion Motion; Motion.Bone=Body->BodySetup->BoneName;
        Motion.Transform=Body->GetUnrealWorldTransform(); Motion.Linear=Body->GetUnrealWorldVelocity(); Motion.Angular=Body->GetUnrealWorldAngularVelocityInRadians();
        Result.Add(Motion);
    }
    return Result;
}
void APersonaCharacter::RestoreBodyMotion(const TArray<FPersonaBodyMotion>& Bodies)
{
    if(!HasAuthority() || !bDead) return;
    CorpsePose.Reset(); DeathState.Impulse=FVector::ZeroVector; DeathState.BlendDelay=0; bRagdollStarted=false;
    BeginRagdoll();
    for(const auto& Motion:Bodies) if(auto Body=GetMesh()->GetBodyInstance(Motion.Bone))
    { Body->SetBodyTransform(Motion.Transform,ETeleportType::TeleportPhysics); Body->SetLinearVelocity(Motion.Linear,false); Body->SetAngularVelocityInRadians(Motion.Angular,false); }
    ForceNetUpdate();
}
