#include "PersonaMovement.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaWeapon.h"
#include "PersonaAI.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

namespace
{
FVector TraversalPosition(const FPersonaTraversalState& State,float T)
{
    if(State.End.Z>=State.Start.Z)
    {
        const float Across=FMath::SmoothStep(.48f,1.f,T);
        FVector Result=FMath::Lerp(State.Start,State.End,Across);
        Result.Z=FMath::Lerp(State.Start.Z,State.End.Z+8,FMath::SmoothStep(0.f,.48f,T))-8*Across;
        return Result;
    }
    FVector Result=FMath::Lerp(State.Start,State.End,FMath::SmoothStep(0.f,.5f,T));
    Result.Z=FMath::Lerp(State.Start.Z+8,State.End.Z,FMath::SmoothStep(.5f,1.f,T));
    return Result;
}

class FPersonaSavedMove final : public FSavedMove_Character
{
public:
    using Super=FSavedMove_Character;
    bool Sprint = false, Aim = false;
    virtual void Clear() override { Super::Clear(); Sprint=Aim=false; }
    virtual uint8 GetCompressedFlags() const override
    { return Super::GetCompressedFlags() | (Sprint ? FLAG_Custom_0 : 0) | (Aim ? FLAG_Custom_1 : 0); }
    virtual bool CanCombineWith(const FSavedMovePtr& Other, ACharacter* Character, float MaxDelta) const override
    {
        const auto& Move=static_cast<const FPersonaSavedMove&>(*Other);
        return Sprint==Move.Sprint && Aim==Move.Aim && Super::CanCombineWith(Other,Character,MaxDelta);
    }
    virtual void SetMoveFor(ACharacter* Character,float Delta,FVector const& Accel,FNetworkPredictionData_Client_Character& Data) override
    {
        Super::SetMoveFor(Character,Delta,Accel,Data);
        auto Movement=CastChecked<UPersonaMovement>(Character->GetCharacterMovement());
        Sprint=Movement->bWantsSprint; Aim=Movement->bWantsAim;
    }
    virtual void PrepMoveFor(ACharacter* Character) override
    {
        Super::PrepMoveFor(Character);
        auto Movement=CastChecked<UPersonaMovement>(Character->GetCharacterMovement());
        Movement->bWantsSprint=Sprint; Movement->bWantsAim=Aim;
    }
};
class FPersonaPrediction final : public FNetworkPredictionData_Client_Character
{
public:
    using Super=FNetworkPredictionData_Client_Character;
    explicit FPersonaPrediction(const UCharacterMovementComponent& Movement) : Super(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FPersonaSavedMove()); }
};
}
UPersonaMovement::UPersonaMovement()
{
    MaxWalkSpeed=600; MaxWalkSpeedCrouched=280; MaxAcceleration=4200;
    BrakingDecelerationWalking=5200; GroundFriction=6; AirControl=.55f;
    AirControlBoostMultiplier=1; BrakingDecelerationFalling=80;
}
bool UPersonaMovement::ServerCheckClientError(float TimeStamp,float DeltaTime,const FVector& Accel,const FVector& ClientWorldLocation,const FVector& RelativeClientLocation,FMovementBaseInterfaceData* Base,FName Bone,uint8 Mode)
{
    if(auto C=Cast<APersonaCharacter>(CharacterOwner); C && C->bTransit) return false;
    return Super::ServerCheckClientError(TimeStamp,DeltaTime,Accel,ClientWorldLocation,RelativeClientLocation,Base,Bone,Mode);
}
void UPersonaMovement::ServerMoveHandleClientError(float TimeStamp,float DeltaTime,const FVector& Accel,const FVector& RelativeClientLocation,FMovementBaseInterfaceData* Base,FName Bone,uint8 Mode)
{
    // The lift owns positions, but moves still need ACKs and a continuous clock.
    if(auto C=Cast<APersonaCharacter>(CharacterOwner); C && C->bTransit)
        GetPredictionData_Server_Character()->bForceClientUpdate=false;
    Super::ServerMoveHandleClientError(TimeStamp,DeltaTime,Accel,RelativeClientLocation,Base,Bone,Mode);
}
bool UPersonaMovement::CanSprint() const
{
    auto C=Cast<APersonaCharacter>(CharacterOwner);
    return C && C->IsAlive() && !C->bTransit && !C->bOffline && !C->bIsCrouched && !bWantsToCrouch
        && !bWantsAim && C->GetVitals()->GetStamina()>(C->bSprinting ? 0.f : 5.f);
}
float UPersonaMovement::GetMaxSpeed() const
{
    auto C=Cast<APersonaCharacter>(CharacterOwner);
    if(!C || MovementMode==MOVE_Flying || MovementMode==MOVE_Swimming) return Super::GetMaxSpeed();
    if(!C->IsAlive() || C->bTransit) return 0;
    if(C->bNPC) return NPCSpeedLimit>=0 ? NPCSpeedLimit : C->NPCMoveSpeed;
    if(bWantsAim && C->Weapon && C->Weapon->Item()) return C->bIsCrouched ? CrouchedAimSpeed : AimSpeed;
    if(C->bIsCrouched) return MaxWalkSpeedCrouched;
    return bWantsSprint && CanSprint() ? C->SprintSpeed : C->WalkSpeed;
}
void UPersonaMovement::UpdateFromCompressedFlags(uint8 Flags)
{
    Super::UpdateFromCompressedFlags(Flags);
    bWantsSprint=(Flags&FSavedMove_Character::FLAG_Custom_0)!=0;
    bWantsAim=(Flags&FSavedMove_Character::FLAG_Custom_1)!=0;
}
void UPersonaMovement::UpdateCharacterStateBeforeMovement(float Delta)
{
    if(auto C=Cast<APersonaCharacter>(CharacterOwner); C && C->bNPC)
    {
        const auto P=C->CombatSettings(); float Target=(C->bIsCrouched ? FMath::Min(P->SneakSpeed,C->NPCMoveSpeed) : C->NPCMoveSpeed)*C->LegSpeedScale();
        if(auto AI=Cast<APersonaAIController>(C->Controller); AI && AI->bTargetVisible && AI->Target &&
            (AI->Tactic==EPersonaTactic::Approach || AI->Tactic==EPersonaTactic::Sprint || AI->Tactic==EPersonaTactic::Attack))
        {
            const FVector To=AI->Target->GetActorLocation()-C->GetActorLocation();
            if(FMath::Abs(To.Z)<70)
            {
                const float Along=FMath::Max(0.f,FVector::DotProduct(AI->Target->GetVelocity(),To.GetSafeNormal2D()));
                Target=FMath::Min(Target,Along+FMath::Sqrt(2*P->Braking*FMath::Max(0.f,float(To.Size2D())-P->AttackStandDistance)));
            }
        }
        NPCSpeedLimit=NPCSpeedLimit<0 ? Target : FMath::FInterpConstantTo(NPCSpeedLimit,Target,Delta,Target<NPCSpeedLimit ? P->Braking : P->Acceleration);
    }
    if(auto C=Cast<APersonaCharacter>(CharacterOwner); C && (C->HasAuthority() || C->IsLocallyControlled()) && !C->bNPC)
    {
        C->bSprinting=bWantsSprint && CanSprint();
        if(C->HasAuthority() && C->Weapon) C->Weapon->bAiming=bWantsAim && C->Weapon->Item() && C->IsAlive() && !C->bTransit;
    }
    Super::UpdateCharacterStateBeforeMovement(Delta);
}
void UPersonaMovement::HandleImpact(const FHitResult& Hit,float TimeSlice,const FVector& MoveDelta)
{
    if(Hit.bBlockingHit) ++CollisionRevision;
    Super::HandleImpact(Hit,TimeSlice,MoveDelta);
}
FNetworkPredictionData_Client* UPersonaMovement::GetPredictionData_Client() const
{
    if(!ClientPredictionData) const_cast<UPersonaMovement*>(this)->ClientPredictionData=new FPersonaPrediction(*this);
    return ClientPredictionData;
}

