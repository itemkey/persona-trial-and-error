#include "PersonaWeapon.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaLoot.h"
#include "PersonaAttributes.h"
#include "PersonaMovement.h"
#include "Curves/CurveFloat.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AISense_Hearing.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
float Time(UWorld* W) { auto GS=W->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : W->GetTimeSeconds(); }
bool Ammo(APersonaProp* P) { return P && P->Definition && P->Definition->GetName()==TEXT("DA_Ammo12GA"); }
uint8 FireSoundVariant(int32 Sequence,int32 Previous)
{ const uint8 Candidate=(Sequence*3)%4+1; return Candidate==Previous ? Candidate%4+1 : Candidate; }
int32 CountAmmo(APersonaCharacter* C) { int32 N=0; for(auto P:C->Interaction->Slots) if(Ammo(P)) N+=P->Quantity; return N; }
bool Consume(APersonaCharacter* C)
{
    for(auto P:C->Interaction->Slots) if(Ammo(P) && P->Quantity>0) { --P->Quantity; if(!P->Quantity) { C->Interaction->Remove(P); P->Destroy(); } else P->ForceNetUpdate(); return true; } return false;
}
void ReturnShell(APersonaCharacter* C)
{
    FPersonaInventoryRecord R; R.Definition=TEXT("/Game/Persona/Items/DA_Ammo12GA.DA_Ammo12GA");
    if(auto P=UPersonaLoot::Spawn(C->GetWorld(),R,FTransform(C->GetActorLocation()+C->GetActorForwardVector()*80))) C->Interaction->Store(P);
}
void Sound(UObject* Context,FName Name,FVector Location,bool Local=false)
{
    auto S=LoadObject<USoundBase>(nullptr,*FString::Printf(TEXT("/Game/Persona/Audio/%s.%s"),*Name.ToString(),*Name.ToString()));
    if(S) { if(Local) UGameplayStatics::PlaySound2D(Context,S,.8f); else UGameplayStatics::PlaySoundAtLocation(Context,S,Location,.8f); }
}
}
APersonaFX::APersonaFX()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false);
}
void APersonaFX::Play(UWorld* World,FName Kind,const FVector& Location,const FRotator& Rotation)
{
    if(World->GetNetMode()==NM_DedicatedServer) return;
    auto Data=LoadObject<UPersonaFXFrames>(nullptr,*FString::Printf(TEXT("/Game/Persona/FX/DA_%s.DA_%s"),*Kind.ToString(),*Kind.ToString()));
    if(!Data || Data->Frames.IsEmpty()) return;
    auto A=World->SpawnActor<APersonaFX>(Location,Rotation); A->Frames=Data; A->SetLifeSpan(Data->Frames.Num()/30.f+.05f); A->Mesh->SetStaticMesh(Data->Frames[0]);
}
void APersonaFX::Tick(float Delta)
{
    Super::Tick(Delta); Age+=Delta; if(Frames) { int32 I=FMath::FloorToInt(Age*30); if(Frames->Frames.IsValidIndex(I)) Mesh->SetStaticMesh(Frames->Frames[I]); else Destroy(); }
}
UPersonaWeapon::UPersonaWeapon() { SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true; }
void UPersonaWeapon::BeginPlay()
{
    Super::BeginPlay();
    if(GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    for(const FString Name:{TEXT("ShotgunFire1"),TEXT("ShotgunFire2"),TEXT("ShotgunFire3"),TEXT("ShotgunFire4"),TEXT("Pump"),TEXT("ShellLoad"),TEXT("ShellDrop"),TEXT("Click"),TEXT("HitBody"),TEXT("HitWall"),TEXT("HitSoft"),TEXT("BulletWhiz")})
        if(auto Asset=LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Persona/Audio/")+Name+TEXT(".")+Name))) PresentationAssets.Add(Asset);
    for(const FString Name:{TEXT("Muzzle"),TEXT("Human"),TEXT("Wall")})
        if(auto Asset=LoadObject<UPersonaFXFrames>(nullptr,*(TEXT("/Game/Persona/FX/DA_")+Name+TEXT(".DA_")+Name))) PresentationAssets.Add(Asset);
}
void UPersonaWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPersonaWeapon,Action); DOREPLIFETIME(UPersonaWeapon,ActionStart); DOREPLIFETIME(UPersonaWeapon,bAiming); }
APersonaProp* UPersonaWeapon::Item() const
{ auto C=Cast<APersonaCharacter>(GetOwner()); auto P=C ? C->Interaction->Held.Get() : nullptr; return P && P->Definition && P->Definition->bShotgun ? P : nullptr; }
bool UPersonaWeapon::CanAct() const { auto C=Cast<APersonaCharacter>(GetOwner()); return C && C->IsAlive() && !C->bTransit && !C->bSpectator && Item(); }
void UPersonaWeapon::SetAim(bool Value)
{
    bAiming=Value && CanAct();
    auto C=CastChecked<APersonaCharacter>(GetOwner());
    CastChecked<UPersonaMovement>(C->GetCharacterMovement())->bWantsAim=bAiming;
    if(bAiming) C->SetSprintIntent(false);
    ServerAction(1,bAiming,Item());
}
void UPersonaWeapon::Load(bool Held) { ServerAction(2,Held,Item()); }
void UPersonaWeapon::Unload(bool Held) { ServerAction(3,Held,Item()); }
void UPersonaWeapon::Pump() { ServerAction(4,false,Item()); }
void UPersonaWeapon::Inspect() { InspectUntil=Time(GetWorld())+2.5f; ServerAction(5,false,Item()); }
void UPersonaWeapon::Safety() { ServerAction(6,false,Item()); }
void UPersonaWeapon::Cancel()
{
    bAiming=false; bLoadHeld=bUnloadHeld=false; bPendingFire=false; bPumpQueued=false; BufferedSequence=0;
    if(auto C=Cast<APersonaCharacter>(GetOwner())) CastChecked<UPersonaMovement>(C->GetCharacterMovement())->bWantsAim=false;
    if(GetOwner()->HasAuthority()) { Action=EPersonaWeaponAction::Idle; ActiveItem.Reset(); }
    else ServerAction(0,false,Item());
}
void UPersonaWeapon::StartAction(EPersonaWeaponAction Value,float Duration)
{ Action=Value; ActionStart=Time(GetWorld()); ActionEnd=ActionStart+Duration; ActiveItem=Item(); GetOwner()->ForceNetUpdate(); }
void UPersonaWeapon::ServerAction_Implementation(uint8 Command,bool Held,APersonaProp* ExpectedGun)
{
    if(ExpectedGun!=Item()) return;
    if(Command==0) { Cancel(); return; }
    if(Command==1)
    {
        bAiming=Held && CanAct();
        auto C=CastChecked<APersonaCharacter>(GetOwner());
        CastChecked<UPersonaMovement>(C->GetCharacterMovement())->bWantsAim=bAiming;
        if(bAiming) C->SetSprintIntent(false);
        return;
    }
    if(!CanAct()) return;
    auto P=Item(); auto C=CastChecked<APersonaCharacter>(GetOwner());
    if(Command==2) { bLoadHeld=Held; if(!Held) return; }
    if(Command==3) { bUnloadHeld=Held; if(!Held) return; }
    if(Command==4 && Action==EPersonaWeaponAction::Load) { bPumpQueued=true; bLoadHeld=false; return; }
    if(Action!=EPersonaWeaponAction::Idle) return;
    if(Command==2 && P->WeaponState.Tube<6 && CountAmmo(C)>0) { StartAction(EPersonaWeaponAction::Load,P->Definition->LoadDuration); MechanicalFX(TEXT("ShellLoad")); }
    if(Command==3 && (P->WeaponState.Tube>0 || P->WeaponState.Chamber!=EPersonaChamber::Empty)) StartAction(EPersonaWeaponAction::Unload,.65f);
    if(Command==4) { StartAction(EPersonaWeaponAction::Pump,P->Definition->PumpDuration); MechanicalFX(TEXT("Pump")); }
    if(Command==5) StartAction(EPersonaWeaponAction::Inspect,1.3f);
    if(Command==6) { P->WeaponState.Safety=!P->WeaponState.Safety; P->ForceNetUpdate(); MechanicalFX(TEXT("Click")); }
}
void UPersonaWeapon::FinishAction()
{
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item(); auto Previous=Action;
    Action=EPersonaWeaponAction::Idle;
    if(!P || P!=ActiveItem || !C->IsAlive()) { ActiveItem.Reset(); return; }
    auto& S=P->WeaponState;
    if(Previous==EPersonaWeaponAction::Load && S.Tube<6 && Consume(C)) ++S.Tube;
    if(Previous==EPersonaWeaponAction::Pump)
    {
        if(S.Chamber==EPersonaChamber::Live) ReturnShell(C);
        else if(S.Chamber==EPersonaChamber::Spent) EjectShell();
        S.Chamber=S.Tube>0 ? EPersonaChamber::Live : EPersonaChamber::Empty; if(S.Tube>0) --S.Tube;
    }
    if(Previous==EPersonaWeaponAction::Unload)
    {
        if(S.Chamber!=EPersonaChamber::Empty) { if(S.Chamber==EPersonaChamber::Live) ReturnShell(C); else EjectShell(); S.Chamber=EPersonaChamber::Empty; }
        else if(S.Tube>0) { --S.Tube; ReturnShell(C); }
        MechanicalFX(TEXT("ShellLoad"));
    }
    P->ForceNetUpdate(); Acknowledge();
    if(Previous==EPersonaWeaponAction::Load && bPumpQueued)
    { bPumpQueued=false; bLoadHeld=false; StartAction(EPersonaWeaponAction::Pump,P->Definition->PumpDuration); MechanicalFX(TEXT("Pump")); return; }
    if(Previous==EPersonaWeaponAction::Pump && BufferedSequence)
    {
        const int32 Sequence=BufferedSequence; BufferedSequence=0;
        ExecuteFire(BufferedAim,Sequence,BufferedSound); return;
    }
    if(Previous==EPersonaWeaponAction::Pump && bLoadHeld && S.Tube<6 && CountAmmo(C)>0)
    { StartAction(EPersonaWeaponAction::Load,P->Definition->LoadDuration); MechanicalFX(TEXT("ShellLoad")); return; }
    if(Previous==EPersonaWeaponAction::Load && bLoadHeld && S.Tube<6 && CountAmmo(C)>0) { StartAction(Previous,P->Definition->LoadDuration); MechanicalFX(TEXT("ShellLoad")); }
    if(Previous==EPersonaWeaponAction::Unload && bUnloadHeld && S.Tube>0) StartAction(Previous,.65f);
}
void UPersonaWeapon::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick); auto C=Cast<APersonaCharacter>(GetOwner()); if(!C) return;
    if(GetOwner()->HasAuthority()) { if(Action!=EPersonaWeaponAction::Idle && (!CanAct() || Item()!=ActiveItem)) Cancel(); else if(Action!=EPersonaWeaponAction::Idle && Time(GetWorld())>=ActionEnd) FinishAction(); }
    AimAlpha=FMath::FInterpTo(AimAlpha,bAiming && CanAct() && !C->bSprinting ? 1.f : 0.f,Delta,12);
    RecoilAge+=Delta; Recoil=RecoilValue(nullptr);
    if(C->IsLocallyControlled() && C->Controller && RecoilAge<ShotDuration+Delta)
    {
        const float T=FMath::Min(RecoilAge,ShotDuration);
        const float Kick=T<.025f ? FMath::SmoothStep(0.f,.025f,T) : 1-FMath::SmoothStep(.025f,ShotDuration,T);
        const float Held=FMath::SmoothStep(0.f,.025f,T);
        const float Pitch=ShotResidual*Held+(ShotKick-ShotResidual)*Kick;
        const float Yaw=ShotYaw*(.5f*Held+.5f*Kick);
        FRotator Rotation=C->Controller->GetControlRotation();
        // Apply only this frame's recoil delta: mouse input remains independent.
        float PitchDelta=Pitch-AppliedCameraRecoil;
        if(PitchDelta<0)
        {
            // Do not return camera motion that was clipped by the upper limit.
            const float ReturnBlocked=FMath::Min(-PitchDelta,ClippedPitchRecoil);
            PitchDelta+=ReturnBlocked; ClippedPitchRecoil-=ReturnBlocked;
        }
        const float Before=FRotator::NormalizeAxis(Rotation.Pitch);
        Rotation.Pitch=FMath::Clamp(Before+PitchDelta,-88.f,88.f);
        if(PitchDelta>0) ClippedPitchRecoil+=FMath::Max(0.f,Before+PitchDelta-Rotation.Pitch);
        Rotation.Yaw+=Yaw-AppliedYawRecoil;
        C->Controller->SetControlRotation(Rotation);
        AppliedCameraRecoil=Pitch; AppliedYawRecoil=Yaw;
    }
    if(bPendingFire && Time(GetWorld())>=RaiseUntil) { bPendingFire=false; Fire(); }
}
float UPersonaWeapon::RecoilValue(const UCurveFloat* Curve) const
{
    if(RecoilAge>=ShotDuration) return 0;
    if(Curve) return Curve->GetFloatValue(RecoilAge)*ShotAimScale;
    return (RecoilAge<.025f ? FMath::SmoothStep(0.f,.025f,RecoilAge) : FMath::Exp(-(RecoilAge-.025f)*7.f)*(1-FMath::SmoothStep(.3f,ShotDuration,RecoilAge)))*ShotAimScale;
}
void UPersonaWeapon::LocalShot(int32 Sequence,uint8 SoundVariant)
{
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item(); if(!P) return;
    const auto D=P->Definition;
    ShotAimScale=FMath::Lerp(1.f,D->RecoilAimScale,AimAlpha);
    ShotKick=D->CameraRecoilDegrees*ShotAimScale; ShotResidual=D->ResidualRecoilDegrees*ShotAimScale;
    ShotDuration=FMath::Max(.1f,D->RecoilDuration);
    FRandomStream Random(Sequence*7919); ShotYaw=Random.FRandRange(-D->RecoilYawDegrees,D->RecoilYawDegrees)*ShotAimScale;
    RecoilAge=0; Recoil=1; AppliedCameraRecoil=AppliedYawRecoil=ClippedPitchRecoil=0;
    LastSoundVariant=SoundVariant; LastPresentedSequence=Sequence;
    Sound(this,FName(FString::Printf(TEXT("ShotgunFire%d"),LastSoundVariant)),C->GetActorLocation(),true);
    APersonaFX::Play(GetWorld(),TEXT("Muzzle"),P->OwnerViewMesh->GetComponentTransform().TransformPosition(P->Definition->Muzzle),C->Camera->GetComponentRotation());
}
void UPersonaWeapon::Fire()
{
    if(!CanAct()) return;
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item();
    if(C->bSprinting)
    {
        C->SetSprintIntent(false); C->ServerSetSprinting(false); RaiseUntil=Time(GetWorld())+.14f; bPendingFire=true; return;
    }
    const FVector Aim=C->GetBaseAimRotation().Vector(); const int32 Sequence=++LocalSequence;
    const uint8 SoundVariant=FireSoundVariant(Sequence,LastSoundVariant);
    if(P->WeaponState.Chamber==EPersonaChamber::Live && !P->WeaponState.Safety && Action!=EPersonaWeaponAction::Pump && Time(GetWorld())>=NextShot)
    { if(!GetOwner()->HasAuthority()) { LocalShot(Sequence,SoundVariant); NextShot=Time(GetWorld())+.3f; } }
    ServerFire(Aim,Sequence,SoundVariant,P);
}
FString UPersonaWeapon::DeferredAction() const
{ return BufferedSequence ? TEXT("Fire") : bPumpQueued ? TEXT("Pump") : Action==EPersonaWeaponAction::Pump && bLoadHeld ? TEXT("Load") : TEXT("None"); }
void UPersonaWeapon::Acknowledge(int32 Sequence,bool Fired,uint8 SoundVariant)
{ if(auto P=Item()) { P->ForceNetUpdate(); ClientFireState(P,++StateRevision,Sequence,Fired,SoundVariant,LastFireReason); } }
void UPersonaWeapon::ServerFire_Implementation(FVector_NetQuantizeNormal Aim,int32 Sequence,uint8 SoundVariant,APersonaProp* ExpectedGun)
{
    if(Sequence<=LastServerSequence || ExpectedGun!=Item() || !CanAct()) { LastFireReason=TEXT("Stale request or unavailable weapon"); Acknowledge(Sequence); return; }
    LastServerSequence=Sequence;
    if(Action==EPersonaWeaponAction::Pump)
    {
        if(ActionEnd-Time(GetWorld())<=.15f && BufferedSequence==0)
        { BufferedSequence=Sequence; BufferedAim=Aim; BufferedSound=SoundVariant; LastFireReason=TEXT("Queued at pump end"); }
        else LastFireReason=TEXT("Pump busy");
        Acknowledge(Sequence); return;
    }
    ExecuteFire(Aim,Sequence,SoundVariant);
}
void UPersonaWeapon::ExecuteFire(FVector Aim,int32 Sequence,uint8 SoundVariant)
{
    if(!CanAct()) return;
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item();
    if(Time(GetWorld())<NextShot) { LastFireReason=TEXT("Shot cooldown"); Acknowledge(Sequence); return; }
    if(C->bSprinting) { C->SetSprintIntent(false); LastFireReason=TEXT("Lowering sprint weapon"); Acknowledge(Sequence); return; }
    if(P->WeaponState.Chamber!=EPersonaChamber::Live || P->WeaponState.Safety)
    {
        LastFireReason=P->WeaponState.Safety ? TEXT("Safety") : P->WeaponState.Chamber==EPersonaChamber::Spent ? TEXT("Pump required") : TEXT("Empty chamber");
        Acknowledge(Sequence); MechanicalFX(TEXT("Click")); NextShot=Time(GetWorld())+.2f; return;
    }
    if(!Aim.IsNormalized() || FVector::DotProduct(Aim,C->GetBaseAimRotation().Vector())<.97f) { LastFireReason=TEXT("Invalid aim"); Acknowledge(Sequence); return; }
    const bool AimHeld=bAiming; Cancel(); bAiming=AimHeld;
    CastChecked<UPersonaMovement>(C->GetCharacterMovement())->bWantsAim=AimHeld;
    NextShot=Time(GetWorld())+.3f; P->WeaponState.Chamber=EPersonaChamber::Spent; P->ForceNetUpdate();
    LastFireReason=TEXT("Fired"); ++ShotsFired; LastPelletHits.Reset();
    const FVector Eyes=C->GetPawnViewLocation();
    // Validate the camera-space barrel, including steep aim angles. The third
    // person carry pose clamps pitch and retracts near walls; using that pose
    // here could approve a shot whose visible barrel is already obstructed.
    const FTransform Carry=BallisticTransform()*FTransform(C->GetBaseAimRotation(),Eyes);
    const FVector Muzzle=Carry.TransformPosition(P->Definition->Muzzle);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(Shotgun),true,C); Q.AddIgnoredActor(P); Q.bReturnPhysicalMaterial=true;
    FHitResult Obstruction; const bool Blocked=GetWorld()->LineTraceSingleByChannel(Obstruction,Eyes,Muzzle,ECC_GameTraceChannel1,Q);
    if(SoundVariant<1 || SoundVariant>4 || SoundVariant==LastServerSoundVariant) SoundVariant=FireSoundVariant(Sequence,LastServerSoundVariant);
    LastServerSoundVariant=SoundVariant; Acknowledge(Sequence,true,SoundVariant);
    ShotFX(Sequence,SoundVariant,Blocked ? Obstruction.ImpactPoint : Muzzle);
    UAISense_Hearing::ReportNoiseEvent(this,C->GetActorLocation(),1.f,C,10000,TEXT("Gunshot"));
    TMap<APersonaCharacter*,float> Damage;
    struct FRegionHits { float Damage[6]={}; FHitResult Hits[6]; };
    TMap<APersonaCharacter*,FRegionHits> Regions; TOptional<FHitResult> Wall;
    TMap<APersonaCharacter*,FVector> NearMisses;
    FRandomStream Random(Sequence*73856093+GetOwner()->GetUniqueID());
    const FRotationMatrix Basis(Aim.Rotation()); const FVector Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
    const float Rotation=Random.FRand()*2*PI, Spread=FMath::Tan(FMath::DegreesToRadians(P->Definition->SpreadDegrees));
    for(int32 I=0; I<P->Definition->Pellets; ++I)
    {
        const float Radius=FMath::Sqrt((I+.5f+Random.FRandRange(-.15f,.15f))/P->Definition->Pellets)*Spread;
        const float Angle=Rotation+I*2.39996323f+Random.FRandRange(-.04f,.04f);
        FHitResult Hit; const FVector Direction=(Aim+Radius*(Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle))).GetSafeNormal();
        FHitResult Sight; GetWorld()->LineTraceSingleByChannel(Sight,Eyes,Eyes+Direction*P->Definition->Range,ECC_GameTraceChannel1,Q);
        if(Blocked) Hit=Obstruction;
        else GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,Sight.bBlockingHit ? Sight.ImpactPoint+Direction*3 : Eyes+Direction*P->Definition->Range,ECC_GameTraceChannel1,Q);
        if(!Blocked)
        {
            const FVector End=Hit.bBlockingHit ? Hit.ImpactPoint : Eyes+Direction*P->Definition->Range;
            for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
                if(auto Listener=Cast<APersonaCharacter>(It->Get()->GetPawn()); Listener && Listener!=C && Listener->IsAlive() && Listener!=Hit.GetActor())
                {
                    const FVector Ear=Listener->GetPawnViewLocation();
                    const FVector Closest=FMath::ClosestPointOnSegment(Ear,Muzzle,End);
                    if(FVector::DistSquared(Ear,Closest)<FMath::Square(110.f) && FVector::DistSquared(Muzzle,Closest)>FMath::Square(180.f))
                    {
                        FHitResult Occlusion; FCollisionQueryParams ListenQuery(SCENE_QUERY_STAT(NearMiss),false,Listener); ListenQuery.AddIgnoredActor(C);
                        if(!GetWorld()->LineTraceSingleByChannel(Occlusion,Closest,Ear,ECC_Visibility,ListenQuery)) NearMisses.FindOrAdd(Listener)=Closest;
                    }
                }
        }
        if(Hit.BoneName.IsNone() && Hit.GetComponent() && Hit.GetComponent()->GetAttachParent()==(Cast<APersonaCharacter>(Hit.GetActor()) ? Cast<APersonaCharacter>(Hit.GetActor())->GetMesh() : nullptr))
            Hit.BoneName=Hit.GetComponent()->GetAttachSocketName();
        if(!Hit.bBlockingHit) { Hit.TraceStart=Muzzle; Hit.TraceEnd=Eyes+Direction*P->Definition->Range; }
        LastPelletHits.Add(Hit);
        if(!Hit.bBlockingHit) continue;
        if(auto Victim=Cast<APersonaCharacter>(Hit.GetActor()))
        {
            const FString Bone=Hit.BoneName.ToString(); float Multiplier=Bone.Contains(TEXT("head")) ? P->Definition->HeadDamageScale : (Bone.Contains(TEXT("arm")) || Bone.Contains(TEXT("hand")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("foot")) ? P->Definition->LimbDamageScale : 1.f);
            float Falloff=FMath::GetMappedRangeValueClamped(FVector2D(P->Definition->FalloffStart,P->Definition->FalloffEnd),FVector2D(1,P->Definition->MinimumDamageScale),FVector::Distance(Eyes,Hit.ImpactPoint));
            const float Amount=P->Definition->PelletDamage*Multiplier*Falloff;
            Damage.FindOrAdd(Victim)+=Amount;
            auto& Region=Regions.FindOrAdd(Victim); const int32 Index=static_cast<uint8>(PersonaBodyRegion(Hit.BoneName));
            Region.Damage[Index]+=Amount; Region.Hits[Index]=Hit;
        }
        else if(!Wall.IsSet()) Wall=Hit;
    }
    for(auto Pair:Damage)
    {
        const auto& Region=Regions[Pair.Key]; int32 Dominant=0;
        for(int32 I=1;I<6;++I) if(Region.Damage[I]>Region.Damage[Dominant]) Dominant=I;
        const auto& Hit=Region.Hits[Dominant]; UGameplayStatics::ApplyPointDamage(Pair.Key,Pair.Value,Aim,Hit,C->GetController(),P,UDamageType::StaticClass());
        ImpactFX(Hit.ImpactPoint,Hit.ImpactNormal,true);
    }
    if(Wall.IsSet()) ImpactFX(Wall->ImpactPoint,Wall->ImpactNormal,false,Wall->PhysMaterial.IsValid() && Wall->PhysMaterial->SurfaceType==SurfaceType1);
    for(const auto& Miss:NearMisses) if(!Damage.Contains(Miss.Key)) Miss.Key->Weapon->ClientNearMiss(Miss.Value);
}
void UPersonaWeapon::ClientFireState_Implementation(APersonaProp* Gun,uint32 Revision,int32 Sequence,bool Fired,uint8 SoundVariant,const FString& Reason)
{
    if(Revision<=AppliedStateRevision || Gun!=Item()) return;
    AppliedStateRevision=Revision; LastFireReason=Reason;
    // Chamber is replicated by the gun only: acknowledgements never overwrite
    // a newer replicated chamber with an old predicted snapshot.
    if(Fired && Sequence>LastPresentedSequence && !GetOwner()->HasAuthority()) LocalShot(Sequence,SoundVariant);
}
void UPersonaWeapon::ShotFX_Implementation(int32 Sequence,uint8 SoundVariant,FVector_NetQuantize Muzzle)
{
    auto C=CastChecked<APersonaCharacter>(GetOwner()); if(C->IsLocallyControlled()) { if(Sequence>LastPresentedSequence) LocalShot(Sequence,SoundVariant); return; }
    APersonaFX::Play(GetWorld(),TEXT("Muzzle"),Muzzle,C->GetBaseAimRotation()); LastSoundVariant=SoundVariant; Sound(this,FName(FString::Printf(TEXT("ShotgunFire%d"),LastSoundVariant)),Muzzle);
}
void UPersonaWeapon::ImpactFX_Implementation(FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal,bool Human,bool Soft)
{ if(Human) ++BodyImpactCount; else ++WallImpactCount; APersonaFX::Play(GetWorld(),Human ? TEXT("Human") : TEXT("Wall"),Point+Normal*.3f,FRotationMatrix::MakeFromZ(Normal).Rotator()); Sound(this,Human ? TEXT("HitBody") : Soft ? TEXT("HitSoft") : TEXT("HitWall"),Point); }
void UPersonaWeapon::ClientNearMiss_Implementation(FVector_NetQuantize Point)
{ ++NearMissCount; Sound(this,TEXT("BulletWhiz"),Point); }
void UPersonaWeapon::MechanicalFX_Implementation(FName Name)
{ auto C=CastChecked<APersonaCharacter>(GetOwner()); Sound(this,Name,C->GetActorLocation(),C->IsLocallyControlled()); }
void UPersonaWeapon::EjectShell_Implementation()
{
    if(GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item(); if(!P) return;
    const FTransform Frame=C->IsLocallyControlled() ? P->OwnerViewMesh->GetComponentTransform() : C->GetCarryTransform();
    const FVector At=Frame.TransformPosition(FVector(1,4,1));
    auto Shell=GetWorld()->SpawnActor<APersonaFX>(At,Frame.Rotator());
    Shell->Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    Shell->Mesh->SetWorldScale3D(FVector(.02,.02,.055));
    Shell->Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/Materials/M_Charcoal.M_Charcoal")));
    Shell->Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Shell->Mesh->SetCollisionObjectType(ECC_PhysicsBody); Shell->Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Shell->Mesh->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
    Shell->Mesh->SetSimulatePhysics(true);
    Shell->Mesh->SetPhysicsLinearVelocity(Frame.TransformVectorNoScale(FVector(30,160,90)));
    Shell->Mesh->SetPhysicsAngularVelocityInDegrees(FVector(250,400,180)); Shell->SetLifeSpan(3);
    Sound(this,TEXT("ShellDrop"),At,C->IsLocallyControlled());
}
FVector UPersonaWeapon::Grip(bool Left,FVector Base) const
{
    float T=Time(GetWorld())-ActionStart;
    const auto P=Item();
    if(Left && Action==EPersonaWeaponAction::Pump) Base.X-=FMath::Sin(FMath::Clamp(T/(P ? P->Definition->PumpDuration : .45f),0.f,1.f)*PI)*9;
    if(Left && (Action==EPersonaWeaponAction::Load || Action==EPersonaWeaponAction::Unload)) Base=FMath::Lerp(Base,FVector(-14,-2,-12),FMath::Sin(FMath::Clamp(T/(P ? P->Definition->LoadDuration : .6f),0.f,1.f)*PI));
    return Base;
}
FTransform UPersonaWeapon::ViewTransform(FTransform Base) const
{
    auto P=Item(); if(!P) return Base;
    FVector L=FMath::Lerp(Base.GetLocation(),P->Definition->SightAlignment.GetLocation(),AimAlpha);
    FRotator R=FQuat::Slerp(Base.GetRotation(),P->Definition->SightAlignment.GetRotation(),AimAlpha).Rotator();
    L+=P->Definition->RecoilTranslation*RecoilValue(P->Definition->TranslationRecoilCurve);
    R+=P->Definition->RecoilRotation*RecoilValue(P->Definition->RotationRecoilCurve);
    if(auto C=Cast<APersonaCharacter>(GetOwner()))
    {
        R+=C->ViewSway*(1-AimAlpha);
        if(C->bSprinting) { R.Pitch-=12; R.Yaw-=8; L.Z-=8; }
    }
    if(Action==EPersonaWeaponAction::Load || Action==EPersonaWeaponAction::Unload) { R.Roll-=12; L.Z-=3; }
    if(Action==EPersonaWeaponAction::Inspect) { R.Yaw-=14; R.Roll-=20; }
    return FTransform(R,L);
}
FTransform UPersonaWeapon::BallisticTransform() const
{
    auto P=Item(); if(!P) return FTransform::Identity;
    return bAiming ? P->Definition->SightAlignment : FTransform(P->Definition->CarryRotation,P->Definition->CarryOffset);
}
FString UPersonaWeapon::Inspection() const
{
    auto P=Item(); if(!P) return FString(); auto S=P->WeaponState;
    return FString::Printf(TEXT("Трубка: %d/6   Патронник: %s   Предохранитель: %s"),S.Tube,S.Chamber==EPersonaChamber::Live ? TEXT("патрон") : S.Chamber==EPersonaChamber::Spent ? TEXT("гильза") : TEXT("пусто"),S.Safety ? TEXT("вкл.") : TEXT("выкл."));
}
