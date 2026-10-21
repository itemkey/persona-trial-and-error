#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "PersonaAttributes.generated.h"

#define PERSONA_ATTRIBUTE(Name) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UPersonaAttributes, Name) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Name) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Name) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Name)

UCLASS()
class PERSONATRIAL_API UPersonaAttributes : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPersonaAttributes();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& Value) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Vitals") FGameplayAttributeData Health;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Stamina, Category="Vitals") FGameplayAttributeData Stamina;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Hunger, Category="Vitals") FGameplayAttributeData Hunger;
    PERSONA_ATTRIBUTE(Health)
    PERSONA_ATTRIBUTE(Stamina)
    PERSONA_ATTRIBUTE(Hunger)
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Stamina(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Hunger(const FGameplayAttributeData& Old);
};

UCLASS()
class PERSONATRIAL_API UPersonaDamageEffect : public UGameplayEffect
{
    GENERATED_BODY()
public: UPersonaDamageEffect();
};

UCLASS()
class PERSONATRIAL_API UPersonaFoodEffect : public UGameplayEffect
{
    GENERATED_BODY()
public: UPersonaFoodEffect();
};
