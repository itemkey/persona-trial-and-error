#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PersonaCombat.generated.h"

UENUM(BlueprintType)
enum class EPersonaBodyRegion : uint8 { Torso, Head, LeftArm, RightArm, LeftLeg, RightLeg };
UENUM(BlueprintType)
enum class EPersonaTactic : uint8 { Patrol, Search, Approach, Flank, Sneak, Sprint, Cover, Ambush, Retreat, Reposition, Attack };

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
UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaCombatProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) float PatrolSpeed=300;
    UPROPERTY(EditAnywhere) float SneakSpeed=200;
    UPROPERTY(EditAnywhere) float CombatSpeed=650;
    UPROPERTY(EditAnywhere) float SprintSpeed=900;
    UPROPERTY(EditAnywhere) float NormalDamage=35;
    UPROPERTY(EditAnywhere) float HeavyDamage=55;
    UPROPERTY(EditAnywhere) float AttackReach=145;
    UPROPERTY(EditAnywhere) float NormalWindup=.4f;
    UPROPERTY(EditAnywhere) float HeavyWindup=.75f;
    UPROPERTY(EditAnywhere) float ActiveTime=.18f;
    UPROPERTY(EditAnywhere) float RecoveryTime=.65f;
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
    UPROPERTY(EditAnywhere) float MemoryTime=7;
    UPROPERTY(EditAnywhere) int32 AttackSlots=2;
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
