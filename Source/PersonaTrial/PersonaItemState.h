#pragma once
#include "CoreMinimal.h"
#include "PersonaItemState.generated.h"

UENUM(BlueprintType)
enum class EPersonaChamber : uint8 { Empty, Live, Spent };
UENUM(BlueprintType)
enum class EPersonaWeaponAction : uint8 { Idle, Load, Pump, Unload, Inspect };
USTRUCT(BlueprintType)
struct FPersonaWeaponState
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Tube = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EPersonaChamber Chamber = EPersonaChamber::Empty;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool Safety = false;
};
USTRUCT(BlueprintType)
struct FPersonaInventoryRecord
{
    GENERATED_BODY()
    UPROPERTY() FString ActorClass;
    UPROPERTY() FString Definition;
    UPROPERTY() FString Id;
    UPROPERTY(BlueprintReadOnly) int32 Quantity = 1;
    UPROPERTY() FPersonaWeaponState Weapon;
    // Nested portable containers use a tagged JSON payload to avoid recursive
    // reflected structs; ordinary legacy items keep an empty payload.
    UPROPERTY() FString ContainerState;
};

USTRUCT(BlueprintType)
struct FPersonaCompartment
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() FString Label;
    UPROPERTY() bool Open = false;
    UPROPERTY() int32 Capacity = 12;
    UPROPERTY() TArray<FPersonaInventoryRecord> Items;
};
USTRUCT()
struct FPersonaContainerState
{
    GENERATED_BODY()
    UPROPERTY() TArray<FPersonaInventoryRecord> Items;
    UPROPERTY() TArray<FPersonaCompartment> Compartments;
    UPROPERTY() int32 Capacity=12;
};
