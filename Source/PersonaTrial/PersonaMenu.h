#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "PersonaMenu.generated.h"
UCLASS()
class UPersonaMenuAction : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<class UPersonaMenu> Menu;
    FString Operation;
    int32 Index = 0;
    UFUNCTION() void Click();
};
UCLASS()
class PERSONATRIAL_API UPersonaMenu : public UUserWidget
{
    GENERATED_BODY()
public:
    bool bInGame = false;
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    void Activate(const FString& Operation, int32 Index = 0);
    void Show(const FString& Page);
private:
    UPROPERTY() TObjectPtr<class UVerticalBox> Contents;
    UPROPERTY() TObjectPtr<class UTextBlock> Status;
    UPROPERTY() TArray<TObjectPtr<UPersonaMenuAction>> Actions;
    TArray<FString> SectorSlots;
    FString CurrentPage;
    int32 ResultCount = -1;
    void AddAction(const FString& Label, const FString& Operation, bool Enabled = true, int32 Index = 0);
};
UCLASS()
class PERSONATRIAL_API APersonaMenuController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<UPersonaMenu> Menu;
};
UCLASS()
class PERSONATRIAL_API APersonaMenuMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    APersonaMenuMode();
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override {}
};
