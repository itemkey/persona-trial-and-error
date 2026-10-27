#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Perception/AIPerceptionTypes.h"
#include "PersonaCombat.h"
#include "PersonaAI.generated.h"
class UAIPerceptionComponent;
class APersonaCharacter;
UCLASS()
class PERSONATRIAL_API APersonaAIController : public AAIController
{
    GENERATED_BODY()
public:
    float StaggerUntil=0;
    APersonaAIController();
    virtual void OnPossess(APawn* Pawn) override;
    UFUNCTION() void Perceived(AActor* Actor, FAIStimulus Stimulus);
    void Think();
    void NotifyDamage(AActor* Source);
    void AlertAt(const FVector& Location);
    void SetTacticalSeed(int32 Seed);
    UPROPERTY(BlueprintReadOnly) EPersonaTactic Tactic=EPersonaTactic::Patrol;
    UPROPERTY(BlueprintReadOnly) FVector TacticalGoal=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) bool bTargetVisible=false;
    int32 TacticalSeed=0;
    void CaptureState(struct FPersonaSubjectRecord& Record) const;
    void RestoreState(const struct FPersonaSubjectRecord& Record, AActor* SavedTarget);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAIPerceptionComponent> Senses;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Target;
    UPROPERTY(BlueprintReadOnly) FVector InvestigateLocation;
    UPROPERTY(BlueprintReadOnly) bool bInvestigating = false;
    UPROPERTY(BlueprintReadOnly) FName LastSense;
private:
    float LastStimulus = -100;
    float NextAttack = 0;
    FVector PatrolOrigin;
    FRandomStream Random;
    float CommitUntil=0, NextSprint=0, NextAlert=0;
    FVector PreviousGoal=FVector::ZeroVector;
    int32 FailedPaths=0;
    bool SelectGoal(const FVector& Threat, EPersonaTactic Choice, FVector& Goal);
    void MoveTactically(EPersonaTactic Choice, const FVector& Goal, float Duration);
    bool AttackSlotAvailable(APersonaCharacter* Victim) const;
};
UCLASS()
class PERSONATRIAL_API UPersonaThinkTask : public UBTTaskNode
{
    GENERATED_BODY()
public:
    UPersonaThinkTask();
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& Owner, uint8* Memory) override;
    virtual void TickTask(UBehaviorTreeComponent& Owner, uint8* Memory, float Delta) override;
private: float Elapsed = 0;
};
