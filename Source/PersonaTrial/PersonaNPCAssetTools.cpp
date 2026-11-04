#include "PersonaAssetTools.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "PersonaCombat.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#endif

void UPersonaAssetTools::ConfigureNPCHitVolumes(UPersonaCombatProfile* Profile,USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    if(!Profile || !Mesh || !Mesh->GetImportedModel() || Mesh->GetImportedModel()->LODModels.IsEmpty()) return;
    const auto& Ref=Mesh->GetRefSkeleton(); TArray<FTransform> CS; CS.SetNum(Ref.GetNum());
    TArray<FBox> Bounds; Bounds.Init(FBox(ForceInit),Ref.GetNum());
    for(int32 I=0;I<CS.Num();++I) { const int32 P=Ref.GetParentIndex(I); CS[I]=P>=0 ? Ref.GetRefBonePose()[I]*CS[P] : Ref.GetRefBonePose()[I]; }
    for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections) for(const auto& Vertex:Section.SoftVertices)
    {
        int32 Best=0; for(int32 I=1;I<MAX_TOTAL_INFLUENCES;++I) if(Vertex.InfluenceWeights[I]>Vertex.InfluenceWeights[Best]) Best=I;
        const int32 Bone=Section.BoneMap[Vertex.InfluenceBones[Best]];
        Bounds[Bone]+=CS[Bone].InverseTransformPosition(FVector(Vertex.Position));
    }
    Profile->Modify(); Profile->HitVolumes.Reset();
    for(int32 I=0;I<Bounds.Num();++I) if(Bounds[I].IsValid)
    {
        FPersonaHitVolume Shape; Shape.Bone=Ref.GetBoneName(I); Shape.Center=Bounds[I].GetCenter(); Shape.Extent=Bounds[I].GetExtent()+FVector(1.5);
        Profile->HitVolumes.Add(Shape);
    }
    Profile->MarkPackageDirty();
#endif
}

