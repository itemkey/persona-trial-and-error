#include "PersonaAssetTools.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "TwoBoneIK.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

void UPersonaAssetTools::BakeStanceClip(UAnimSequence* Source,UAnimSequence* Target,int32 Kind)
{
#if WITH_EDITOR
    if(!Source || !Target || !Source->GetSkeleton()) return;
    // Retargeted locomotion carries horizontal travel in the pelvis as well
    // as the root. Locking only the root leaves a multi-metre loop snap.
    // Normalize a temporary copy before authoring the player's crouch pose.
    if(Kind==0)
    {
        UAnimSequence* InPlace=NewObject<UAnimSequence>(GetTransientPackage());
        InPlace->SetSkeleton(Source->GetSkeleton());
        NormalizeNPCClip(Source,InPlace);
        Source=InPlace;
    }
    Target->SetSkeleton(Source->GetSkeleton());
    const auto& Ref=Source->GetSkeleton()->GetReferenceSkeleton(); const int32 N=Ref.GetNum();
    const float Duration=Kind==0 ? Source->GetPlayLength() : Kind==3 ? 1.2f : .32f;
    const int32 Frames=FMath::Max(2,FMath::RoundToInt(Duration*60));
    TArray<TArray<FVector>> Positions,Scales; TArray<TArray<FQuat>> Rotations;
    Positions.SetNum(N); Scales.SetNum(N); Rotations.SetNum(N);
    for(int32 Frame=0;Frame<=Frames;++Frame)
    {
        const float T=float(Frame)/Frames;
        const float Alpha=Kind==1 ? FMath::SmoothStep(0.f,1.f,T) : Kind==2 ? 1-FMath::SmoothStep(0.f,1.f,T) : 1;
        TArray<FTransform> Local,Original,Pose; Local.SetNum(N); Original.SetNum(N); Pose.SetNum(N);
        for(int32 I=0;I<N;++I)
        {
            Source->GetBoneTransform(Local[I],FSkeletonPoseBoneIndex(I),FAnimExtractContext(double(Kind==0 ? T*Source->GetPlayLength() : 0)),true);
            const int32 Parent=Ref.GetParentIndex(I); Original[I]=Parent>=0 ? Local[I]*Original[Parent] : Local[I];
        }
        const int32 Pelvis=Ref.FindBoneIndex(TEXT("pelvis"));
        if(Pelvis>=0)
        {
            // PERSONA's root is rotated: author the displacement in mesh space,
            // then convert into the pelvis parent's space before baking.
            const FVector Drop=FVector(0,-11,-34)*(Kind==3 ? FMath::Sin(T*PI)*.65f : Alpha);
            const int32 Parent=Ref.GetParentIndex(Pelvis);
            Local[Pelvis].AddToTranslation(Parent>=0 ? Original[Parent].InverseTransformVector(Drop) : Drop);
        }
        auto Rebuild=[&]() { for(int32 I=0;I<N;++I) { const int32 P=Ref.GetParentIndex(I); Pose[I]=P>=0 ? Local[I]*Pose[P] : Local[I]; } };
        Rebuild();
        const int32 Spine=Ref.FindBoneIndex(TEXT("spine_01"));
        if(Spine>=0)
        {
            FTransform Lean=Pose[Spine]; Lean.SetRotation(FQuat(FVector::XAxisVector,FMath::DegreesToRadians(-14*Alpha))*Lean.GetRotation());
            Local[Spine]=Lean.GetRelativeTransform(Pose[Ref.GetParentIndex(Spine)]); Rebuild();
        }
        for(int32 Side=0;Side<2;++Side)
        {
            const FString Suffix=Side==0 ? TEXT("_l") : TEXT("_r");
            const int32 Hip=Ref.FindBoneIndex(FName(TEXT("thigh")+Suffix)),Knee=Ref.FindBoneIndex(FName(TEXT("calf")+Suffix)),Foot=Ref.FindBoneIndex(FName(TEXT("foot")+Suffix));
            if(Hip<0 || Knee<0 || Foot<0) continue;
            FTransform A=Pose[Hip],B=Pose[Knee],C=Pose[Foot]; FVector TargetFoot=Original[Foot].GetLocation();
            if(Kind==3) TargetFoot+=FVector(0,18,30)*FMath::Sin(FMath::Clamp((T-Side*.12f)/.8f,0.f,1.f)*PI);
            AnimationCore::SolveTwoBoneIK(A,B,C,A.GetLocation()+FVector(Side==0 ? -8 : 8,100,0),TargetFoot,false,1,1);
            C.SetRotation(Original[Foot].GetRotation());
            Local[Hip]=A.GetRelativeTransform(Pose[Ref.GetParentIndex(Hip)]); Local[Knee]=B.GetRelativeTransform(A); Local[Foot]=C.GetRelativeTransform(B); Rebuild();
        }
        for(int32 I=0;I<N;++I) { Positions[I].Add(Local[I].GetTranslation()); Rotations[I].Add(Local[I].GetRotation()); Scales[I].Add(Local[I].GetScale3D()); }
    }
    auto& Controller=Target->GetController(); Controller.OpenBracket(FText::FromString(TEXT("Author PERSONA stance")),false);
    Controller.InitializeModel(); Controller.RemoveAllBoneTracks(false);
    Controller.SetFrameRate(FFrameRate(60,1),false); Controller.SetNumberOfFrames(FFrameNumber(Frames),false);
    for(int32 I=0;I<N;++I) { Controller.AddBoneCurve(Ref.GetBoneName(I),false); Controller.SetBoneTrackKeys(Ref.GetBoneName(I),Positions[I],Rotations[I],Scales[I],false); }
    Controller.NotifyPopulated(); Controller.CloseBracket(false); Target->MarkPackageDirty();
    Target->bEnableRootMotion=false; Target->bForceRootLock=true;
    Target->RootMotionRootLock=ERootMotionRootLock::AnimFirstFrame;
#endif
}
void UPersonaAssetTools::BakeCrouchBlend(UBlendSpace* Target)
{
#if WITH_EDITOR
    if(!Target) return;
    auto SourceBlend=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Persona/Animation/Retargeted/P_BS_Idle_Walk_Run.P_BS_Idle_Walk_Run"));
    if(!SourceBlend) return;
    const auto Samples=SourceBlend->GetBlendSamples(); TMap<UAnimSequence*,UAnimSequence*> Baked;
    for(int32 I=0;I<Samples.Num();++I)
    {
        auto Source=Samples[I].Animation.Get(); if(!Source) continue;
        UAnimSequence* Clip=Baked.FindRef(Source);
        if(!Clip)
        {
            FString Name=TEXT("Crouch_")+Source->GetName(); Name.RemoveFromStart(TEXT("Crouch_Crouch_"));
            auto Package=CreatePackage(*(TEXT("/Game/Persona/Animation/Stances/")+Name));
            Clip=FindObject<UAnimSequence>(Package,*Name);
            if(!Clip) { Clip=NewObject<UAnimSequence>(Package,*Name,RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Clip); }
            BakeStanceClip(Source,Clip,0); Baked.Add(Source,Clip);
        }
        Target->ReplaceSampleAnimation(I,Clip);
    }
    Target->MarkPackageDirty();
#endif
}
void UPersonaAssetTools::ConfigureBodyPhysics(UPhysicsAsset* Asset)
{
#if WITH_EDITOR
    if(!Asset) return; Asset->Modify();
    for(auto Setup:Asset->SkeletalBodySetups)
    {
        if(!Setup) continue; Setup->Modify(); const FString Bone=Setup->BoneName.ToString();
        auto& Body=Setup->DefaultInstance;
        const float Mass=Bone==TEXT("pelvis") ? 14.f : Bone.Contains(TEXT("spine")) ? 12.f : Bone.Contains(TEXT("thigh")) ? 8.f : Bone.Contains(TEXT("calf")) ? 4.f : Bone.Contains(TEXT("head")) ? 5.f : Bone.Contains(TEXT("upperarm")) ? 2.5f : 1.5f;
        Body.SetMassOverride(Mass); Body.LinearDamping=.8f; Body.AngularDamping=5.f;
        Body.SetPositionSolverIterationCount(12); Body.SetVelocitySolverIterationCount(4);
        Body.bUseCCD=true; Body.SetMaxDepenetrationVelocity(120);
        Body.SetMaxAngularVelocityInRadians(12,false);
        for(auto& Shape:Setup->AggGeom.SphylElems)
        {
            const float MaxRadius=Bone.Contains(TEXT("arm")) ? 6.5f : Bone.Contains(TEXT("calf")) ? 7.5f : Bone.Contains(TEXT("thigh")) ? 10.f : Bone.Contains(TEXT("head")) ? 11.f : 15.f;
            Shape.Radius=FMath::Clamp(Shape.Radius,2.f,MaxRadius);
        }
        Setup->InvalidatePhysicsData(); Setup->CreatePhysicsMeshes();
    }
    for(auto Template:Asset->ConstraintSetup)
    {
        if(!Template) continue; Template->Modify(); auto& Joint=Template->DefaultInstance;
        const FString Bone=Joint.ConstraintBone1.ToString();
        const bool Hinge=Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("lowerarm"));
        const bool Spine=Bone.Contains(TEXT("spine")) || Bone.Contains(TEXT("head"));
        Joint.SetLinearLimits(LCM_Locked,LCM_Locked,LCM_Locked,0);
        Joint.SetAngularSwing1Limit(ACM_Limited,Hinge ? 65.f : Spine ? 25.f : 55.f);
        Joint.SetAngularSwing2Limit(ACM_Limited,Hinge ? 6.f : Spine ? 20.f : 40.f);
        Joint.SetAngularTwistLimit(ACM_Limited,Hinge ? 6.f : Spine ? 18.f : 35.f);
        Joint.SetDisableCollision(true); Joint.SetProjectionParams(true,.1f,.1f,5,15);
        const int32 A=Asset->FindBodyIndex(Joint.ConstraintBone1),B=Asset->FindBodyIndex(Joint.ConstraintBone2);
        if(A!=INDEX_NONE && B!=INDEX_NONE) Asset->DisableCollision(A,B);
    }
    Asset->MarkPackageDirty();
#endif
}