bool UPersonaMovement::BeginTraversal(const FVector& FloorTarget,UPrimitiveComponent* Support)
{
    auto C=Cast<APersonaCharacter>(CharacterOwner);
    TraversalFailure.Reset();
    auto Reject=[&](const FString& Reason) { TraversalFailure=Reason; return false; };
    if(!C || !C->HasAuthority() || !C->bNPC || !C->IsAlive() || C->Traversal.Active || !Support) return false;
    bWantsToCrouch=false; UnCrouch(false); if(C->bIsCrouched) return Reject(TEXT("Standing clearance"));
    const float Half=C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const FVector End=FloorTarget+FVector(0,0,Half+2);
    const FVector Start=C->GetActorLocation();
    if(FMath::Abs(End.Z-Start.Z)>245 || FVector::Dist2D(End,Start)>250) return Reject(TEXT("Transition range"));
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        if(*It!=C && It->Traversal.Active && FVector::DistSquared(It->Traversal.End,End)<FMath::Square(90.f)) return Reject(TEXT("Reserved landing"));
    FCollisionQueryParams Q(SCENE_QUERY_STAT(TraversalLanding),false,C);
    if(GetWorld()->OverlapBlockingTestByChannel(End,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(32,Half-1),Q)) return Reject(TEXT("Occupied landing"));
    FHitResult Floor;
    if(!GetWorld()->LineTraceSingleByChannel(Floor,FloorTarget+FVector(0,0,12),FloorTarget-FVector(0,0,25),ECC_Visibility,Q) || Floor.ImpactNormal.Z<.7f) return Reject(TEXT("Missing landing support"));
    FPersonaTraversalState Candidate; Candidate.Start=Start; Candidate.End=End;
    FVector Previous=Start;
    for(int32 I=1;I<=16;++I)
    {
        const FVector Next=TraversalPosition(Candidate,float(I)/16);
        FHitResult Block;
        if(GetWorld()->SweepSingleByChannel(Block,Previous,Next,FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(C->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half),Q)) return Reject(TEXT("Blocked trajectory: ")+GetNameSafe(Block.GetActor()));
        Previous=Next;
    }
    StopMovementImmediately(); C->CancelMeleeAttack();
    C->Traversal.Active=true; C->Traversal.Start=Start;
    C->Traversal.End=FloorTarget+FVector(0,0,C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+2);
    C->Traversal.Ledge=FloorTarget; C->Traversal.Started=C->ServerTime();
    C->Traversal.Jump=End.Z-Start.Z<95;
    C->Traversal.Duration=C->Traversal.Jump ? .65f : FMath::GetMappedRangeValueClamped(FVector2D(95,240),FVector2D(.95,1.4),End.Z-Start.Z);
    const FVector Facing=End-Start; if(C->Controller) C->Controller->SetControlRotation(FRotator(0,Facing.Rotation().Yaw,0));
    TraversalSupport=Support; TraversalSupportTransform=Support->GetComponentTransform();
    SetMovementMode(MOVE_Custom,1); C->ForceNetUpdate(); return true;
}
void UPersonaMovement::AbortTraversal()
{
    if(auto C=Cast<APersonaCharacter>(CharacterOwner)) { C->Traversal.Active=false; C->ForceNetUpdate(); }
    TraversalSupport.Reset(); if(MovementMode==MOVE_Custom) SetMovementMode(MOVE_Falling);
}
void UPersonaMovement::PhysCustom(float Delta,int32 Iterations)
{
    auto C=Cast<APersonaCharacter>(CharacterOwner);
    if(!C || CustomMovementMode!=1) { Super::PhysCustom(Delta,Iterations); return; }
    if(!C->HasAuthority()) return;
    if(!C->IsAlive() || !TraversalSupport.IsValid() || !TraversalSupport->GetComponentTransform().Equals(TraversalSupportTransform,1.f)) { AbortTraversal(); return; }
    const auto& State=C->Traversal;
    const float T=FMath::Clamp((C->ServerTime()-State.Started)/State.Duration,0.f,1.f);
    const FVector Desired=TraversalPosition(State,T);
    FHitResult Hit; const FVector Before=C->GetActorLocation();
    const FRotator Facing(0,(State.End-State.Start).Rotation().Yaw,0);
    const FQuat Rotation=FMath::RInterpConstantTo(C->GetActorRotation(),Facing,Delta,C->CombatSettings()->TurnRate).Quaternion();
    SafeMoveUpdatedComponent(Desired-Before,Rotation,true,Hit);
    Velocity=(C->GetActorLocation()-Before)/FMath::Max(Delta,.001f);
    if(Hit.bBlockingHit && Hit.Time<.95f) { TraversalFailure=TEXT("Interrupted trajectory: ")+GetNameSafe(Hit.GetActor()); AbortTraversal(); return; }
    if(T>=1) { C->Traversal.Active=false; TraversalSupport.Reset(); SetMovementMode(MOVE_Walking); C->ForceNetUpdate(); }
}
