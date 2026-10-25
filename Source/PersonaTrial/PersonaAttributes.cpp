#include "PersonaAttributes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UPersonaAttributes::UPersonaAttributes() { InitMaxHealth(100); InitHealth(100); InitStamina(100); InitHunger(100); }
void UPersonaAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UPersonaAttributes, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPersonaAttributes, MaxHealth, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPersonaAttributes, Stamina, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UPersonaAttributes, Hunger, COND_None, REPNOTIFY_Always);
}
void UPersonaAttributes::PreAttributeChange(const FGameplayAttribute& Attribute, float& Value)
{
    Super::PreAttributeChange(Attribute, Value);
    Value = FMath::Clamp(Value, Attribute==GetMaxHealthAttribute() ? 1.f : 0.f, Attribute==GetMaxHealthAttribute() ? 10000.f : Attribute==GetHealthAttribute() ? GetMaxHealth() : 100.f);
}
void UPersonaAttributes::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);
    SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
    SetStamina(FMath::Clamp(GetStamina(), 0.f, 100.f));
    SetHunger(FMath::Clamp(GetHunger(), 0.f, 100.f));
}
void UPersonaAttributes::OnRep_Health(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UPersonaAttributes, Health, Old); }
void UPersonaAttributes::OnRep_MaxHealth(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UPersonaAttributes, MaxHealth, Old); }
void UPersonaAttributes::OnRep_Stamina(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UPersonaAttributes, Stamina, Old); }
void UPersonaAttributes::OnRep_Hunger(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UPersonaAttributes, Hunger, Old); }
UPersonaDamageEffect::UPersonaDamageEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UPersonaAttributes::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    FSetByCallerFloat Amount; Amount.DataName = TEXT("Damage");
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}
UPersonaFoodEffect::UPersonaFoodEffect()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
    Modifier.Attribute = UPersonaAttributes::GetHungerAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(25.f);
}
