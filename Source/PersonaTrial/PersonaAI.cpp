#include "PersonaAI.h"
#include "PersonaCharacter.h"
#include "PersonaSave.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"

APersonaAIController::APersonaAIController()
{
    Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Senses")); SetPerceptionComponent(*Senses);
    auto Sight = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
    Sight->SightRadius = 1800; Sight->LoseSightRadius = 2200; Sight->PeripheralVisionAngleDegrees = 65; Sight->SetMaxAge(4);
    Sight->DetectionByAffiliation.bDetectEnemies = Sight->DetectionByAffiliation.bDetectNeutrals = Sight->DetectionByAffiliation.bDetectFriendlies = true;
    auto Hearing = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
    Hearing->HearingRange = 2000; Hearing->SetMaxAge(5);
    Hearing->DetectionByAffiliation.bDetectEnemies = Hearing->DetectionByAffiliation.bDetectNeutrals = Hearing->DetectionByAffiliation.bDetectFriendlies = true;
    Senses->ConfigureSense(*Sight); Senses->ConfigureSense(*Hearing); Senses->SetDominantSense(Sight->GetSenseImplementation());
}
void APersonaAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn); PatrolOrigin = InPawn->GetActorLocation();
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ThisClass::Perceived);
    auto Tree = NewObject<UBehaviorTree>(this, TEXT("BT_PersonaPatrolInvestigateChase"));
    auto Root = NewObject<UBTComposite_Sequence>(Tree, TEXT("Root")); Tree->RootNode = Root;
    FBTCompositeChild Child; Child.ChildTask = NewObject<UPersonaThinkTask>(Tree); Root->Children.Add(Child);
    RunBehaviorTree(Tree);
}
void APersonaAIController::Perceived(AActor* Actor, FAIStimulus Stimulus)
{
    auto SensedCharacter = Cast<APersonaCharacter>(Actor);
    if (!SensedCharacter || SensedCharacter->bNPC || !SensedCharacter->IsAlive()) return;
    if (Stimulus.WasSuccessfullySensed())
    {
        LastSense = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() ? TEXT("Sight") : TEXT("Hearing");
        InvestigateLocation = Stimulus.StimulusLocation; bInvestigating = true; LastStimulus = GetWorld()->GetTimeSeconds();
        if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>()) Target = Actor;
    }
    else if (Target == Actor && Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>()) Target = nullptr;
}
void APersonaAIController::Think()
{
    auto Self = Cast<APersonaCharacter>(GetPawn());
    if (!Self || !Self->IsAlive()) { StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); return; }
    if(GetWorld()->GetTimeSeconds()<StaggerUntil) { StopMovement(); return; }
    if (auto C = Cast<APersonaCharacter>(Target); C && C->IsAlive())
    {
        SetFocus(C); MoveToActor(C, 110);
        if (FVector::DistSquared(C->GetActorLocation(), Self->GetActorLocation()) < FMath::Square(150.f)
            && GetWorld()->GetTimeSeconds() > NextAttack && LineOfSightTo(C))
        {
            NextAttack = GetWorld()->GetTimeSeconds() + 2;
            UGameplayStatics::ApplyDamage(C, 5*Self->DamageScale, this, Self, UDamageType::StaticClass());
        }
        return;
    }
    Target = nullptr; ClearFocus(EAIFocusPriority::Gameplay);
    if (bInvestigating && GetWorld()->GetTimeSeconds() - LastStimulus < 6)
    { MoveToLocation(InvestigateLocation, 80); return; }
    bInvestigating = false;
    if (GetMoveStatus() != EPathFollowingStatus::Moving)
        if (auto Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            FNavLocation Point;
            if (Nav->GetRandomReachablePointInRadius(PatrolOrigin, 1400, Point)) MoveToLocation(Point.Location, 70);
        }
}
void APersonaAIController::CaptureState(FPersonaSubjectRecord& R) const
{
    if (auto C = Cast<APersonaCharacter>(Target)) R.Target = C->PersistentId;
    R.Investigate = InvestigateLocation; R.Patrol = PatrolOrigin; R.LastSense = LastSense;
    R.InvestigationRemaining = bInvestigating ? FMath::Max(0.f, 6 - (GetWorld()->GetTimeSeconds()-LastStimulus)) : 0;
    R.AttackRemaining = FMath::Max(0.f,NextAttack-GetWorld()->GetTimeSeconds());
    R.Moving = GetMoveStatus() == EPathFollowingStatus::Moving;
    R.MoveGoal = GetPathFollowingComponent()->GetCurrentTargetLocation();
}
void APersonaAIController::RestoreState(const FPersonaSubjectRecord& R, AActor* SavedTarget)
{
    Target = SavedTarget; InvestigateLocation = R.Investigate; PatrolOrigin = R.Patrol; LastSense = R.LastSense;
    bInvestigating = R.InvestigationRemaining > 0;
    LastStimulus = GetWorld()->GetTimeSeconds() - 6 + R.InvestigationRemaining;
    NextAttack = GetWorld()->GetTimeSeconds() + R.AttackRemaining;
    if (R.Moving) MoveToLocation(R.MoveGoal,70);
}
UPersonaThinkTask::UPersonaThinkTask() { NodeName = TEXT("Patrol / investigate / chase"); bNotifyTick = true; bCreateNodeInstance = true; }
EBTNodeResult::Type UPersonaThinkTask::ExecuteTask(UBehaviorTreeComponent& Owner, uint8* Memory) { Elapsed = 0; return EBTNodeResult::InProgress; }
void UPersonaThinkTask::TickTask(UBehaviorTreeComponent& Owner, uint8* Memory, float Delta)
{
    Elapsed += Delta;
    if (Elapsed >= 0.25f) { Elapsed = 0; if (auto C = Cast<APersonaAIController>(Owner.GetAIOwner())) C->Think(); }
}
