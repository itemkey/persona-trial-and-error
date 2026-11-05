#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PersonaCatalog.generated.h"
class UPersonaItemDefinition;

USTRUCT(BlueprintType)
struct FPersonaTrialFind
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UPersonaItemDefinition> Item;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Source;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool FromNPC=false;
};

UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaItemCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UPersonaItemDefinition>> Items;
    static UPersonaItemCatalog* Get();
    UPersonaItemDefinition* Find(const FString& Path) const;
    static FString SearchKey(FString Text);
    static FString Properties(const UPersonaItemDefinition& Item);
};
