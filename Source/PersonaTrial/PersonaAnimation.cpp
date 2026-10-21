#include "PersonaCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "TwoBoneIK.h"

// A native graph keeps the first foundation reproducible. Assets and animation
// inputs remain editable in the derived Animation Blueprint.
struct FPersonaAnimProxy : FAnimInstanceProxy
{
    FAnimNode_BlendSpacePlayer_Standalone Walk;
    FAnimNode_SequencePlayer_Standalone Jump;
    FAnimNode_SequencePlayer_Standalone Hands;
    float Speed = 0, Direction = 0, CrouchAlpha = 0, AirAlpha = 0;
    bool bHands = false, bFalling = false, bCrouched = false;
    FPersonaAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    virtual void Initialize(UAnimInstance* Instance) override
    {
        FAnimInstanceProxy::Initialize(Instance);
        auto A = CastChecked<UPersonaAnimInstance>(Instance);
        A->NativeInitializeAnimation();
        Walk.SetBlendSpace(A->Locomotion); Walk.SetLoop(true);
        Jump.SetSequence(A->JumpClip); Jump.SetLoopAnimation(false);
        Hands.SetSequence(A->HandsClip); Hands.SetLoopAnimation(true);
        FAnimationInitializeContext Context(this);
        Walk.Initialize_AnyThread(Context); Jump.Initialize_AnyThread(Context); Hands.Initialize_AnyThread(Context);
    }
    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        auto A = CastChecked<UPersonaAnimInstance>(Instance);
        Speed = A->Speed; Direction = A->Direction; bCrouched = A->bCrouched;
        if (A->bFalling && !bFalling) Jump.SetAccumulatedTime(0);
        bFalling = A->bFalling;
        if (auto C = Cast<APersonaCharacter>(Instance->TryGetPawnOwner())) bHands = Instance->GetSkelMeshComponent() == C->FirstPersonArms;
        AirAlpha = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Delta, 10);
        CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouched ? 1.f : 0.f, Delta, 10);
    }
    virtual void CacheBones() override
    {
        FAnimInstanceProxy::CacheBones();
        FAnimationCacheBonesContext Context(this);
        Walk.CacheBones_AnyThread(Context); Jump.CacheBones_AnyThread(Context); Hands.CacheBones_AnyThread(Context);
    }
    virtual void UpdateAnimationNode(const FAnimationUpdateContext& Context) override
    {
        // The bundled locomotion asset uses Direction (X), Speed (Y).
        Walk.SetPosition(FVector(Direction, Speed, 0));
        Walk.Update_AnyThread(Context); Jump.Update_AnyThread(Context); Hands.Update_AnyThread(Context);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        if (bHands) { Hands.Evaluate_AnyThread(Output); return true; }
        Walk.Evaluate_AnyThread(Output);
        if (AirAlpha > .001f)
        {
            FPoseContext Air(this); Jump.Evaluate_AnyThread(Air);
            FAnimationPoseData Result(Output), Other(Air);
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result, Other, 1-AirAlpha);
        }
        if (CrouchAlpha > .001f)
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
        return true;
    }
};
void UPersonaAnimInstance::NativeInitializeAnimation()
{
    if (!Locomotion) Locomotion = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_BS_Idle_Walk_Run.P_BS_Idle_Walk_Run"));
    if (!JumpClip) JumpClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MM_Jump.P_MM_Jump"));
    if (!HandsClip) HandsClip = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Persona/Animation/Retargeted/P_MF_Pistol_Idle_ADS.P_MF_Pistol_Idle_ADS"));
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
