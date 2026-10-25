#include "PersonaInteraction.h"
#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "PersonaLoot.h"
#include "PersonaWeapon.h"
#include "PersonaHub.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Components/StaticMeshComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

UPersonaInteraction::UPersonaInteraction() { SetIsReplicatedByDefault(true); Slots.SetNum(3); }
void UPersonaInteraction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UPersonaInteraction, Held); DOREPLIFETIME(UPersonaInteraction, Slots); DOREPLIFETIME(UPersonaInteraction, SelectedSlot);
}
AActor* UPersonaInteraction::TraceTarget() const
{
    auto C = Cast<APersonaCharacter>(GetOwner()); if (!C) return nullptr;
    FVector Start; FRotator Rotation; C->GetActorEyesViewPoint(Start, Rotation);
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PersonaInteraction), false, C);
    for (auto Item : Slots) if (Item) Query.AddIgnoredActor(Item);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Rotation.Vector() * Reach, ECC_Visibility, Query);
    return Hit.GetActor();
}
void UPersonaInteraction::TryInteract() { if (LootTarget) CloseLoot(); else ServerInteract(TraceTarget()); }
void UPersonaInteraction::ServerInteract_Implementation(AActor* Target)
{
    auto C = Cast<APersonaCharacter>(GetOwner());
    if (!C || !C->IsAlive() || C->bTransit || !IsValid(Target) || Target != TraceTarget() || !Target->Implements<UPersonaInteractable>()) return;
    IPersonaInteractable::Execute_Interact(Target, C);
}
void UPersonaInteraction::ServerRelease_Implementation(bool Throw)
{
    auto C = Cast<APersonaCharacter>(GetOwner()); if (C && C->IsAlive() && !C->bTransit) ReleaseHeld(Throw);
}
void UPersonaInteraction::ReleaseHeld(bool Throw) { if (GetOwner()->HasAuthority() && Held) Held->Release(Throw); }
void UPersonaInteraction::ReleaseAll() { const auto Items = Slots; for (auto Item : Items) if (Item) Item->Release(false); }
void UPersonaInteraction::ClientFeedback_Implementation(const FString& Message) { Feedback = Message; FeedbackUntil = GetWorld()->GetTimeSeconds()+2.5; }
void UPersonaInteraction::ServerSelectSlot_Implementation(int32 Slot)
{
    auto C = Cast<APersonaCharacter>(GetOwner());
    if (!C || !C->IsAlive() || !Slots.IsValidIndex(Slot)) return;
    ServerChargeStart=-1; ServerChargedItem.Reset(); C->Weapon->Cancel(); SelectedSlot = Slot; RefreshEquipped();
}
void UPersonaInteraction::RefreshEquipped()
{
    Held = Slots.IsValidIndex(SelectedSlot) ? Slots[SelectedSlot] : nullptr;
    for (auto Item : Slots) if (Item) { Item->bStowed = Item != Held; Item->OnRep_Holder(); Item->ForceNetUpdate(); }
    GetOwner()->ForceNetUpdate();
}
void UPersonaInteraction::Remove(APersonaProp* Item)
{
    for (auto& Slot : Slots) if (Slot == Item) Slot = nullptr;
    if (Held == Item) Held = nullptr;
    GetOwner()->ForceNetUpdate();
}
bool UPersonaInteraction::Store(APersonaProp* Item)
{
    auto C = Cast<APersonaCharacter>(GetOwner());
    if (!C || !C->HasAuthority() || !C->IsAlive() || !IsValid(Item) || Item->Quantity <= 0) return false;
    if (Item->Holder && !Item->Holder->bOffline) return false;
    // Merge only identical definitions. No client-provided counts or stack limits.
    for (auto Slot : Slots)
        if (Slot && Item->Definition && Slot->Definition == Item->Definition && Slot->Quantity < Slot->StackLimit())
        {
            const int32 Count = FMath::Min(Item->Quantity, Slot->StackLimit()-Slot->Quantity);
            Slot->Quantity += Count; Item->Quantity -= Count; Slot->ForceNetUpdate(); Item->ForceNetUpdate();
            if (!Item->Quantity) { if (Item->Holder) Item->Holder->Interaction->Remove(Item); Item->Destroy(); return true; }
        }
    const int32 Empty = Slots.IndexOfByPredicate([](const auto& Slot) { return !IsValid(Slot); });
    if (Empty == INDEX_NONE) { ClientFeedback(TEXT("Хотбар заполнен")); return false; }
    if (Item->Holder) Item->Holder->Interaction->Remove(Item);
    Item->Holder = C; Item->SetOwner(C); Slots[Empty] = Item;
    if (!Held) SelectedSlot = Empty;
    RefreshEquipped(); return true;
}
APersonaProp::APersonaProp()
{
    bReplicates = true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetWorldScale3D(FVector(0.35f));
    Mesh->SetSimulatePhysics(true); Mesh->SetCollisionProfileName(TEXT("PersonaInteractable"));
    Mesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Ignore);
    // Actor movement replicates the rigid body; the replicated item definition
    // owns mesh selection. Replicating StaticMesh as well races its rep-notify
    // against OnRep_Item and can leave an inconsistent component on clients.
    Mesh->SetIsReplicated(false); Mesh->SetRenderCustomDepth(true); Mesh->SetCustomDepthStencilValue(2);
    OwnerViewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OwnerViewMesh"));
    OwnerViewMesh->SetupAttachment(Mesh); OwnerViewMesh->SetStaticMesh(Cube.Object);
    OwnerViewMesh->SetOnlyOwnerSee(true); OwnerViewMesh->SetVisibility(false);
    OwnerViewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); OwnerViewMesh->SetCastShadow(false);
    PumpVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PumpVisual")); PumpVisual->SetupAttachment(OwnerViewMesh);
    BoltVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoltVisual")); BoltVisual->SetupAttachment(OwnerViewMesh);
    WorldPump=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorldPump")); WorldPump->SetupAttachment(Mesh);
    WorldBolt=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorldBolt")); WorldBolt->SetupAttachment(Mesh);
    for (auto Part:{PumpVisual.Get(),BoltVisual.Get(),WorldPump.Get(),WorldBolt.Get()}) { Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetCastShadow(false); }
    PumpVisual->SetOnlyOwnerSee(true); BoltVisual->SetOnlyOwnerSee(true);
    PrimaryActorTick.TickGroup = TG_PostPhysics;
}
void APersonaProp::BeginPlay()
{
    Super::BeginPlay(); Mesh->SetIsReplicated(false); if (PersistentItemId.IsEmpty()) PersistentItemId = GetName();
    if (HasAuthority()) Quantity = FMath::Clamp(Quantity,1,StackLimit());
    OnRep_Item(); OnRep_Holder();
}
int32 APersonaProp::StackLimit() const { return Definition ? FMath::Max(1,Definition->MaxStack) : 1; }
FText APersonaProp::ItemName() const { return Definition ? Definition->DisplayName : FText::FromString(TEXT("Тестовый груз")); }
FVector APersonaProp::Grip(bool Left) const { FVector Base=Definition ? (Left ? Definition->LeftGrip : Definition->RightGrip) : (Left ? LeftGrip : RightGrip); return Holder && Definition && Definition->bShotgun ? Holder->Weapon->Grip(Left,Base) : Base; }
FTransform APersonaProp::CarryLocal() const
{
    FTransform Base=Definition ? FTransform(Definition->CarryRotation,Definition->CarryOffset) : FTransform(FVector(65,0,-23));
    return Holder && Definition && Definition->bShotgun ? Holder->Weapon->ViewTransform(Base) : Base;
}
void APersonaProp::OnRep_Item()
{
    if (Definition && Definition->Mesh)
    {
        Mesh->SetStaticMesh(Definition->Mesh); Mesh->SetWorldScale3D(FVector::OneVector);
        Mesh->EmptyOverrideMaterials();
    }
    OwnerViewMesh->SetStaticMesh(Mesh->GetStaticMesh());
    OwnerViewMesh->EmptyOverrideMaterials();
    for (int32 I=0; I<Mesh->GetNumMaterials(); ++I) OwnerViewMesh->SetMaterial(I,Mesh->GetMaterial(I));
    PumpVisual->SetStaticMesh(Definition ? Definition->PumpMesh : nullptr); WorldPump->SetStaticMesh(PumpVisual->GetStaticMesh());
    BoltVisual->SetStaticMesh(Definition ? Definition->BoltMesh : nullptr); WorldBolt->SetStaticMesh(BoltVisual->GetStaticMesh());
    OnRep_Holder();
}
void APersonaProp::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!Holder) return;
    // The visible item of an abandoned body remains available for looting.
    Mesh->SetCollisionEnabled(Holder->bOffline && !bStowed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (!bStowed)
    {
        const FTransform Carry = Holder->GetCarryTransform();
        SetActorLocationAndRotation(Carry.GetLocation(),Carry.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    }
    // Possession/replication may arrive after Holder. Reconcile the visual once
    // available, then let the camera hierarchy update it without world-space lag.
    const bool Local = Holder->IsLocallyControlled() && !bStowed;
    if (Local && OwnerViewMesh->GetAttachParent() != Holder->Camera) OnRep_Holder();
    else if (!Local && OwnerViewMesh->GetAttachParent() != Mesh) OnRep_Holder();
    if (Local) { auto View=CarryLocal(); View.SetScale3D(Mesh->GetComponentScale()); OwnerViewMesh->SetRelativeTransform(View); }
    const float Now=GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const float Pump=Holder->Weapon->Action==EPersonaWeaponAction::Pump ? FMath::Sin(FMath::Clamp((Now-Holder->Weapon->ActionStart)/.55f,0.f,1.f)*PI)*-9.f : 0;
    PumpVisual->SetRelativeLocation(FVector(Pump,0,0)); WorldPump->SetRelativeLocation(FVector(Pump,0,0));
    BoltVisual->SetRelativeLocation(FVector(Pump*.7f,0,0)); WorldBolt->SetRelativeLocation(FVector(Pump*.7f,0,0));
}
FText APersonaProp::InteractionLabel_Implementation() const
{
    return FText::FromString(Holder && !Holder->bOffline ? TEXT("Занято") : FString::Printf(TEXT("E — %s  ×%d"),*ItemName().ToString(),Quantity));
}
void APersonaProp::Interact_Implementation(APersonaCharacter* C) { if (HasAuthority() && C) C->Interaction->Store(this); }
void APersonaProp::OnRep_Holder()
{
    Mesh->SetSimulatePhysics(Holder == nullptr && !bInTransit);
    Mesh->SetCollisionEnabled(Holder ? (Holder->bOffline && !bStowed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision) : ECollisionEnabled::QueryAndPhysics);
    if (HasAuthority()) SetReplicateMovement(Holder == nullptr);
    Mesh->SetOwnerNoSee(Holder != nullptr); Mesh->SetVisibility(!bStowed);
    const bool Local = Holder && Holder->IsLocallyControlled() && !bStowed;
    OwnerViewMesh->AttachToComponent(Local ? static_cast<USceneComponent*>(Holder->Camera) : Mesh.Get(),FAttachmentTransformRules::KeepWorldTransform);
    FTransform View = Local ? CarryLocal() : FTransform::Identity;
    View.SetScale3D(Local ? Mesh->GetComponentScale() : FVector::OneVector);
    OwnerViewMesh->SetRelativeTransform(View);
    OwnerViewMesh->SetVisibility(Local);
    PumpVisual->SetVisibility(Local); BoltVisual->SetVisibility(Local);
    WorldPump->SetVisibility(!bStowed && !Local); WorldBolt->SetVisibility(!bStowed && !Local);
}
void APersonaProp::Release(bool Throw)
{
    if (!HasAuthority() || !Holder) return;
    APersonaCharacter* Previous = Holder; const FVector Direction = Previous->GetBaseAimRotation().Vector();
    const FTransform Carry = Previous->GetCarryTransform();
    Previous->Interaction->Remove(this);
    Holder = nullptr; bStowed = false; SetOwner(nullptr);
    SetActorLocationAndRotation(Carry.GetLocation(),Carry.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    OnRep_Holder();
    Mesh->SetPhysicsLinearVelocity(Previous->GetVelocity());
    if (Throw) Mesh->AddImpulse(Direction * ThrowImpulse,NAME_None,true);
    UAISense_Hearing::ReportNoiseEvent(this,GetActorLocation(),Throw ? 1.f : .3f,Previous,2000,TEXT("Prop"));
    ForceNetUpdate();
}
void APersonaProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaProp,Holder);
    DOREPLIFETIME(APersonaProp,Definition); DOREPLIFETIME(APersonaProp,Quantity); DOREPLIFETIME(APersonaProp,bStowed);
    DOREPLIFETIME(APersonaProp,WeaponState); DOREPLIFETIME(APersonaProp,bInTransit); DOREPLIFETIME(APersonaProp,PersistentItemId);
}

