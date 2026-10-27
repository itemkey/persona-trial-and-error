#include "PersonaCombat.h"
#include "PersonaCharacter.h"
#include "PersonaAI.h"
#include "PersonaAttributes.h"
#include "PersonaWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarPersonaCameraMotion(TEXT("persona.CameraMotion"),1.f,
    TEXT("Local camera bob, lean and landing intensity. 0 disables cosmetic motion; range 0-2."),ECVF_Default);

float APersonaCharacter::ServerTime() const
{ const auto GS=GetWorld()->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); }
const UPersonaCombatProfile* APersonaCharacter::CombatSettings() const
{ return CombatProfile ? CombatProfile.Get() : GetDefault<UPersonaCombatProfile>(); }
float APersonaCharacter::LegSpeedScale() const
{ return ServerTime()<LegImpairedUntil ? CombatSettings()->LegSlowScale : 1.f; }
void APersonaCharacter::OnStartCrouch(float Height,float Scaled)
{
    Super::OnStartCrouch(Height,Scaled);
    CameraHeight+=Scaled;
}
void APersonaCharacter::OnEndCrouch(float Height,float Scaled)
{
    Super::OnEndCrouch(Height,Scaled);
    CameraHeight-=Scaled;
}
void APersonaCharacter::Landed(const FHitResult& Hit)
{
    LandingOffset=-FMath::Clamp(FMath::Abs(GetVelocity().Z)*.005f,0.f,3.f);
    Super::Landed(Hit);
}
void APersonaCharacter::UpdateView(float Delta)
{
    if(bNPC) return;
    const float TargetHeight=bIsCrouched ? CrouchedEyeHeight : 68.f;
    // Exponential decay remains stable across frame rates and capsule corrections.
    CameraHeight=FMath::Lerp(TargetHeight,CameraHeight,FMath::Exp(-Delta*4.6f/FMath::Max(.05f,CrouchTransitionTime)));
    if(!IsLocallyControlled()) { Camera->SetRelativeLocation(FVector(0,0,CameraHeight)); return; }
    const float Amount=FMath::Clamp(CameraMotionIntensity*CVarPersonaCameraMotion.GetValueOnGameThread(),0.f,2.f);
    const float Aim=Weapon->AimAlpha;
    const float Motion=Amount*(1-.85f*Aim)*(IsAlive() && !bTransit ? 1.f : 0.f);
    const FVector Velocity=GetVelocity();
    const FVector Acceleration=(Velocity-PreviousViewVelocity)/FMath::Max(Delta,.001f);
    const FRotator Control=GetBaseAimRotation();
    const float Turn=FMath::FindDeltaAngleDegrees(PreviousViewYaw,Control.Yaw)/FMath::Max(Delta,.001f);
    const FVector LocalAcceleration=FRotationMatrix(FRotator(0,Control.Yaw,0)).GetTransposed().TransformVector(Acceleration);
    const float Gait=GetCharacterMovement()->IsMovingOnGround() ? HandMoveAlpha : 0.f;
    const float Bob=FMath::Cos(HandMotionTime*2)*Gait*(bSprinting ? 1.1f : .55f);
    LandingOffset*=FMath::Exp(-Delta*12);
    DamageFlash=FMath::Max(0.f,DamageFlash-Delta*2.5f);
    DamageKick*=FMath::Exp(-Delta*13);
    const FRotator SwayTarget(
        FMath::Clamp(-LocalAcceleration.X*.00012f,-.5f,.5f),
        0,
        FMath::Clamp(-Turn*.0014f-LocalAcceleration.Y*.0001f,-.7f,.7f));
    ViewSway=FMath::RInterpTo(ViewSway,SwayTarget,Delta,12);
    Camera->SetRelativeLocation(FVector(0,FMath::Sin(HandMotionTime)*Gait*.3f*Motion,CameraHeight+(Bob+LandingOffset)*Motion));
    FRotator View=Control+ViewSway*Motion;
    View.Pitch+=DamageKick*.8f*Amount;
    Camera->SetWorldRotation(View);
    const float FOV=FMath::Lerp(bSprinting ? SprintFOV : NormalFOV,AimFOV,Aim);
    Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView,FOV,Delta,10));
    Camera->SetFirstPersonFieldOfView(ViewmodelFOV);
    PreviousViewVelocity=Velocity; PreviousViewYaw=Control.Yaw;
}
void APersonaCharacter::ClientDamageFeedback_Implementation(float Amount,FVector_NetQuantizeNormal Direction)
{
    if(!IsLocallyControlled() || bDead) return;
    DamageFlash=FMath::Clamp(DamageFlash+Amount/55.f,.25f,1.f);
    DamageKick=FMath::Min(1.2f,DamageKick+Amount/45.f); DamageDirection=Direction;
    if(auto Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Persona/Audio/HitBody.HitBody")))
        UGameplayStatics::PlaySound2D(this,Sound,.8f,.85f);
}
void APersonaCharacter::ReactToDamage(FName Bone,const FVector& Direction,float Damage)
{
    Reaction.Sequence++; Reaction.Region=PersonaBodyRegion(Bone); Reaction.Start=ServerTime();
    const auto Profile=CombatSettings();
    const bool Leg=Reaction.Region==EPersonaBodyRegion::LeftLeg || Reaction.Region==EPersonaBodyRegion::RightLeg;
    const bool Arm=Reaction.Region==EPersonaBodyRegion::LeftArm || Reaction.Region==EPersonaBodyRegion::RightArm;
    Reaction.Duration=FMath::Max(.05f,Reaction.Region==EPersonaBodyRegion::Head ? Profile->HeadReactionDuration
        : Leg ? Profile->LegReactionDuration : Arm ? Profile->ArmReactionDuration : Profile->TorsoReactionDuration);
    Reaction.Strength=FMath::Clamp(Damage*Profile->ImpactImpulsePerDamage,0.f,Profile->MaxImpactImpulse); Reaction.Direction=Direction;
    OnRep_Reaction();
    if(bNPC && !bDead && ServerTime()>=ImpairmentImmuneUntil)
    {
        if(Leg) LegImpairedUntil=ServerTime()+CombatSettings()->LegSlowDuration;
        if(Arm || Leg || Reaction.Region==EPersonaBodyRegion::Head) CancelMeleeAttack();
        if(auto AI=Cast<APersonaAIController>(Controller))
        {
            AI->StaggerUntil=GetWorld()->GetTimeSeconds()+(Arm ? Profile->ArmInterruptDuration : Reaction.Duration);
            AI->StopMovement();
        }
        ImpairmentImmuneUntil=ServerTime()+CombatSettings()->ImpairmentCooldown;
    }
    ForceNetUpdate();
}
void APersonaCharacter::OnRep_Reaction()
{
    if(!Reaction.Sequence || ServerTime()-Reaction.Start>Reaction.Duration+.3f) return;
    static const FName Bones[]={TEXT("spine_02"),TEXT("head"),TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("thigh_l"),TEXT("thigh_r")};
    HitReaction_Implementation(Bones[static_cast<uint8>(Reaction.Region)],Reaction.Direction,Reaction.Strength);
}
void APersonaCharacter::StartMeleeAttack(int32 Variant)
{
    if(!HasAuthority() || !bNPC || !IsAlive() || AttackState.Playing) return;
    const auto Profile=CombatSettings();
    AttackState.Sequence++; AttackState.Variant=FMath::Clamp(Variant,0,3);
    AttackState.Start=ServerTime(); AttackState.Playing=true;
    AttackState.Windup=Variant==3 ? Profile->HeavyWindup : Profile->NormalWindup;
    AttackState.Active=Profile->ActiveTime; AttackState.Recovery=Profile->RecoveryTime;
    bStrikeApplied=false; GetCharacterMovement()->StopMovementImmediately(); ForceNetUpdate();
}
void APersonaCharacter::CancelMeleeAttack()
{
    if(HasAuthority()) { AttackState.Playing=false; bStrikeApplied=true; ForceNetUpdate(); }
}
void APersonaCharacter::UpdateMelee(float Delta)
{
    if(!AttackState.Playing) return;
    if(!IsAlive()) { CancelMeleeAttack(); return; }
    const float Elapsed=ServerTime()-AttackState.Start;
    const float End=AttackState.Windup+AttackState.Active;
    if(Elapsed>=End+AttackState.Recovery) { CancelMeleeAttack(); return; }
    if(Elapsed<AttackState.Windup || Elapsed>End || bStrikeApplied) return;
    const auto Profile=CombatSettings();
    const FVector Start=GetActorLocation()+FVector(0,0,22);
    const FVector Forward=GetActorForwardVector();
    TArray<FHitResult> Hits;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PersonaMelee),true,this);
    GetWorld()->SweepMultiByChannel(Hits,Start,Start+Forward*Profile->AttackReach,FQuat::Identity,
        ECC_GameTraceChannel1,FCollisionShape::MakeSphere(30),Query);
    for(const auto& Hit:Hits)
    {
        auto Victim=Cast<APersonaCharacter>(Hit.GetActor());
        if(!Victim || Victim->bNPC || !Victim->IsAlive()) continue;
        FHitResult Wall;
        if(GetWorld()->LineTraceSingleByChannel(Wall,Start,Hit.ImpactPoint,ECC_Visibility,Query) && Wall.GetActor()!=Victim) continue;
        bStrikeApplied=true;
        UGameplayStatics::ApplyPointDamage(Victim,(AttackState.Variant==3 ? Profile->HeavyDamage : Profile->NormalDamage)*DamageScale,
            Forward,Hit,Controller,this,UDamageType::StaticClass());
        break;
    }
}
