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
    UPROPERTY(EditAnywhere) int32 Capacity = 12;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDeposit = true;
    bool Take(APersonaCharacter* User, int32 Index);
    bool Deposit(APersonaCharacter* User, int32 Slot);
    void Add(const FString& Definition, int32 Count);
    static FPersonaInventoryRecord Capture(APersonaProp* Item);
    static APersonaProp* Spawn(UWorld* World, const FPersonaInventoryRecord& Record, const FTransform& Transform);
};