void UPersonaAssetTools::NormalizeNPCClip(UAnimSequence* Source,UAnimSequence* Target)
{
#if WITH_EDITOR
    if(!Source || !Target || !Source->GetSkeleton()) return;
    const auto& Ref=Source->GetSkeleton()->GetReferenceSkeleton(); const int32 N=Ref.GetNum();
    const int32 Pelvis=Ref.FindBoneIndex(TEXT("pelvis")); if(Pelvis<0) return;
    const bool Walk=Source->GetName().Contains(TEXT("_Walk_")),Jog=Source->GetName().Contains(TEXT("_Jog_"));
    // These source takes contain two walk cycles / three jog cycles. A shared
    // gait phase must describe one stride, not an entire multi-cycle take.
    const float Duration=Source->GetPlayLength()/(Walk ? 2.f : Jog ? 3.f : 1.f);
    const float Start=Source->GetPlayLength()*(Walk ? .375f : Jog ? .285f : 0.f);
    const int32 Frames=FMath::Max(1,FMath::RoundToInt(Duration*60));
    TArray<TArray<FVector>> Positions,Scales; TArray<TArray<FQuat>> Rotations; Positions.SetNum(N); Scales.SetNum(N); Rotations.SetNum(N);
    TArray<FTransform> ReferenceCS; ReferenceCS.SetNum(N);
    for(int32 I=0;I<N;++I) { int32 P=Ref.GetParentIndex(I); ReferenceCS[I]=P>=0 ? Ref.GetRefBonePose()[I]*ReferenceCS[P] : Ref.GetRefBonePose()[I]; }
    FVector FirstPelvis; const bool Death=Source->GetName().Contains(TEXT("Death")); float LowestFoot=FLT_MAX;
    for(int32 F=0;F<=Frames;++F)
    {
        TArray<FTransform> Local,CS; Local.SetNum(N); CS.SetNum(N);
        for(int32 I=0;I<N;++I)
        {
            const double Time=Walk||Jog ? FMath::Fmod(Start+double(F)*Duration/Frames,double(Source->GetPlayLength())) : double(F)*Duration/Frames;
            Source->GetBoneTransform(Local[I],FSkeletonPoseBoneIndex(I),FAnimExtractContext(Time),true);
            const int32 Parent=Ref.GetParentIndex(I); CS[I]=Parent>=0 ? Local[I]*CS[Parent] : Local[I];
        }
        if(F==0) FirstPelvis=CS[Pelvis].GetLocation();
        const FVector Delta=CS[Pelvis].GetLocation()-FirstPelvis;
        const FVector PelvisAt=ReferenceCS[Pelvis].GetLocation()+FVector(FMath::Clamp(Delta.X,Death ? -25. : -5.,Death ? 25. : 5.),
            FMath::Clamp(Delta.Y,Death ? -25. : -5.,Death ? 25. : 5.),FMath::Clamp(Delta.Z,Death ? -70. : -6.,6.));
        for(int32 I=0;I<N;++I)
        {
            FVector Position=Ref.GetRefBonePose()[I].GetLocation(); FQuat Rotation=Local[I].GetRotation();
            if(I==0) Rotation=Ref.GetRefBonePose()[I].GetRotation();
            if(I==Pelvis) Position=ReferenceCS[Ref.GetParentIndex(I)].InverseTransformPosition(PelvisAt);
            Positions[I].Add(Position); Rotations[I].Add(Rotation.GetNormalized()); Scales[I].Add(FVector::OneVector);
        }
        if(Walk||Jog) for(int32 I=0;I<N;++I)
        {
            const int32 Parent=Ref.GetParentIndex(I); FTransform Pose(Rotations[I].Last(),Positions[I].Last());
            CS[I]=Parent>=0 ? Pose*CS[Parent] : Pose;
            if(Ref.GetBoneName(I)==TEXT("foot_l") || Ref.GetBoneName(I)==TEXT("foot_r")) LowestFoot=FMath::Min(LowestFoot,float(CS[I].GetLocation().Z));
        }
    }
    if(Walk||Jog)
    {
        const int32 Foot=Ref.FindBoneIndex(TEXT("foot_l"));
        const FVector Offset=ReferenceCS[Ref.GetParentIndex(Pelvis)].InverseTransformVectorNoScale(FVector(0,0,LowestFoot-ReferenceCS[Foot].GetLocation().Z));
        for(auto& Position:Positions[Pelvis]) Position-=Offset;
        // Close the retargeted take without a discontinuity at its loop seam.
        for(int32 I=0;I<N;++I) for(int32 F=FMath::Max(1,Frames-3);F<=Frames;++F)
        {
            const float Alpha=FMath::SmoothStep(0.f,1.f,float(F-(Frames-4))/4);
            Positions[I][F]=FMath::Lerp(Positions[I][F],Positions[I][0],Alpha);
            Rotations[I][F]=FQuat::Slerp(Rotations[I][F],Rotations[I][0],Alpha).GetNormalized();
        }
    }
    auto& Controller=Target->GetController(); Controller.OpenBracket(FText::FromString(TEXT("Normalize NPC root and bone lengths")),false);
    Controller.InitializeModel(); Controller.SetFrameRate(FFrameRate(60,1),false); Controller.SetNumberOfFrames(FFrameNumber(Frames),false);
    for(int32 I=0;I<N;++I) { Controller.AddBoneCurve(Ref.GetBoneName(I),false); Controller.SetBoneTrackKeys(Ref.GetBoneName(I),Positions[I],Rotations[I],Scales[I],false); }
    Controller.NotifyPopulated(); Controller.CloseBracket(false);
    Target->AuthoredSyncMarkers.Reset(); Target->RefreshSyncMarkerDataFromAuthored(); Target->MarkPackageDirty();
#endif
}

