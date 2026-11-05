#include "PersonaCatalog.h"
#include "PersonaInteraction.h"

UPersonaItemCatalog* UPersonaItemCatalog::Get()
{
    return LoadObject<UPersonaItemCatalog>(nullptr,TEXT("/Game/Persona/Items/DA_ItemCatalog.DA_ItemCatalog"));
}
UPersonaItemDefinition* UPersonaItemCatalog::Find(const FString& Path) const
{
    for(const auto& Item:Items) if(Item.ToSoftObjectPath().ToString()==Path) return Item.LoadSynchronous();
    return nullptr;
}
FString UPersonaItemCatalog::SearchKey(FString Text)
{
    Text=Text.ToLower();
    for(auto& C:Text) { if(C>=0x0410 && C<=0x042f) C+=0x20;else if(C==0x0401) C=0x0435; }
    return Text.Replace(TEXT("ё"),TEXT("е")).TrimStartAndEnd();
}
FString UPersonaItemCatalog::Properties(const UPersonaItemDefinition& Item)
{
    switch(Item.UseKind)
    {
        case EPersonaUseKind::Food:return FString::Printf(TEXT("Восстанавливает сытость: +%.0f"),Item.UseAmount);
        case EPersonaUseKind::Water:return FString::Printf(TEXT("Восстанавливает воду: +%.0f"),Item.UseAmount);
        case EPersonaUseKind::Health:return FString::Printf(TEXT("Восстанавливает здоровье: +%.0f ХП"),Item.UseAmount);
        default:return Item.bShotgun ? TEXT("Оружие. Совместимые патроны: 12 калибр.") : FString();
    }
}
