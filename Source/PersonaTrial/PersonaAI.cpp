#include "PersonaAI.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaSave.h"
#include "PersonaTraversal.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

APersonaAIController::APersonaAIController()
{
    Senses=CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Senses")); SetPerceptionComponent(*Senses);
    auto Sight=CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
    Sight->SightRadius=3500; Sight->LoseSightRadius=4000; Sight->PeripheralVisionAngleDegrees=80; Sight->SetMaxAge(15);
    Sight->DetectionByAffiliation.bDetectEnemies=Sight->DetectionByAffiliation.bDetectNeutrals=Sight->DetectionByAffiliation.bDetectFriendlies=true;
    auto Hearing=CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
    Hearing->HearingRange=6000; Hearing->SetMaxAge(15);
    Hearing->DetectionByAffiliation.bDetectEnemies=Hearing->DetectionByAffiliation.bDetectNeutrals=Hearing->DetectionByAffiliation.bDetectFriendlies=true;
    Senses->ConfigureSense(*Sight); Senses->ConfigureSense(*Hearing); Senses->SetDominantSense(Sight->GetSenseImplementation());
}
void APersonaAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn); APersonaTraversalGraph::Ensure(GetWorld()); PatrolOrigin=InPawn->GetActorLocation(); SetTacticalSeed(GetTypeHash(InPawn->GetName()));
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this,&ThisClass::Perceived);
    auto Tree=NewObject<UBehaviorTree>(this,TEXT("BT_PersonaTactical"));
    auto Root=NewObject<UBTComposite_Sequence>(Tree,TEXT("Root")); Tree->RootNode=Root;
    FBTCompositeChild Child; Child.ChildTask=NewObject<UPersonaThinkTask>(Tree); Root->Children.Add(Child); RunBehaviorTree(Tree);
}
void APersonaAIController::SetTacticalSeed(int32 Seed) { TacticalSeed=Seed; Random.Initialize(Seed); }
void APersonaAIController::RequestStance(bool Crouched,bool Immediate)
{
    auto Self=Cast<APersonaCharacter>(GetPawn()); if(!Self) return;
    const float Now=GetWorld()->GetTimeSeconds();
    if((Self->bIsCrouched==Crouched && Self->GetCharacterMovement()->bWantsToCrouch==Crouched) || (!Immediate && Now<StanceUntil)) return;
    if(Crouched) Self->Crouch(); else Self->UnCrouch();
    StanceUntil=Now+Self->CombatSettings()->StanceHold;
}
void APersonaAIController::Tick(float Delta)
{
    Super::Tick(Delta);
    auto Self=Cast<APersonaCharacter>(GetPawn()); auto Victim=Cast<APersonaCharacter>(Target);
    if(!Self || !Self->IsAlive() || !BrainComponent || !BrainComponent->IsRunning() || Self->Traversal.Active) return;
    const float Now=GetWorld()->GetTimeSeconds();
    if(Now<StaggerUntil || !Victim || !Victim->IsAlive() || !bTargetVisible) return;
    const auto Profile=Self->CombatSettings(); const FVector To=Victim->GetActorLocation()-Self->GetActorLocation();
    const float Distance=To.Size2D();
    if(Now>=BurstUntil && Self->NPCMoveSpeed>Profile->CombatSpeed) Self->NPCMoveSpeed=Profile->CombatSpeed;
    if(Self->AttackState.Playing)
    {
        const float Age=Self->ServerTime()-Self->AttackState.Start;
        if(Age<Self->AttackState.Windup || Age>=Self->AttackState.Windup+Self->AttackState.Active) SetFocus(Victim); else ClearFocus(EAIFocusPriority::Gameplay);
        if(Distance<Profile->AttackStandDistance && GetMoveStatus()!=EPathFollowingStatus::Idle)
        { const FVector V=Self->GetVelocity(); StopMovement(); Self->GetCharacterMovement()->Velocity=V; }
        else if(Distance>Profile->AttackStandDistance+10 && GetMoveStatus()==EPathFollowingStatus::Idle)
        { const FVector V=Self->GetVelocity(); MoveToActor(Victim,35,false,true,true,nullptr,false); GetPathFollowingComponent()->SetStopMovementOnFinish(false); Self->GetCharacterMovement()->Velocity=V; ++PathRevision; }
        return;
    }
    if(Distance>350) bFirstStrike=true;
    const float Closing=FMath::Max(0.f,FVector::DotProduct(Self->GetVelocity()-Victim->GetVelocity(),To.GetSafeNormal2D()));
    const float Lead=FMath::Min(180.f,Closing*Profile->NormalWindup);
    if(Now<NextAttack || FMath::Abs(To.Z)>70 || Distance>Profile->AttackStandDistance+25+Lead || !AttackSlotAvailable(Victim)) return;
    if(FVector::DotProduct(Self->GetActorForwardVector(),To.GetSafeNormal2D())<.65f || !LineOfSightTo(Victim)) return;
    RequestStance(false,true); if(Self->bIsCrouched) return;
    if(Distance>Profile->AttackStandDistance && (GetMoveStatus()!=EPathFollowingStatus::Moving || GetPathFollowingComponent()->GetMoveGoal()!=Victim))
    {
        const FVector V=Self->GetVelocity(); MoveToActor(Victim,35,false,true,true,nullptr,false);
        GetPathFollowingComponent()->SetStopMovementOnFinish(false);
        Self->GetCharacterMovement()->Velocity=V; ++PathRevision;
    }
    const int32 Normal=!bFirstStrike && Self->GetVelocity().Size2D()<40 ? Random.RandRange(0,2) : Random.RandRange(0,1);
    Self->StartMeleeAttack(!bFirstStrike && Random.FRand()<.2f ? 3 : Normal);
    bFirstStrike=false; Tactic=EPersonaTactic::Attack;
    NextAttack=Now+Self->AttackState.Windup+Self->AttackState.Active+Self->AttackState.Recovery+Random.FRandRange(.05f,.15f);
    CommitUntil=0;
}
void APersonaAIController::AlertAt(const FVector& Location)
{
    if(bTargetVisible) return;
    InvestigateLocation=Location; bInvestigating=true; LastStimulus=GetWorld()->GetTimeSeconds(); LastSense=TEXT("Shared");
}
void APersonaAIController::NotifyDamage(AActor* Source)
{
    if(!Source) return;
    InvestigateLocation=Source->GetActorLocation(); bInvestigating=true; LastStimulus=GetWorld()->GetTimeSeconds(); LastSense=TEXT("Damage"); CommitUntil=0;
    if(auto Player=Cast<APersonaCharacter>(Source); Player && !Player->bNPC && Player->IsAlive() && LineOfSightTo(Player))
    { Target=Player; bTargetVisible=true; }
}
void APersonaAIController::Perceived(AActor* Actor,FAIStimulus Stimulus)
{
    auto Player=Cast<APersonaCharacter>(Actor); if(!Player || Player->bNPC || !Player->IsAlive()) return;
    const bool Sight=Stimulus.Type==UAISense::GetSenseID<UAISense_Sight>();
    if(Stimulus.WasSuccessfullySensed())
    {
        if(Sight || !bTargetVisible)
        { InvestigateLocation=Stimulus.StimulusLocation; LastStimulus=GetWorld()->GetTimeSeconds(); bInvestigating=true; LastSense=Sight ? TEXT("Sight") : TEXT("Hearing"); }
        if(Sight && (!Target || !bTargetVisible || FVector::DistSquared(Player->GetActorLocation(),GetPawn()->GetActorLocation())
            < FVector::DistSquared(Target->GetActorLocation(),GetPawn()->GetActorLocation())*.65f))
        { Target=Player; bTargetVisible=true; CommitUntil=0; }
    }
    else if(Sight && Target==Actor)
    {
        bTargetVisible=false; ClearFocus(EAIFocusPriority::Gameplay);
        if(auto Self=Cast<APersonaCharacter>(GetPawn()); Self && Self->IsAlive() && !Self->Traversal.Active && BrainComponent && BrainComponent->IsRunning())
            MoveTactically(EPersonaTactic::Search,InvestigateLocation,.3f);
    }
}
bool APersonaAIController::AttackSlotAvailable(APersonaCharacter* Victim) const
{
    auto Self=CastChecked<APersonaCharacter>(GetPawn()); int32 Count=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(*It!=Self && It->bNPC && It->IsAlive() && It->AttackState.Playing)
            if(auto AI=Cast<APersonaAIController>(It->Controller); AI && AI->Target==Victim)
            { ++Count; if(Self->ServerTime()-It->AttackState.Start<Self->CombatSettings()->AttackSpacing) return false; }
    return Count<Self->CombatSettings()->AttackSlots;
}
bool APersonaAIController::SelectGoal(const FVector& Threat,EPersonaTactic Choice,FVector& Goal)
{
    auto Self=CastChecked<APersonaCharacter>(GetPawn());
    auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); if(!Nav) return false;
    const FVector At=Self->GetActorLocation();
    FVector Away=(At-Threat).GetSafeNormal2D(); if(Away.IsNearlyZero()) Away=Self->GetActorForwardVector();
    struct FCandidate { FVector At; float Score; }; TArray<FCandidate> Candidates;
    const float PreferredSide=(TacticalSeed&1) ? 1.f : -1.f;
    for(int32 I=0;I<12;++I)
    {
        const float Angle=Random.FRandRange(35,145)*((I&1) ? PreferredSide : -PreferredSide);
        FVector Proposed=Choice==EPersonaTactic::Retreat
            ? At+Away.RotateAngleAxis(Random.FRandRange(-65,65),FVector::UpVector)*Random.FRandRange(450,1000)
            : Threat+Away.RotateAngleAxis(Angle,FVector::UpVector)*Random.FRandRange(350,1000);
        FNavLocation Projected; if(!Nav->ProjectPointToNavigation(Proposed,Projected,FVector(200,200,350))) continue;
        FHitResult Cover; FCollisionQueryParams Q(SCENE_QUERY_STAT(TacticalCover),false,Self); if(Target) Q.AddIgnoredActor(Target);
        const bool Hidden=GetWorld()->LineTraceSingleByChannel(Cover,Threat+FVector(0,0,50),Projected.Location+FVector(0,0,100),ECC_Visibility,Q);
        float Score=Random.FRandRange(0,100)-FVector::Distance(At,Projected.Location)*.12f;
        Score+=Hidden ? ((Choice==EPersonaTactic::Cover || Choice==EPersonaTactic::Retreat || Choice==EPersonaTactic::Sneak) ? 650.f : 180.f) : 0;
        Score+=Angle*PreferredSide>0 ? 80.f : 0;
        if(FVector::DistSquared(Projected.Location,PreviousGoal)<FMath::Square(250.f)) Score-=300;
        for(TActorIterator<APersonaAIController> It(GetWorld());It;++It)
            if(*It!=this && It->GetPawn() && FVector::DistSquared(Projected.Location,It->TacticalGoal)<FMath::Square(230.f)) Score-=180;
        Candidates.Add({Projected.Location,Score});
    }
    Candidates.Sort([](const auto& A,const auto& B){return A.Score>B.Score;});
    for(int32 I=0;I<FMath::Min(4,Candidates.Num());++I)
    {
        auto Path=Nav->FindPathToLocationSynchronously(GetWorld(),At,Candidates[I].At,Self);
        if(Path && Path->IsValid() && !Path->IsPartial()) { Goal=Candidates[I].At; return true; }
    }
    return false;
}
void APersonaAIController::MoveTactically(EPersonaTactic Choice,const FVector& Goal,float Duration)
{
    auto Self=CastChecked<APersonaCharacter>(GetPawn()); const auto Profile=Self->CombatSettings();
    const bool SameGoal=FVector::DistSquared(TacticalGoal,Goal)<FMath::Square(50.f);
    PreviousGoal=TacticalGoal; TacticalGoal=Goal; Tactic=Choice; CommitUntil=GetWorld()->GetTimeSeconds()+Duration;
    const bool Sneak=Choice==EPersonaTactic::Sneak || Choice==EPersonaTactic::Ambush || Choice==EPersonaTactic::Cover;
    const float Now=GetWorld()->GetTimeSeconds();
    RequestStance(Sneak);
    Self->NPCMoveSpeed=Sneak ? Profile->SneakSpeed : Choice==EPersonaTactic::Sprint ? Profile->SprintSpeed
        : Choice==EPersonaTactic::Patrol ? Profile->PatrolSpeed : Profile->CombatSpeed;
    const bool Follow=bTargetVisible && Target && (Choice==EPersonaTactic::Approach || Choice==EPersonaTactic::Sprint) && Now>=DetourUntil;
    // UE brakes against the cached path end (updated every 0.5 s), even
    // while steering toward the actor's current location. Chase braking is
    // instead based on current relative distance in PersonaMovement.
    GetPathFollowingComponent()->SetStopMovementOnFinish(!Follow);
    AActor* PreviousActor=GetPathFollowingComponent()->GetMoveGoal();
    if(GetMoveStatus()==EPathFollowingStatus::Moving &&
        (Follow ? PreviousActor==Target : !PreviousActor && (SameGoal || Now-LastPathRequest<.3f))) return;
    const FVector Velocity=Self->GetVelocity();
    const auto Result=Follow ? MoveToActor(Target,35,false,true,true,nullptr,false) : MoveToLocation(Goal,60,true,true,false,true,nullptr,false);
    GetPathFollowingComponent()->SetStopMovementOnFinish(!Follow);
    Self->GetCharacterMovement()->Velocity=Velocity;
    LastPathRequest=Now; ++PathRevision;
    if(Result==EPathFollowingRequestResult::Failed) { ++FailedPaths; CommitUntil=0; } else FailedPaths=0;
}
void APersonaAIController::Think()
{
    auto Self=Cast<APersonaCharacter>(GetPawn());
    if(!Self || !Self->IsAlive()) { StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); return; }
    const float Now=GetWorld()->GetTimeSeconds(); const auto Profile=Self->CombatSettings();
    if(Self->Traversal.Active) { ClearFocus(EAIFocusPriority::Gameplay); return; }
    if(Now<StaggerUntil) return;
    auto Victim=Cast<APersonaCharacter>(Target);
    if(Victim && (!Victim->IsAlive() || Victim->bTransit))
    { Target=nullptr; Victim=nullptr; bTargetVisible=false; }
    if(Victim) bTargetVisible=FVector::DistSquared(Self->GetActorLocation(),Victim->GetActorLocation())<FMath::Square(4000.f) && LineOfSightTo(Victim);
    if(!bTargetVisible && GetPathFollowingComponent()->GetMoveGoal()) MoveTactically(EPersonaTactic::Search,InvestigateLocation,.3f);
    // Repath only stalled movement, never a reserved smart-link transition.
    if(GetPathFollowingComponent()->GetCurrentCustomLinkOb()) { ProgressAt=Now; ProgressPosition=Self->GetActorLocation(); return; }
    if(Now-ProgressAt>=Profile->ProgressInterval)
    {
        const bool Stalled=GetMoveStatus()==EPathFollowingStatus::Moving && !Self->AttackState.Playing &&
            FVector::Dist2D(Self->GetActorLocation(),TacticalGoal)>160 && FVector::Dist2D(ProgressPosition,Self->GetActorLocation())<Profile->ProgressDistance;
        ProgressAt=Now; ProgressPosition=Self->GetActorLocation();
        if(Stalled)
        {
            ++StuckCount; RepathReason=TEXT("No progress"); CommitUntil=0; LastPathRequest=-100;
            FVector Goal=TacticalGoal;
            if(StuckCount>=2 && SelectGoal(InvestigateLocation,EPersonaTactic::Reposition,Goal))
            { RepathReason=TEXT("Blocked route: adjacent detour"); DetourUntil=Now+1.2f; }
            const FVector V=Self->GetVelocity(); StopMovement(); Self->GetCharacterMovement()->Velocity=V;
            MoveTactically(EPersonaTactic::Approach,Goal,.3f);
        }
        else if(GetMoveStatus()==EPathFollowingStatus::Moving) StuckCount=0;
    }
    if(Self->AttackState.Playing)
    {
        Tactic=EPersonaTactic::Attack;
        return;
    }
    if(Victim && bTargetVisible)
    {
        InvestigateLocation=Victim->GetActorLocation(); LastStimulus=Now; bInvestigating=true;
        const float Distance=FVector::Dist(Self->GetActorLocation(),InvestigateLocation);
        if(Now>NextAlert)
        {
            NextAlert=Now+1.5f;
            for(TActorIterator<APersonaAIController> It(GetWorld());It;++It)
                if(*It!=this && It->GetPawn() && FVector::DistSquared(Self->GetActorLocation(),It->GetPawn()->GetActorLocation())<FMath::Square(1800.f))
                    It->AlertAt(InvestigateLocation);
        }
        const float HeightDifference=InvestigateLocation.Z-Self->GetActorLocation().Z;
        SetFocus(Victim);
        // Resolve a height disadvantage before choosing lateral cover tactics.
        if(FMath::Abs(HeightDifference)>70)
        {
            if(Now>=CommitUntil || GetMoveStatus()!=EPathFollowingStatus::Moving)
                MoveTactically(EPersonaTactic::Approach,Victim->GetNavAgentLocation(),1.5f);
            return;
        }
        if(Now<DetourUntil && GetMoveStatus()==EPathFollowingStatus::Moving) return;
        if(FailedPaths>=2)
        {
            FVector Goal;
            if(SelectGoal(InvestigateLocation,EPersonaTactic::Reposition,Goal))
            { DetourUntil=Now+1.2f; RepathReason=TEXT("Path failed: adjacent detour"); MoveTactically(EPersonaTactic::Reposition,Goal,1.2f); return; }
        }
        if(Distance<230 && !AttackSlotAvailable(Victim))
        {
            if(Now>=CommitUntil || GetMoveStatus()!=EPathFollowingStatus::Moving)
            {
                FVector Goal; if(SelectGoal(InvestigateLocation,EPersonaTactic::Reposition,Goal)) MoveTactically(EPersonaTactic::Reposition,Goal,.6f);
            }
            return;
        }
        if(Distance>350 && Now>=NextSprint)
        { BurstUntil=Now+Profile->BurstDuration; NextSprint=BurstUntil+Profile->BurstRecovery; }
        const bool Burst=Now<BurstUntil && Distance>180;
        MoveTactically(Burst ? EPersonaTactic::Sprint : EPersonaTactic::Approach,Victim->GetNavAgentLocation(),.3f);
        return;
    }
    ClearFocus(EAIFocusPriority::Gameplay);
    if(Now<CommitUntil && GetMoveStatus()==EPathFollowingStatus::Moving) return;
    if(bInvestigating && Now-LastStimulus<Profile->MemoryTime)
    {
        if(FVector::DistSquared(Self->GetActorLocation(),InvestigateLocation)>FMath::Square(160.f))
        {
            FVector Goal=InvestigateLocation;
            if(FailedPaths>=2 && SelectGoal(InvestigateLocation,EPersonaTactic::Reposition,Goal)) RepathReason=TEXT("Search route failed: adjacent detour");
            MoveTactically(EPersonaTactic::Search,Goal,1.2f);
        }
        else if(Now>=SearchAt)
        {
            SearchAt=Now+1;
            if(auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
            {
                FNavLocation Point;
                if(Nav->GetRandomReachablePointInRadius(InvestigateLocation,500,Point)) MoveTactically(EPersonaTactic::Search,Point.Location,.8f);
            }
        }
        return;
    }
    Target=nullptr; bTargetVisible=false; bInvestigating=false; RequestStance(false);
    if(auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        FNavLocation Point;
        if(Nav->GetRandomReachablePointInRadius(LastStimulus>0 ? InvestigateLocation : PatrolOrigin,FailedPaths>2 ? 500 : 1400,Point))
            MoveTactically(EPersonaTactic::Patrol,Point.Location,2.f);
    }
}
void APersonaAIController::CaptureState(FPersonaSubjectRecord& R) const
{
    if(auto C=Cast<APersonaCharacter>(Target)) R.Target=C->PersistentId;
    R.Investigate=InvestigateLocation; R.Patrol=PatrolOrigin; R.LastSense=LastSense;
    R.InvestigationRemaining=bInvestigating ? FMath::Max(0.f,CastChecked<APersonaCharacter>(GetPawn())->CombatSettings()->MemoryTime-(GetWorld()->GetTimeSeconds()-LastStimulus)) : 0;
    R.AttackRemaining=FMath::Max(0.f,NextAttack-GetWorld()->GetTimeSeconds());
    R.Moving=GetMoveStatus()==EPathFollowingStatus::Moving; R.MoveGoal=TacticalGoal;
    R.TacticalSeed=TacticalSeed; R.TacticalRandomSeed=Random.GetCurrentSeed(); R.Tactic=static_cast<uint8>(Tactic);
    R.CommitRemaining=FMath::Max(0.f,CommitUntil-GetWorld()->GetTimeSeconds()); R.SprintRemaining=FMath::Max(0.f,NextSprint-GetWorld()->GetTimeSeconds());
}
void APersonaAIController::RestoreState(const FPersonaSubjectRecord& R,AActor* SavedTarget)
{
    SetTacticalSeed(R.TacticalSeed ? R.TacticalSeed : GetTypeHash(R.Id)); if(R.TacticalRandomSeed) Random.Initialize(R.TacticalRandomSeed);
    Target=nullptr; bTargetVisible=false; InvestigateLocation=R.Investigate; PatrolOrigin=R.Patrol; LastSense=R.LastSense;
    bInvestigating=R.InvestigationRemaining>0; LastStimulus=GetWorld()->GetTimeSeconds()-CastChecked<APersonaCharacter>(GetPawn())->CombatSettings()->MemoryTime+R.InvestigationRemaining;
    NextAttack=GetWorld()->GetTimeSeconds()+FMath::Max(.75f,R.AttackRemaining); NextSprint=GetWorld()->GetTimeSeconds()+R.SprintRemaining;
    if(R.Moving) MoveTactically(EPersonaTactic::Search,R.MoveGoal,FMath::Min(1.f,R.CommitRemaining));
}
UPersonaThinkTask::UPersonaThinkTask() { NodeName=TEXT("Perceive / choose / commit / attack"); bNotifyTick=true; bCreateNodeInstance=true; }
EBTNodeResult::Type UPersonaThinkTask::ExecuteTask(UBehaviorTreeComponent& Owner,uint8* Memory) { Elapsed=FMath::FRandRange(0,.2f); return EBTNodeResult::InProgress; }
void UPersonaThinkTask::TickTask(UBehaviorTreeComponent& Owner,uint8* Memory,float Delta)
{
    Elapsed+=Delta; if(Elapsed>=.2f) { Elapsed=0; if(auto AI=Cast<APersonaAIController>(Owner.GetAIOwner())) AI->Think(); }
}