void UPersonaAssetTools::ConfigureNPCBlend(UBlendSpace* Blend,bool Crouched)
{
#if WITH_EDITOR
    if(!Blend) return; Blend->Modify();
    auto& X=const_cast<FBlendParameter&>(Blend->GetBlendParameter(0)); X.Min=-180; X.Max=180; X.GridNum=8; X.bWrapInput=true;
    auto& Y=const_cast<FBlendParameter&>(Blend->GetBlendParameter(1)); Y.Min=0; Y.Max=Crouched ? 180 : 1150; Y.GridNum=4;
    auto Load=[](const FString& Name) { return LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Persona/Animation/NPC/")+Name+TEXT(".")+Name)); };
    if(Crouched)
    {
        while(Blend->GetNumberOfBlendSamples()) Blend->DeleteSample(Blend->GetNumberOfBlendSamples()-1);
        // Blender's positive turn points left after the mesh's -90 degree yaw.
        for(int32 Angle=-180;Angle<=180;Angle+=45) Blend->AddSample(Load(FString::Printf(TEXT("CrouchMove_%d"),-Angle)),FVector(Angle,180,0));
        for(int32 Angle:{-180,0,180}) Blend->AddSample(Load(TEXT("CrouchIdle")),FVector(Angle,0,0));
    }
    else
    {
        const auto Samples=Blend->GetBlendSamples();
        for(int32 I=0;I<Samples.Num();++I) if(auto Clip=Samples[I].Animation.Get())
        {
            if(auto Copy=Load(Clip->GetName())) Blend->ReplaceSampleAnimation(I,Copy);
            FVector At=Samples[I].SampleValue; At.Y=Clip->GetName().Contains(TEXT("Jog")) ? 780 : At.Y>0 ? 220 : 0;
            Blend->EditSampleValue(I,At);
        }
    }
    Blend->ValidateSampleData(); Blend->ResampleData(); Blend->MarkPackageDirty();
#endif
}
void UPersonaAssetTools::ConfigureNPCPhysics(UPhysicsAsset* Asset,USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    if(!Asset || !Mesh) return; Asset->Modify();
    const auto& Ref=Mesh->GetRefSkeleton(); TArray<FTransform> CS; CS.SetNum(Ref.GetNum());
    for(int32 I=0;I<CS.Num();++I) { int32 P=Ref.GetParentIndex(I); CS[I]=P>=0 ? Ref.GetRefBonePose()[I]*CS[P] : Ref.GetRefBonePose()[I]; }
    // The imported auto-generated asset merged upper arms into clavicles and
    // omitted hands/feet. Every deforming link needs its own rigid body: an
    // animated bone between simulated bodies otherwise stretches the skin.
    Asset->SkeletalBodySetups.Reset(); Asset->ConstraintSetup.Reset(); Asset->CollisionDisableTable.Reset();
    for(int32 I=1;I<Ref.GetNum();++I)
    {
        if(Ref.GetBoneName(I)==TEXT("root")) continue;
        auto Body=NewObject<USkeletalBodySetup>(Asset,NAME_None,RF_Transactional);
        Body->BoneName=Ref.GetBoneName(I); Body->PhysicsType=PhysType_Default;
        Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;
        Asset->SkeletalBodySetups.Add(Body);
        const int32 Parent=Ref.GetParentIndex(I);
        if(Parent>0 && Ref.GetBoneName(Parent)!=TEXT("root"))
        {
            auto Joint=NewObject<UPhysicsConstraintTemplate>(Asset,NAME_None,RF_Transactional);
            Joint->DefaultInstance.JointName=Ref.GetBoneName(I);
            Joint->DefaultInstance.ConstraintBone1=Ref.GetBoneName(I); Joint->DefaultInstance.ConstraintBone2=Ref.GetBoneName(Parent);
            Asset->ConstraintSetup.Add(Joint);
        }
    }
    Asset->UpdateBodySetupIndexMap(); Asset->UpdateBoundsBodiesArray();
    auto At=[&](FName Bone) { int32 I=Ref.FindBoneIndex(Bone); return I>=0 ? CS[I] : FTransform::Identity; };
    auto Next=[](const FString& B)->FName
    {
        if(B==TEXT("pelvis")) return TEXT("spine_01"); if(B==TEXT("spine_01")) return TEXT("spine_02"); if(B==TEXT("spine_02")) return TEXT("head");
        for(const FString S:{FString(TEXT("_l")),FString(TEXT("_r"))})
        { if(B==TEXT("clavicle")+S) return FName(TEXT("upperarm")+S); if(B==TEXT("thigh")+S) return FName(TEXT("calf")+S); if(B==TEXT("calf")+S) return FName(TEXT("foot")+S); if(B==TEXT("upperarm")+S) return FName(TEXT("lowerarm")+S); if(B==TEXT("lowerarm")+S) return FName(TEXT("hand")+S); }
        return NAME_None;
    };
    auto Weight=[](const FString& B) { return B==TEXT("pelvis") ? 14.f : B.Contains(TEXT("spine")) ? 11.f : B.Contains(TEXT("thigh")) ? 8.f : B.Contains(TEXT("calf")) ? 4.f : B.Contains(TEXT("head")) ? 5.f : B.Contains(TEXT("upperarm")) ? 2.5f : 1.5f; };
    float Total=0; for(auto Setup:Asset->SkeletalBodySetups) if(Setup) Total+=Weight(Setup->BoneName.ToString());
    struct FCapsule { FVector A,B; float Radius; }; TArray<FCapsule> Shapes; Shapes.SetNum(Asset->SkeletalBodySetups.Num());
    for(int32 I=0;I<Asset->SkeletalBodySetups.Num();++I)
    {
        auto Setup=Asset->SkeletalBodySetups[I]; if(!Setup) continue; Setup->Modify(); const FString B=Setup->BoneName.ToString(); const FTransform Bone=At(Setup->BoneName);
        FVector Start=Bone.GetLocation(),End; const FName Child=Next(B);
        if(!Child.IsNone()) End=At(Child).GetLocation();
        else if(B.Contains(TEXT("head"))) { Start+=FVector(0,0,5); End=Start+FVector(0,0,15); }
        else if(B.Contains(TEXT("foot"))) { Start+=FVector(0,-3,-5); End=Start+FVector(0,12,0); }
        else if(B.Contains(TEXT("hand"))) End=Start+(Start-At(FName(B.Replace(TEXT("hand"),TEXT("lowerarm")))).GetLocation()).GetSafeNormal()*10;
        else End=Start+FVector(0,0,5);
        const float Radius=B.Contains(TEXT("head")) ? 10.f : B.Contains(TEXT("thigh")) ? 7.f : B.Contains(TEXT("calf")) ? 5.5f : B.Contains(TEXT("spine")) || B==TEXT("pelvis") ? 10.f : B.Contains(TEXT("arm")) ? 4.5f : 3.5f;
        const FVector Axis=(End-Start).GetSafeNormal(); const FVector Center=(Start+End)*.5f;
        FKSphylElem Capsule; Capsule.Center=Bone.InverseTransformPosition(Center); Capsule.Radius=Radius; Capsule.Length=FMath::Max(0.f,float(FVector::Distance(Start,End))-2*Radius);
        Capsule.Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Bone.InverseTransformVectorNoScale(Axis)).Rotator();
        Setup->AggGeom.EmptyElements(); Setup->AggGeom.SphylElems.Add(Capsule);
        auto& Body=Setup->DefaultInstance; Body.SetMassOverride(80*Weight(B)/Total); Body.LinearDamping=.5f; Body.AngularDamping=6.f;
        Body.SetOverrideIterationCounts(true); Body.SetPositionSolverIterationCount(16); Body.SetVelocitySolverIterationCount(8); Body.bUseCCD=true;
        Body.SetMaxDepenetrationVelocity(80); Body.SetMaxAngularVelocityInRadians(12,false);
        Body.SetPhysMaterialOverride(LoadObject<UPhysicalMaterial>(nullptr,TEXT("/Game/Persona/Characters/PM_NPCBody.PM_NPCBody")));
        Shapes[I]={Center-Axis*Capsule.Length*.5f,Center+Axis*Capsule.Length*.5f,Radius};
        Setup->InvalidatePhysicsData(); Setup->CreatePhysicsMeshes();
    }
    for(int32 A=0;A<Shapes.Num();++A) for(int32 B=A+1;B<Shapes.Num();++B)
    {
        FVector P,Q; FMath::SegmentDistToSegmentSafe(Shapes[A].A,Shapes[A].B,Shapes[B].A,Shapes[B].B,P,Q);
        const int32 BoneA=Ref.FindBoneIndex(Asset->SkeletalBodySetups[A]->BoneName),BoneB=Ref.FindBoneIndex(Asset->SkeletalBodySetups[B]->BoneName);
        auto CloseAncestor=[&](int32 Child,int32 Ancestor)
        {
            for(int32 Depth=0;Depth<2 && Child>=0;++Depth) { Child=Ref.GetParentIndex(Child); if(Child==Ancestor) return true; }
            return false;
        };
        // A folded wrist/ankle can overlap the capsule two joints above it.
        // Let the anatomical joint limits resolve this close chain instead of
        // making contact and joint solvers continually fight one another.
        if(CloseAncestor(BoneA,BoneB) || CloseAncestor(BoneB,BoneA) || FVector::Distance(P,Q)<Shapes[A].Radius+Shapes[B].Radius+1)
            Asset->DisableCollision(A,B);
        else Asset->EnableCollision(A,B);
    }
    for(auto Template:Asset->ConstraintSetup)
    {
        if(!Template) continue; Template->Modify(); auto& J=Template->DefaultInstance; const FString B=J.ConstraintBone1.ToString();
        const bool Knee=B.Contains(TEXT("calf")),Elbow=B.Contains(TEXT("lowerarm")),Hinge=Knee||Elbow;
        const bool Spine=B.Contains(TEXT("spine")) || B.Contains(TEXT("head")) || B.Contains(TEXT("clavicle"));
        const bool Shoulder=B.Contains(TEXT("upperarm")),Hip=B.Contains(TEXT("thigh"));
        const FTransform Child=At(J.ConstraintBone1),Parent=At(J.ConstraintBone2); const FVector Origin=Child.GetLocation();
        const FName Tip=Next(B);
        const FVector AnatomicalAxis=Tip.IsNone() ? (Origin-Parent.GetLocation()).GetSafeNormal() : (At(Tip).GetLocation()-Origin).GetSafeNormal();
        FVector Primary=Knee ? FVector::ForwardVector : Elbow ? FVector::UpVector*(B.EndsWith(TEXT("_l")) ? 1.f : -1.f) : AnatomicalAxis;
        if(Primary.IsNearlyZero()) Primary=FVector::UpVector;
        const FVector Secondary=FMath::Abs(Primary.Z)>.9f ? FVector::ForwardVector : FVector::UpVector;
        const FTransform Frame(FRotationMatrix::MakeFromXY(Primary,Secondary).ToQuat(),Origin);
        J.SetRefFrame(EConstraintFrame::Frame1,Frame.GetRelativeTransform(Child));
        FTransform ParentFrame=Frame;
        // Shift the hinge interval anatomically in the actual joint frame.
        // AngularRotationOffset alone is editor metadata, not a runtime limit.
        if(Hinge) ParentFrame.SetRotation(FQuat(Primary,FMath::DegreesToRadians(Knee ? -65.f : 65.f))*Frame.GetRotation());
        if(Shoulder) ParentFrame.SetRotation(FQuat::FindBetweenNormals(Primary,(Primary+FVector::DownVector).GetSafeNormal())*Frame.GetRotation());
        J.SetRefFrame(EConstraintFrame::Frame2,ParentFrame.GetRelativeTransform(Parent));
        J.SetLinearLimits(LCM_Locked,LCM_Locked,LCM_Locked,0);
        J.SetAngularSwing1Limit(Hinge ? ACM_Locked : ACM_Limited,Shoulder ? 110 : Hip ? 95 : Spine ? 22 : 35);
        J.SetAngularSwing2Limit(Hinge ? ACM_Locked : ACM_Limited,Shoulder ? 85 : Hip ? 50 : Spine ? 18 : 30);
        J.SetAngularTwistLimit(ACM_Limited,Hinge ? 75 : Shoulder ? 70 : Spine ? 18 : 35);
        J.AngularRotationOffset=FRotator(0,0,Knee ? -65 : Elbow ? 65 : 0);
        J.SetOrientationDriveTwistAndSwing(false,false); J.SetOrientationDriveSLERP(false);
        J.SetAngularVelocityDriveTwistAndSwing(false,false); J.SetAngularVelocityDriveSLERP(false); J.SetAngularDriveParams(0,0,0);
        J.SetDisableCollision(true); J.SetProjectionParams(true,.5f,0,1,15); J.SetShockPropagationParams(false,0);
        Template->SetDefaultProfile(J);
        const int32 A=Asset->FindBodyIndex(J.ConstraintBone1),BIndex=Asset->FindBodyIndex(J.ConstraintBone2);
        if(A>=0 && BIndex>=0) Asset->DisableCollision(A,BIndex);
    }
    Asset->MarkPackageDirty();
#endif
}
