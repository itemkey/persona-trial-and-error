#include "PersonaMovement.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaWeapon.h"

namespace
{
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
    if(C->bNPC) return (C->bIsCrouched ? FMath::Min(200.f,C->NPCMoveSpeed) : C->NPCMoveSpeed)*C->LegSpeedScale();
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
    if(auto C=Cast<APersonaCharacter>(CharacterOwner); C && (C->HasAuthority() || C->IsLocallyControlled()) && !C->bNPC)
    {
        C->bSprinting=bWantsSprint && CanSprint();
        if(C->HasAuthority() && C->Weapon) C->Weapon->bAiming=bWantsAim && C->Weapon->Item() && C->IsAlive() && !C->bTransit;
    }
    Super::UpdateCharacterStateBeforeMovement(Delta);
}
FNetworkPredictionData_Client* UPersonaMovement::GetPredictionData_Client() const
{
    if(!ClientPredictionData) const_cast<UPersonaMovement*>(this)->ClientPredictionData=new FPersonaPrediction(*this);
    return ClientPredictionData;
}
