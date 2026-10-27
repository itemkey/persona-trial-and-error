#include "PersonaAI.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
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
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

APersonaAIController::APersonaAIController()
{
    Senses=CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Senses")); SetPerceptionComponent(*Senses);
    auto Sight=CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
    Sight->SightRadius=3500; Sight->LoseSightRadius=4000; Sight->PeripheralVisionAngleDegrees=80; Sight->SetMaxAge(7);
    Sight->DetectionByAffiliation.bDetectEnemies=Sight->DetectionByAffiliation.bDetectNeutrals=Sight->DetectionByAffiliation.bDetectFriendlies=true;
    auto Hearing=CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
    Hearing->HearingRange=6000; Hearing->SetMaxAge(7);
    Hearing->DetectionByAffiliation.bDetectEnemies=Hearing->DetectionByAffiliation.bDetectNeutrals=Hearing->DetectionByAffiliation.bDetectFriendlies=true;
    Senses->ConfigureSense(*Sight); Senses->ConfigureSense(*Hearing); Senses->SetDominantSense(Sight->GetSenseImplementation());
}
void APersonaAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn); PatrolOrigin=InPawn->GetActorLocation(); SetTacticalSeed(GetTypeHash(InPawn->GetName()));
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this,&ThisClass::Perceived);
    auto Tree=NewObject<UBehaviorTree>(this,TEXT("BT_PersonaTactical"));
    auto Root=NewObject<UBTComposite_Sequence>(Tree,TEXT("Root")); Tree->RootNode=Root;
    FBTCompositeChild Child; Child.ChildTask=NewObject<UPersonaThinkTask>(Tree); Root->Children.Add(Child); RunBehaviorTree(Tree);
}
void APersonaAIController::SetTacticalSeed(int32 Seed) { TacticalSeed=Seed; Random.Initialize(Seed); }
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
    else if(Sight && Target==Actor) { Target=nullptr; bTargetVisible=false; ClearFocus(EAIFocusPriority::Gameplay); }
}
bool APersonaAIController::AttackSlotAvailable(APersonaCharacter* Victim) const
{
    auto Self=CastChecked<APersonaCharacter>(GetPawn()); int32 Count=0;
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(*It!=Self && It->bNPC && It->IsAlive() && It->AttackState.Playing)
            if(auto AI=Cast<APersonaAIController>(It->Controller); AI && AI->Target==Victim)
            { ++Count; if(Self->ServerTime()-It->AttackState.Start<.2f) return false; }
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
    PreviousGoal=TacticalGoal; TacticalGoal=Goal; Tactic=Choice; CommitUntil=GetWorld()->GetTimeSeconds()+Duration;
    const bool Sneak=Choice==EPersonaTactic::Sneak || Choice==EPersonaTactic::Ambush || Choice==EPersonaTactic::Cover;
    if(Sneak) Self->Crouch(); else Self->UnCrouch();
    Self->NPCMoveSpeed=Sneak ? Profile->SneakSpeed : Choice==EPersonaTactic::Sprint ? Profile->SprintSpeed
        : Choice==EPersonaTactic::Patrol ? Profile->PatrolSpeed : Profile->CombatSpeed;
    const auto Result=MoveToLocation(Goal,75,true,true,false,true,nullptr,false);
    if(Result==EPathFollowingRequestResult::Failed) { ++FailedPaths; CommitUntil=0; } else FailedPaths=0;
}
void APersonaAIController::Think()
{
    auto Self=Cast<APersonaCharacter>(GetPawn());
    if(!Self || !Self->IsAlive()) { StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); return; }
    const float Now=GetWorld()->GetTimeSeconds(); const auto Profile=Self->CombatSettings();
    if(Now<StaggerUntil) { StopMovement(); return; }
    auto Victim=Cast<APersonaCharacter>(Target);
    if(Victim && (!Victim->IsAlive() || Victim->bTransit || !LineOfSightTo(Victim) ||
        FVector::DistSquared(Self->GetActorLocation(),Victim->GetActorLocation())>FMath::Square(4000.f)))
    { Target=nullptr; Victim=nullptr; bTargetVisible=false; ClearFocus(EAIFocusPriority::Gameplay); }
    if(Self->AttackState.Playing)
    {
        Tactic=EPersonaTactic::Attack; StopMovement(); ClearFocus(EAIFocusPriority::Gameplay);
        if(Victim && Self->ServerTime()-Self->AttackState.Start<Self->AttackState.Windup*.6f)
        {
            FRotator Desired=(Victim->GetActorLocation()-Self->GetActorLocation()).Rotation(); Desired.Pitch=Desired.Roll=0;
            Self->SetActorRotation(FMath::RInterpConstantTo(Self->GetActorRotation(),Desired,.2f,100)); SetControlRotation(Self->GetActorRotation());
        }
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
        if(Distance<Profile->AttackReach+35 && Now>=NextAttack && AttackSlotAvailable(Victim))
        {
            StopMovement(); Self->UnCrouch(); ClearFocus(EAIFocusPriority::Gameplay);
            FRotator Facing=(InvestigateLocation-Self->GetActorLocation()).Rotation(); Facing.Pitch=Facing.Roll=0;
            Self->SetActorRotation(Facing); SetControlRotation(Facing);
            Self->StartMeleeAttack(Random.FRand()<.2f ? 3 : Random.RandRange(0,2)); Tactic=EPersonaTactic::Attack;
            NextAttack=Now+Self->AttackState.Windup+Self->AttackState.Active+Self->AttackState.Recovery+Random.FRandRange(.15f,.45f);
            CommitUntil=0; return;
        }
        SetFocus(Victim);
        if(Now<CommitUntil && GetMoveStatus()==EPathFollowingStatus::Moving) return;
        if((Tactic==EPersonaTactic::Cover || Tactic==EPersonaTactic::Ambush) && Now<CommitUntil) return;
        EPersonaTactic Choice=EPersonaTactic::Approach;
        const bool Injured=Self->GetVitals()->GetHealth()<Self->GetVitals()->GetMaxHealth()*.4f;
        const float Roll=Random.FRand();
        if(Injured && Roll<.4f) Choice=EPersonaTactic::Retreat;
        else if(Distance<230 && !AttackSlotAvailable(Victim)) Choice=EPersonaTactic::Reposition;
        else if(Roll<.25f) Choice=EPersonaTactic::Cover;
        else if(Roll<.5f || TacticalSeed%3==1) Choice=EPersonaTactic::Flank;
        else if(Roll<.62f && Distance>600) Choice=EPersonaTactic::Sneak;
        else if(Distance>400 && Now>NextSprint) { Choice=EPersonaTactic::Sprint; NextSprint=Now+5; }
        FVector Goal=InvestigateLocation;
        if(Choice!=EPersonaTactic::Approach && Choice!=EPersonaTactic::Sprint && !SelectGoal(InvestigateLocation,Choice,Goal)) Choice=EPersonaTactic::Approach;
        if(Choice==EPersonaTactic::Sprint) Goal+=Victim->GetVelocity().GetClampedToMaxSize(500)*.45f;
        MoveTactically(Choice,Goal,Choice==EPersonaTactic::Sprint ? 1.6f : Random.FRandRange(1.1f,2.5f)); return;
    }
    ClearFocus(EAIFocusPriority::Gameplay);
    if(Now<CommitUntil && GetMoveStatus()==EPathFollowingStatus::Moving) return;
    if(bInvestigating && Now-LastStimulus<Profile->MemoryTime)
    {
        if(FVector::DistSquared(Self->GetActorLocation(),InvestigateLocation)>FMath::Square(160.f)) MoveTactically(EPersonaTactic::Search,InvestigateLocation,1.2f);
        else
        {
            if(Tactic!=EPersonaTactic::Ambush) { Tactic=EPersonaTactic::Ambush; CommitUntil=Now+Random.FRandRange(.7f,1.5f); StopMovement(); Self->Crouch(); }
            if(Now>=CommitUntil) { bInvestigating=false; Self->UnCrouch(); }
        }
        return;
    }
    bInvestigating=false; Self->UnCrouch();
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
    R.InvestigationRemaining=bInvestigating ? FMath::Max(0.f,7-(GetWorld()->GetTimeSeconds()-LastStimulus)) : 0;
    R.AttackRemaining=FMath::Max(0.f,NextAttack-GetWorld()->GetTimeSeconds());
    R.Moving=GetMoveStatus()==EPathFollowingStatus::Moving; R.MoveGoal=TacticalGoal;
    R.TacticalSeed=TacticalSeed; R.TacticalRandomSeed=Random.GetCurrentSeed(); R.Tactic=static_cast<uint8>(Tactic);
    R.CommitRemaining=FMath::Max(0.f,CommitUntil-GetWorld()->GetTimeSeconds()); R.SprintRemaining=FMath::Max(0.f,NextSprint-GetWorld()->GetTimeSeconds());
}
void APersonaAIController::RestoreState(const FPersonaSubjectRecord& R,AActor* SavedTarget)
{
    SetTacticalSeed(R.TacticalSeed ? R.TacticalSeed : GetTypeHash(R.Id)); if(R.TacticalRandomSeed) Random.Initialize(R.TacticalRandomSeed);
    Target=nullptr; bTargetVisible=false; InvestigateLocation=R.Investigate; PatrolOrigin=R.Patrol; LastSense=R.LastSense;
    bInvestigating=R.InvestigationRemaining>0; LastStimulus=GetWorld()->GetTimeSeconds()-7+R.InvestigationRemaining;
    NextAttack=GetWorld()->GetTimeSeconds()+FMath::Max(.75f,R.AttackRemaining); NextSprint=GetWorld()->GetTimeSeconds()+R.SprintRemaining;
    if(R.Moving) MoveTactically(EPersonaTactic::Search,R.MoveGoal,FMath::Min(1.f,R.CommitRemaining));
}
UPersonaThinkTask::UPersonaThinkTask() { NodeName=TEXT("Perceive / choose / commit / attack"); bNotifyTick=true; bCreateNodeInstance=true; }
EBTNodeResult::Type UPersonaThinkTask::ExecuteTask(UBehaviorTreeComponent& Owner,uint8* Memory) { Elapsed=FMath::FRandRange(0,.2f); return EBTNodeResult::InProgress; }
void UPersonaThinkTask::TickTask(UBehaviorTreeComponent& Owner,uint8* Memory,float Delta)
{
    Elapsed+=Delta; if(Elapsed>=.2f) { Elapsed=0; if(auto AI=Cast<APersonaAIController>(Owner.GetAIOwner())) AI->Think(); }
}
