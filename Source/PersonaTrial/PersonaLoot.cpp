#include "PersonaLoot.h"
#include "PersonaInteraction.h"
#include "PersonaCharacter.h"
#include "Net/UnrealNetwork.h"

UPersonaLoot::UPersonaLoot() { SetIsReplicatedByDefault(true); }
void UPersonaLoot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPersonaLoot,Items); }
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
bool UPersonaLoot::Take(APersonaCharacter* C,int32 Index)
{
    if (!GetOwner()->HasAuthority() || !C || !C->IsAlive() || !Items.IsValidIndex(Index)) return false;
    // Create a temporary authoritative item. Store may partially merge it.
    auto P=Spawn(GetWorld(),Items[Index],C->GetActorTransform()); if (!P) return false;
    const int32 Before=P->Quantity;
    C->Interaction->Store(P);
    if (!IsValid(P) || P->Holder==C) Items.RemoveAt(Index);
    else { Items[Index].Quantity=P->Quantity; P->Destroy(); }
    GetOwner()->ForceNetUpdate(); return Before>0;
}
bool UPersonaLoot::Deposit(APersonaCharacter* C,int32 Slot)
{
    if (!GetOwner()->HasAuthority() || !bDeposit || !C || !C->IsAlive() || !C->Interaction->Slots.IsValidIndex(Slot)) return false;
    auto P=C->Interaction->Slots[Slot]; if (!P) return false;
    for (auto& R:Items) if (P->Definition && R.Definition==P->Definition->GetPathName() && P->StackLimit()>1)
    { const int32 N=FMath::Min(P->Quantity,P->StackLimit()-R.Quantity); R.Quantity+=N; P->Quantity-=N; }
    if (P->Quantity>0 && Items.Num()<Capacity) { Items.Add(Capture(P)); P->Quantity=0; }
    if (P->Quantity<=0) { C->Interaction->Remove(P); P->Destroy(); } else P->ForceNetUpdate();
    GetOwner()->ForceNetUpdate(); return true;
}
void UPersonaLoot::Add(const FString& Definition,int32 Count)
{
    if (!GetOwner()->HasAuthority()) return;
    auto D=LoadObject<UPersonaItemDefinition>(nullptr,*Definition); if (!D) return;
    for (auto& R:Items) if (R.Definition==Definition) { int32 N=FMath::Min(Count,D->MaxStack-R.Quantity); R.Quantity+=N; Count-=N; }
    while (Count>0 && Items.Num()<Capacity) { FPersonaInventoryRecord R; R.ActorClass=APersonaProp::StaticClass()->GetPathName(); R.Definition=Definition; R.Id=FGuid::NewGuid().ToString(); R.Quantity=FMath::Min(Count,D->MaxStack); Count-=R.Quantity; Items.Add(R); }
    GetOwner()->ForceNetUpdate();
}
