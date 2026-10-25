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
};
