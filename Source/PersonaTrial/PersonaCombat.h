#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PersonaCombat.generated.h"

UENUM(BlueprintType)
enum class EPersonaBodyRegion : uint8 { Torso, Head, LeftArm, RightArm, LeftLeg, RightLeg };
UENUM(BlueprintType)
enum class EPersonaTactic : uint8 { Patrol, Search, Approach, Flank, Sneak, Sprint, Cover, Ambush, Retreat, Reposition, Attack };

USTRUCT()
struct FPersonaDeathState
{
    GENERATED_BODY()
    UPROPERTY() float Start=0;
    UPROPERTY() float BlendDelay=0;
    UPROPERTY() int32 Variant=0;
    UPROPERTY() FVector Velocity=FVector::ZeroVector;
    UPROPERTY() FVector Impulse=FVector::ZeroVector;
    UPROPERTY() FVector Point=FVector::ZeroVector;
    UPROPERTY() FName Bone;
    UPROPERTY() FVector BoneLocalPoint=FVector::ZeroVector;
    UPROPERTY() uint32 Generation=0;
};
USTRUCT()
struct FPersonaCorpseFrame
{
    GENERATED_BODY()
    UPROPERTY() FTransform Mesh;
    UPROPERTY() TArray<FTransform> Pose;
    UPROPERTY() float ServerTime=0;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() uint32 Generation=0;
    UPROPERTY() bool Final=false;
};
USTRUCT()
struct FPersonaBodyMotion
{
    GENERATED_BODY()
    UPROPERTY() FName Bone;
    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector Linear=FVector::ZeroVector;
    UPROPERTY() FVector Angular=FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FPersonaAttackState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Sequence=0;
    UPROPERTY(BlueprintReadOnly) int32 Variant=0;
    UPROPERTY(BlueprintReadOnly) float Start=0;
    UPROPERTY(BlueprintReadOnly) float Windup=.4f;
    UPROPERTY(BlueprintReadOnly) float Active=.18f;
    UPROPERTY(BlueprintReadOnly) float Recovery=.65f;
    UPROPERTY(BlueprintReadOnly) bool Playing=false;
};
USTRUCT(BlueprintType)
struct FPersonaReactionState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Sequence=0;
    UPROPERTY(BlueprintReadOnly) EPersonaBodyRegion Region=EPersonaBodyRegion::Torso;
    UPROPERTY(BlueprintReadOnly) float Start=0;
    UPROPERTY(BlueprintReadOnly) float Duration=.35f;
    UPROPERTY(BlueprintReadOnly) float Strength=0;
    UPROPERTY(BlueprintReadOnly) FVector Direction=FVector::ForwardVector;
};
USTRUCT(BlueprintType)
struct FPersonaHitVolume
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FName Bone;
    UPROPERTY(EditAnywhere) FVector Center=FVector::ZeroVector;
    UPROPERTY(EditAnywhere) FVector Extent=FVector(5);
};
UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaCombatProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) float PatrolSpeed=220;
    UPROPERTY(EditAnywhere) float SneakSpeed=180;
    UPROPERTY(EditAnywhere) float CombatSpeed=780;
    UPROPERTY(EditAnywhere) float SprintSpeed=1150;
    UPROPERTY(EditAnywhere) float BurstDuration=2;
    UPROPERTY(EditAnywhere) float BurstRecovery=3;
    UPROPERTY(EditAnywhere) float ProgressInterval=1;
    UPROPERTY(EditAnywhere) float ProgressDistance=30;
    UPROPERTY(EditAnywhere) float StrikeRadius=20;
    UPROPERTY(EditAnywhere) float AttackSpacing=.12f;
    UPROPERTY(EditAnywhere) TArray<FPersonaHitVolume> HitVolumes;
    UPROPERTY(EditAnywhere, Category="NPC Motion") float Acceleration=2600;
    UPROPERTY(EditAnywhere, Category="NPC Motion") float Braking=3000;
    UPROPERTY(EditAnywhere, Category="NPC Motion") float TurnRate=540;
    UPROPERTY(EditAnywhere, Category="NPC Motion") float StanceHold=1.2f;
    UPROPERTY(EditAnywhere, Category="NPC Motion") float CrouchHalfHeight=65;
    UPROPERTY(EditAnywhere, Category="NPC Motion", meta=(ClampMin="0.05",ClampMax="0.5")) float PoseTransitionTime=.2f;
    UPROPERTY(EditAnywhere, Category="NPC Physics", meta=(ClampMin="0.01",ClampMax="0.3")) float RagdollBlendTime=.12f;
    UPROPERTY(EditAnywhere, Category="NPC Physics") float ImpactBodyDeltaSpeed=150;
    UPROPERTY(EditAnywhere, Category="NPC Physics") float ImpactTotalDeltaSpeed=60;
    UPROPERTY(EditAnywhere) float NormalDamage=35;
    UPROPERTY(EditAnywhere) float HeavyDamage=55;
    UPROPERTY(EditAnywhere) float AttackReach=150;
    UPROPERTY(EditAnywhere) float AttackStandDistance=95;
    UPROPERTY(EditAnywhere) TArray<float> StrikeFractions={.4f,.45f,.3f,.625f};
    UPROPERTY(EditAnywhere) TArray<FName> StrikeBones={TEXT("hand_r"),TEXT("hand_l"),TEXT("foot_r"),TEXT("hand_r")};
    UPROPERTY(EditAnywhere) float NormalWindup=.22f;
    UPROPERTY(EditAnywhere) float HeavyWindup=.45f;
    UPROPERTY(EditAnywhere) float ActiveTime=.18f;
    UPROPERTY(EditAnywhere) float RecoveryTime=.4f;
    UPROPERTY(EditAnywhere) float ClipStrikeFraction=.45f;
    UPROPERTY(EditAnywhere) float HeavyClipStrikeFraction=.6f;
    UPROPERTY(EditAnywhere) float LegSlowDuration=1.25f;
    UPROPERTY(EditAnywhere) float LegSlowScale=.65f;
    UPROPERTY(EditAnywhere) float ImpairmentCooldown=1.5f;
    UPROPERTY(EditAnywhere, Category="Reactions") float HeadReactionDuration=.45f;
    UPROPERTY(EditAnywhere, Category="Reactions") float TorsoReactionDuration=.35f;
    UPROPERTY(EditAnywhere, Category="Reactions") float ArmReactionDuration=.3f;
    UPROPERTY(EditAnywhere, Category="Reactions") float LegReactionDuration=.4f;
    UPROPERTY(EditAnywhere, Category="Reactions") float ArmInterruptDuration=.25f;
    UPROPERTY(EditAnywhere, Category="Reactions") float ImpactImpulsePerDamage=3;
    UPROPERTY(EditAnywhere, Category="Reactions") float MaxImpactImpulse=350;
    UPROPERTY(EditAnywhere) float MemoryTime=15;
    UPROPERTY(EditAnywhere) int32 AttackSlots=3;
};

inline EPersonaBodyRegion PersonaBodyRegion(FName Bone)
{
    const FString Name=Bone.ToString();
    const bool Left=Name.EndsWith(TEXT("_l"));
    if(Name.Contains(TEXT("head")) || Name.Contains(TEXT("neck"))) return EPersonaBodyRegion::Head;
    if(Name.Contains(TEXT("arm")) || Name.Contains(TEXT("hand"))) return Left ? EPersonaBodyRegion::LeftArm : EPersonaBodyRegion::RightArm;
    if(Name.Contains(TEXT("thigh")) || Name.Contains(TEXT("calf")) || Name.Contains(TEXT("foot"))) return Left ? EPersonaBodyRegion::LeftLeg : EPersonaBodyRegion::RightLeg;
    return EPersonaBodyRegion::Torso;
}
