#include "PersonaWeapon.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaLoot.h"
#include "PersonaAttributes.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AISense_Hearing.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
float Time(UWorld* W) { auto GS=W->GetGameState(); return GS ? GS->GetServerWorldTimeSeconds() : W->GetTimeSeconds(); }
bool Ammo(APersonaProp* P) { return P && P->Definition && P->Definition->GetName()==TEXT("DA_Ammo12GA"); }
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
void UPersonaWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPersonaWeapon,Action); DOREPLIFETIME(UPersonaWeapon,ActionStart); DOREPLIFETIME(UPersonaWeapon,bAiming); }
APersonaProp* UPersonaWeapon::Item() const
{ auto C=Cast<APersonaCharacter>(GetOwner()); auto P=C ? C->Interaction->Held.Get() : nullptr; return P && P->Definition && P->Definition->bShotgun ? P : nullptr; }
bool UPersonaWeapon::CanAct() const { auto C=Cast<APersonaCharacter>(GetOwner()); return C && C->IsAlive() && !C->bTransit && !C->bSpectator && Item(); }
void UPersonaWeapon::SetAim(bool Value) { bAiming=Value && CanAct(); ServerAction(1,bAiming); }
void UPersonaWeapon::Load(bool Held) { ServerAction(2,Held); }
void UPersonaWeapon::Unload(bool Held) { ServerAction(3,Held); }
void UPersonaWeapon::Pump() { ServerAction(4); }
void UPersonaWeapon::Inspect() { InspectUntil=Time(GetWorld())+2.5f; ServerAction(5); }
void UPersonaWeapon::Safety() { ServerAction(6); }
void UPersonaWeapon::Cancel()
{
    bAiming=false; bLoadHeld=bUnloadHeld=false;
    if(GetOwner()->HasAuthority()) { Action=EPersonaWeaponAction::Idle; ActiveItem.Reset(); }
    else ServerAction(0);
}
void UPersonaWeapon::StartAction(EPersonaWeaponAction Value,float Duration)
{ Action=Value; ActionStart=Time(GetWorld()); ActionEnd=ActionStart+Duration; ActiveItem=Item(); GetOwner()->ForceNetUpdate(); }
void UPersonaWeapon::ServerAction_Implementation(uint8 Command,bool Held)
{
    if(Command==0) { Cancel(); return; }
    if(Command==1) { bAiming=Held && CanAct(); return; }
    if(!CanAct()) return;
    auto P=Item(); auto C=CastChecked<APersonaCharacter>(GetOwner());
    if(Command==2) { bLoadHeld=Held; if(!Held) return; }
    if(Command==3) { bUnloadHeld=Held; if(!Held) return; }
    if(Action!=EPersonaWeaponAction::Idle) return;
    if(Command==2 && P->WeaponState.Tube<6 && CountAmmo(C)>0) { StartAction(EPersonaWeaponAction::Load,.7f); MechanicalFX(TEXT("ShellLoad")); }
    if(Command==3 && (P->WeaponState.Tube>0 || P->WeaponState.Chamber!=EPersonaChamber::Empty)) StartAction(EPersonaWeaponAction::Unload,.65f);
    if(Command==4) { StartAction(EPersonaWeaponAction::Pump,.55f); MechanicalFX(TEXT("Pump")); }
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
    P->ForceNetUpdate();
    if(Previous==EPersonaWeaponAction::Load && bLoadHeld && S.Tube<6 && CountAmmo(C)>0) { StartAction(Previous,.7f); MechanicalFX(TEXT("ShellLoad")); }
    if(Previous==EPersonaWeaponAction::Unload && bUnloadHeld && S.Tube>0) StartAction(Previous,.65f);
}
void UPersonaWeapon::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick); auto C=Cast<APersonaCharacter>(GetOwner()); if(!C) return;
    if(GetOwner()->HasAuthority()) { if(Action!=EPersonaWeaponAction::Idle && (!CanAct() || Item()!=ActiveItem)) Cancel(); else if(Action!=EPersonaWeaponAction::Idle && Time(GetWorld())>=ActionEnd) FinishAction(); }
    AimAlpha=FMath::FInterpTo(AimAlpha,bAiming && CanAct() && !C->bSprinting ? 1.f : 0.f,Delta,12);
    Recoil=FMath::FInterpTo(Recoil,0.f,Delta,12);
    if(C->IsLocallyControlled()) C->Camera->SetFieldOfView(FMath::Lerp(90.f,72.f,AimAlpha));
}
void UPersonaWeapon::LocalShot()
{
    Recoil=1; auto C=CastChecked<APersonaCharacter>(GetOwner());
    C->AddControllerPitchInput(-1.3f); Sound(this,TEXT("ShotgunFire"),C->GetActorLocation(),true);
    if(auto P=Item()) APersonaFX::Play(GetWorld(),TEXT("Muzzle"),P->OwnerViewMesh->GetComponentTransform().TransformPosition(P->Definition->Muzzle),C->Camera->GetComponentRotation());
}
void UPersonaWeapon::Fire()
{
    if(!CanAct()) return;
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item();
    if(P->WeaponState.Chamber==EPersonaChamber::Live && !P->WeaponState.Safety && Action!=EPersonaWeaponAction::Pump && Time(GetWorld())>=NextShot)
    { if(!GetOwner()->HasAuthority()) { LocalShot(); P->WeaponState.Chamber=EPersonaChamber::Spent; NextShot=Time(GetWorld())+.3f; } }
    ServerFire(C->GetBaseAimRotation().Vector(),++LocalSequence);
}
void UPersonaWeapon::ServerFire_Implementation(FVector_NetQuantizeNormal Aim,int32 Sequence)
{
    if(Sequence<=LastServerSequence || !CanAct())
    {
        // An unchanged authoritative chamber is not replicated again. Explicitly
        // undo local prediction when a late request reaches a locked passenger.
        if(auto Gun=Item()) ClientFireState(Gun,Gun->WeaponState);
        return;
    }
    LastServerSequence=Sequence;
    auto C=CastChecked<APersonaCharacter>(GetOwner()); auto P=Item();
    if(Time(GetWorld())<NextShot || Action==EPersonaWeaponAction::Pump) { ClientFireState(P,P->WeaponState); return; }
    const bool AimHeld=bAiming; Cancel(); bAiming=AimHeld;
    if(P->WeaponState.Chamber!=EPersonaChamber::Live || P->WeaponState.Safety) { ClientFireState(P,P->WeaponState); MechanicalFX(TEXT("Click")); NextShot=Time(GetWorld())+.2f; return; }
    if(!Aim.IsNormalized() || FVector::DotProduct(Aim,C->GetBaseAimRotation().Vector())<.97f) { ClientFireState(P,P->WeaponState); return; }
    NextShot=Time(GetWorld())+.3f; P->WeaponState.Chamber=EPersonaChamber::Spent; P->ForceNetUpdate();
    const FVector Eyes=C->GetPawnViewLocation();
    // Validate the camera-space barrel, including steep aim angles. The third
    // person carry pose clamps pitch and retracts near walls; using that pose
    // here could approve a shot whose visible barrel is already obstructed.
    const FTransform Carry=P->CarryLocal()*FTransform(C->GetBaseAimRotation(),Eyes);
    const FVector Muzzle=Carry.TransformPosition(P->Definition->Muzzle);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(Shotgun),true,C); Q.AddIgnoredActor(P); Q.bReturnPhysicalMaterial=true;
    FHitResult Obstruction; const bool Blocked=GetWorld()->LineTraceSingleByChannel(Obstruction,Eyes,Muzzle,ECC_GameTraceChannel1,Q);
    ShotFX(Sequence,Blocked ? Obstruction.ImpactPoint : Muzzle);
    UAISense_Hearing::ReportNoiseEvent(this,C->GetActorLocation(),1.f,C,10000,TEXT("Gunshot"));
    TMap<APersonaCharacter*,float> Damage; TMap<APersonaCharacter*,FHitResult> Hits; TOptional<FHitResult> Wall;
    FRandomStream Random(Sequence*73856093+GetOwner()->GetUniqueID());
    for(int32 I=0; I<P->Definition->Pellets; ++I)
    {
        FHitResult Hit; const FVector Direction=Random.VRandCone(Aim,FMath::DegreesToRadians(P->Definition->SpreadDegrees));
        FHitResult Sight; GetWorld()->LineTraceSingleByChannel(Sight,Eyes,Eyes+Direction*P->Definition->Range,ECC_GameTraceChannel1,Q);
        if(Blocked) Hit=Obstruction;
        else GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,Sight.bBlockingHit ? Sight.ImpactPoint+Direction*3 : Eyes+Direction*P->Definition->Range,ECC_GameTraceChannel1,Q);
        if(!Hit.bBlockingHit) continue;
        if(auto Victim=Cast<APersonaCharacter>(Hit.GetActor()))
        {
            const FString Bone=Hit.BoneName.ToString(); float Multiplier=Bone.Contains(TEXT("head")) ? P->Definition->HeadDamageScale : (Bone.Contains(TEXT("arm")) || Bone.Contains(TEXT("hand")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("foot")) ? P->Definition->LimbDamageScale : 1.f);
            float Falloff=FMath::GetMappedRangeValueClamped(FVector2D(P->Definition->FalloffStart,P->Definition->FalloffEnd),FVector2D(1,P->Definition->MinimumDamageScale),FVector::Distance(Eyes,Hit.ImpactPoint));
            Damage.FindOrAdd(Victim)+=P->Definition->PelletDamage*Multiplier*Falloff; Hits.Add(Victim,Hit);
        }
        else if(!Wall.IsSet()) Wall=Hit;
    }
    for(auto Pair:Damage)
    {
        const auto& Hit=Hits[Pair.Key]; UGameplayStatics::ApplyPointDamage(Pair.Key,Pair.Value,Aim,Hit,C->GetController(),P,UDamageType::StaticClass());
        ImpactFX(Hit.ImpactPoint,Hit.ImpactNormal,true);
    }
    if(Wall.IsSet()) ImpactFX(Wall->ImpactPoint,Wall->ImpactNormal,false);
}
void UPersonaWeapon::ClientFireState_Implementation(APersonaProp* Gun,FPersonaWeaponState State)
{ if(Gun && Gun->Holder==GetOwner()) Gun->WeaponState=State; }
void UPersonaWeapon::ShotFX_Implementation(int32 Sequence,FVector_NetQuantize Muzzle)
{
    auto C=CastChecked<APersonaCharacter>(GetOwner()); if(C->IsLocallyControlled()) { if(GetOwner()->HasAuthority()) LocalShot(); return; }
    APersonaFX::Play(GetWorld(),TEXT("Muzzle"),Muzzle,C->GetBaseAimRotation()); Sound(this,TEXT("ShotgunFire"),Muzzle);
}
void UPersonaWeapon::ImpactFX_Implementation(FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal,bool Human)
{ APersonaFX::Play(GetWorld(),Human ? TEXT("Human") : TEXT("Wall"),Point+Normal*.3f,FRotationMatrix::MakeFromZ(Normal).Rotator()); Sound(this,Human ? TEXT("HitBody") : TEXT("HitWall"),Point); }
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
    if(Left && Action==EPersonaWeaponAction::Pump) Base.X-=FMath::Sin(FMath::Clamp(T/.55f,0.f,1.f)*PI)*9;
    if(Left && (Action==EPersonaWeaponAction::Load || Action==EPersonaWeaponAction::Unload)) Base=FMath::Lerp(Base,FVector(-14,-2,-12),FMath::Sin(FMath::Clamp(T/.7f,0.f,1.f)*PI));
    return Base;
}
FTransform UPersonaWeapon::ViewTransform(FTransform Base) const
{
    auto P=Item(); if(!P) return Base;
    FVector L=FMath::Lerp(Base.GetLocation(),P->Definition->AimOffset,AimAlpha); L.X-=Recoil*5; L.Z-=Recoil*1.5f;
    FRotator R=Base.Rotator(); R.Pitch+=Recoil*5;
    if(Action==EPersonaWeaponAction::Load || Action==EPersonaWeaponAction::Unload) { R.Roll-=12; L.Z-=3; }
    if(Action==EPersonaWeaponAction::Inspect) { R.Yaw-=14; R.Roll-=20; }
    return FTransform(R,L);
}
FString UPersonaWeapon::Inspection() const
{
    auto P=Item(); if(!P) return FString(); auto S=P->WeaponState;
    return FString::Printf(TEXT("Трубка: %d/6   Патронник: %s   Предохранитель: %s"),S.Tube,S.Chamber==EPersonaChamber::Live ? TEXT("патрон") : S.Chamber==EPersonaChamber::Spent ? TEXT("гильза") : TEXT("пусто"),S.Safety ? TEXT("вкл.") : TEXT("выкл."));
}
