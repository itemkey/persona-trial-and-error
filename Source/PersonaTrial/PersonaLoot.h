#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PersonaItemState.h"
#include "PersonaLoot.generated.h"
class APersonaProp;
class APersonaCharacter;

UCLASS(ClassGroup=(Persona), meta=(BlueprintSpawnableComponent))
class PERSONATRIAL_API UPersonaLoot : public UActorComponent
{
    GENERATED_BODY()
public:
    UPersonaLoot();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<FPersonaInventoryRecord> Items;
    UPROPERTY(Replicated) TArray<FPersonaCompartment> Compartments;
    UPROPERTY(EditAnywhere) int32 Capacity = 12;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDeposit = true;
    bool Take(APersonaCharacter* User, int32 Index, FName Part = NAME_None);
    bool Deposit(APersonaCharacter* User, int32 Slot, FName Part = NAME_None);
    void Add(const FString& Definition, int32 Count, FName Part = NAME_None);
    TArray<FPersonaInventoryRecord>* Contents(FName Part);
    const TArray<FPersonaInventoryRecord>* Contents(FName Part) const;
    FPersonaCompartment* FindPart(FName Part);
    void Clear();
    bool HasSharedInventory() const;
    TArray<FPersonaInventoryRecord> AllItems() const;
    static FPersonaInventoryRecord Capture(APersonaProp* Item);
    static void RestoreContainer(APersonaProp* Item,const FString& State);
    static APersonaProp* Spawn(UWorld* World, const FPersonaInventoryRecord& Record, const FTransform& Transform);
};
