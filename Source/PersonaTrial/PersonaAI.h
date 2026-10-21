#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Perception/AIPerceptionTypes.h"
#include "PersonaAI.generated.h"
class UAIPerceptionComponent;
class APersonaCharacter;
UCLASS()
class PERSONATRIAL_API APersonaAIController : public AAIController
{
    GENERATED_BODY()
public:
    APersonaAIController();
    virtual void OnPossess(APawn* Pawn) override;
    UFUNCTION() void Perceived(AActor* Actor, FAIStimulus Stimulus);
    void Think();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAIPerceptionComponent> Senses;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Target;
    UPROPERTY(BlueprintReadOnly) FVector InvestigateLocation;
    UPROPERTY(BlueprintReadOnly) bool bInvestigating = false;
    UPROPERTY(BlueprintReadOnly) FName LastSense;
private:
    float LastStimulus = -100;
    float NextAttack = 0;
    FVector PatrolOrigin;
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