void UPersonaInteraction::BeginDrop()
{
    auto C=Cast<APersonaCharacter>(GetOwner()); if (!Held || LootTarget || !C || !C->IsAlive() || C->bTransit) return;
    bCharging=true; ChargeStart=GetWorld()->GetTimeSeconds(); ChargedItem=Held; ServerCharge(true);
}
float UPersonaInteraction::ThrowAlpha() const { return bCharging ? FMath::Clamp(float(GetWorld()->GetTimeSeconds()-ChargeStart-.2)/1.f,0.f,1.f) : 0.f; }
void UPersonaInteraction::EndDrop() { if (bCharging) ServerCharge(false); bCharging=false; ChargedItem.Reset(); }
void UPersonaInteraction::CancelActions()
{
    if (bCharging) ServerCharge(false,true); bCharging=false; ChargedItem.Reset();
    if (auto C=Cast<APersonaCharacter>(GetOwner())) C->Weapon->Cancel();
    if(GetOwner()->HasAuthority()) { ServerChargeStart=-1; ServerChargedItem.Reset(); }
}
void UPersonaInteraction::ServerCharge_Implementation(bool Begin,bool Cancel)
{
    auto C=Cast<APersonaCharacter>(GetOwner());
    if (Begin) { if (C && C->IsAlive() && !C->bTransit && Held) { ServerChargeStart=GetWorld()->GetTimeSeconds(); ServerChargedItem=Held; C->Weapon->Cancel(); } return; }
    if (!Cancel && C && C->IsAlive() && !C->bTransit && Held && ServerChargedItem==Held && ServerChargeStart>=0)
    {
        const float Duration=GetWorld()->GetTimeSeconds()-ServerChargeStart;
        Held->ThrowImpulse=FMath::Lerp(200.f,1000.f,FMath::Clamp((Duration-.2f),0.f,1.f));
        ReleaseHeld(Duration>=.2f);
    }
    ServerChargeStart=-1; ServerChargedItem.Reset();
}
void UPersonaInteraction::ClientOpenLoot_Implementation(AActor* Target)
{
    CancelActions(); LootTarget=Target;
    if (auto C=Cast<APersonaCharacter>(GetOwner())) if (auto PC=Cast<APlayerController>(C->Controller))
    { PC->SetIgnoreLookInput(true); PC->bShowMouseCursor=true; PC->SetInputMode(FInputModeGameAndUI()); }
}
void UPersonaInteraction::CloseLoot()
{
    if(LootTarget) ServerCloseLoot(LootTarget);
    LootTarget=nullptr;
    if (auto C=Cast<APersonaCharacter>(GetOwner())) if (auto PC=Cast<APlayerController>(C->Controller))
    { PC->ResetIgnoreMoveInput(); PC->ResetIgnoreLookInput(); PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); }
}
void UPersonaInteraction::ServerCloseLoot_Implementation(AActor* Target)
{
    if(auto H=Cast<APersonaHubObject>(Target); H && CanReachLoot(H)) { H->bOpen=false; H->ForceNetUpdate(); }
}
bool UPersonaInteraction::CanReachLoot(AActor* Target) const
{
    auto C=Cast<APersonaCharacter>(GetOwner()); if(!C || !C->IsAlive() || C->bTransit || !IsValid(Target)) return false;
    const FVector Eye=C->GetPawnViewLocation();
    const FBox Bounds=Target->GetComponentsBoundingBox(true);
    const FVector Closest=Bounds.IsValid ? Bounds.GetClosestPointTo(Eye) : Target->GetActorLocation();
    if(FVector::DistSquared(Eye,Closest)>FMath::Square(Reach+100)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(LootReach),false,C); if (Held) Q.AddIgnoredActor(Held);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Closest,ECC_Visibility,Q) || Hit.GetActor()==Target;
}
void UPersonaInteraction::ServerLoot_Implementation(AActor* Target,int32 Index,bool Deposit,const FString& ExpectedId)
{
    auto C=Cast<APersonaCharacter>(GetOwner()); if(!C || !CanReachLoot(Target) || ExpectedId.IsEmpty()) return;
    auto L=Target->FindComponentByClass<UPersonaLoot>(); if (!L) return;
    if (auto Body=Cast<APersonaCharacter>(Target); Body && !Body->bDead) return;
    if(auto H=Cast<APersonaHubObject>(Target); H && (!H->Definition || (H->Definition->Kind!=EPersonaHubKind::Storage && H->Definition->Kind!=EPersonaHubKind::Sink && H->PersistentId!=TEXT("MigrationStorage")))) return;
    if (Deposit)
    { if(Slots.IsValidIndex(Index) && Slots[Index] && Slots[Index]->PersistentItemId==ExpectedId) { CancelActions(); L->Deposit(C,Index); } }
    else { Index=L->Items.IndexOfByPredicate([&](const auto& R){return R.Id==ExpectedId;}); if(Index!=INDEX_NONE) L->Take(C,Index); }
}
APersonaDoor::APersonaDoor()
{
    bReplicates = true;
    Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge")); SetRootComponent(Hinge);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); Mesh->SetupAttachment(Hinge);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetRelativeLocation(FVector(0, 65, 120)); Mesh->SetRelativeScale3D(FVector(.18f, 1.3f, 2.4f));
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
}
FText APersonaDoor::InteractionLabel_Implementation() const { return FText::FromString(bOpen ? TEXT("E - close") : TEXT("E - open")); }
void APersonaDoor::Interact_Implementation(APersonaCharacter* C) { if (HasAuthority()) { bOpen = !bOpen; OnRep_Open(); ForceNetUpdate(); } }
void APersonaDoor::OnRep_Open()
{
    const FRotator Rotation(0, bOpen ? -95 : 0, 0);
    Mesh->SetRelativeRotation(Rotation);
    Mesh->SetRelativeLocation(Rotation.RotateVector(FVector(0,65,120)));
}
void APersonaDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaDoor, bOpen); }
APersonaTerminal::APersonaTerminal()
{
    bReplicates = true; Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetWorldScale3D(FVector(.5f, .7f, 1.4f));
}
FText APersonaTerminal::InteractionLabel_Implementation() const { return FText::FromString(bGoToTrial ? TEXT("E - enter test chamber (host)") : TEXT("E - return to hub (host)")); }
void APersonaTerminal::Interact_Implementation(APersonaCharacter* C)
{
    if (auto PC = C ? Cast<APersonaPlayerController>(C->GetController()) : nullptr; PC && PC->IsHost())
        if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->Travel(bGoToTrial);
}
