#include "PersonaLoot.h"
#include "PersonaInteraction.h"
#include "PersonaCharacter.h"
#include "PersonaHub.h"
#include "Net/UnrealNetwork.h"

UPersonaLoot::UPersonaLoot() { SetIsReplicatedByDefault(true); }
void UPersonaLoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPersonaLoot,Items); DOREPLIFETIME(UPersonaLoot,Compartments); }
FPersonaInventoryRecord UPersonaLoot::Capture(APersonaProp* Item)
{
    FPersonaInventoryRecord R;
    if (Item) { R.ActorClass=Item->GetClass()->GetPathName(); R.Definition=Item->Definition ? Item->Definition->GetPathName() : FString(); R.Id=Item->PersistentItemId; R.Quantity=Item->Quantity; R.Weapon=Item->WeaponState; }
    return R;
}
APersonaProp* UPersonaLoot::Spawn(UWorld* World,const FPersonaInventoryRecord& R,const FTransform& Transform)
{
    UClass* Class=R.ActorClass.IsEmpty() ? APersonaProp::StaticClass() : LoadClass<APersonaProp>(nullptr,*R.ActorClass);
    if (!Class) return nullptr;
    auto P=World->SpawnActorDeferred<APersonaProp>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!P) return nullptr;
    P->Definition=R.Definition.IsEmpty() ? nullptr : LoadObject<UPersonaItemDefinition>(nullptr,*R.Definition);
    P->Quantity=FMath::Clamp(R.Quantity,1,P->StackLimit()); P->WeaponState=R.Weapon;
    P->PersistentItemId=R.Id.IsEmpty() ? FGuid::NewGuid().ToString() : R.Id;
    P->FinishSpawning(Transform); return P;
}
bool UPersonaLoot::Take(APersonaCharacter* C,int32 Index,FName Part)
{
    auto Content=Contents(Part); if(!Content) return false; auto& Entries=*Content;
    if (!GetOwner()->HasAuthority() || !C || !C->IsAlive() || !Entries.IsValidIndex(Index)) return false;
    // Create a temporary authoritative item. Store may partially merge it.
    auto P=Spawn(GetWorld(),Entries[Index],C->GetActorTransform()); if (!P) return false;
    const int32 Before=P->Quantity;
    C->Interaction->Store(P);
    if (!IsValid(P) || P->Holder==C) Entries.RemoveAt(Index);
    else { Entries[Index].Quantity=P->Quantity; P->Destroy(); }
    GetOwner()->ForceNetUpdate(); return Before>0;
}
bool UPersonaLoot::Deposit(APersonaCharacter* C,int32 Slot,FName Part)
{
    auto Content=Contents(Part); if(!Content) return false; auto& Entries=*Content;
    const int32 Limit=!HasSharedInventory() && FindPart(Part) ? FindPart(Part)->Capacity : Capacity;
    if (!GetOwner()->HasAuthority() || !bDeposit || !C || !C->IsAlive() || !C->Interaction->Slots.IsValidIndex(Slot)) return false;
    auto P=C->Interaction->Slots[Slot]; if (!P) return false;
    for (auto& R:Entries) if (P->Definition && R.Definition==P->Definition->GetPathName() && P->StackLimit()>1)
    { const int32 N=FMath::Min(P->Quantity,P->StackLimit()-R.Quantity); R.Quantity+=N; P->Quantity-=N; }
    if (P->Quantity>0 && Entries.Num()<Limit) { Entries.Add(Capture(P)); P->Quantity=0; }
    if (P->Quantity<=0) { C->Interaction->Remove(P); P->Destroy(); } else P->ForceNetUpdate();
    GetOwner()->ForceNetUpdate(); return true;
}
void UPersonaLoot::Add(const FString& Definition,int32 Count,FName Part)
{
    if(Part.IsNone() && !Compartments.IsEmpty() && !HasSharedInventory())
    {
        // Seed supplies evenly without changing the total amount.
        const int32 N=Compartments.Num();
        for(int32 I=0;I<N;++I) if(Count/N+(I<Count%N ? 1 : 0)>0) Add(Definition,Count/N+(I<Count%N ? 1 : 0),Compartments[I].Id);
        return;
    }
    auto Content=Contents(Part); if(!Content) return; auto& Entries=*Content;
    const int32 Limit=!HasSharedInventory() && FindPart(Part) ? FindPart(Part)->Capacity : Capacity;
    if (!GetOwner()->HasAuthority()) return;
    auto D=LoadObject<UPersonaItemDefinition>(nullptr,*Definition); if (!D) return;
    for (auto& R:Entries) if (R.Definition==Definition) { int32 N=FMath::Min(Count,D->MaxStack-R.Quantity); R.Quantity+=N; Count-=N; }
    while (Count>0 && Entries.Num()<Limit) { FPersonaInventoryRecord R; R.ActorClass=APersonaProp::StaticClass()->GetPathName(); R.Definition=Definition; R.Id=FGuid::NewGuid().ToString(); R.Quantity=FMath::Min(Count,D->MaxStack); Count-=R.Quantity; Entries.Add(R); }
    GetOwner()->ForceNetUpdate();
}

FPersonaCompartment* UPersonaLoot::FindPart(FName Part)
{ return Compartments.FindByPredicate([&](const auto& Entry){return Entry.Id==Part;}); }
bool UPersonaLoot::HasSharedInventory() const
{ auto Hub=Cast<APersonaHubObject>(GetOwner()); return Hub && Hub->Definition && Hub->Definition->bSharedInventory; }
TArray<FPersonaInventoryRecord>* UPersonaLoot::Contents(FName Part)
{
    if(HasSharedInventory()) return Part.IsNone() || FindPart(Part) ? &Items : nullptr;
    if(auto Entry=FindPart(Part)) return &Entry->Items;
    return Part.IsNone() && Compartments.IsEmpty() ? &Items : nullptr;
}
const TArray<FPersonaInventoryRecord>* UPersonaLoot::Contents(FName Part) const
{ return const_cast<UPersonaLoot*>(this)->Contents(Part); }
void UPersonaLoot::Clear()
{ Items.Reset(); for(auto& Part:Compartments) Part.Items.Reset(); GetOwner()->ForceNetUpdate(); }
TArray<FPersonaInventoryRecord> UPersonaLoot::AllItems() const
{ auto Result=Items; for(const auto& Part:Compartments) Result.Append(Part.Items); return Result; }
