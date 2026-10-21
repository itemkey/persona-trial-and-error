#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PersonaPanel.generated.h"
class UTextBlock;
class UVerticalBox;
class UComboBoxString;
class USlider;
class UButton;
class APersonaPlayerState;

UCLASS()
class PERSONATRIAL_API UPersonaPlayerRow : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetPlayer(APersonaPlayerState* InPlayer);
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
private:
    UPROPERTY() TObjectPtr<APersonaPlayerState> Player;
    UPROPERTY() TObjectPtr<UTextBlock> Label;
    UFUNCTION() void Mute();
};

UCLASS()
class PERSONATRIAL_API UPersonaPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    void SetPanelOpen(bool Open);
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UPROPERTY() TObjectPtr<UVerticalBox> Menu;
    UPROPERTY() TObjectPtr<UVerticalBox> Players;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Vitals;
    UPROPERTY() TObjectPtr<UTextBlock> Prompt;
    UPROPERTY() TObjectPtr<UComboBoxString> SessionList;
    TArray<FString> LastResults;
    TArray<TWeakObjectPtr<APersonaPlayerState>> LastPlayers;
    UButton* AddButton(UVerticalBox* Parent, const FString& Text);
    UFUNCTION() void Host();
    UFUNCTION() void Find();
    UFUNCTION() void Join();
    UFUNCTION() void Leave();
    UFUNCTION() void Invite();
    UFUNCTION() void Ready();
    UFUNCTION() void Mode();
    UFUNCTION() void Travel();
    UFUNCTION() void NPC1();
    UFUNCTION() void NPC10();
    UFUNCTION() void NPC30();
    UFUNCTION() void Damage();
    UFUNCTION() void Food();
    UFUNCTION() void Volume(float Value);
};
