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
class PERSONATRIAL_API UPersonaLootRow : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    void SetLabel(const FString& Value);
    UPROPERTY() TObjectPtr<AActor> Target;
    int32 Index=0;
    FString ExpectedId;
    bool bDeposit=false;
private:
    UPROPERTY() TObjectPtr<UTextBlock> Label;
    UFUNCTION() void Transfer();
};

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
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY() TObjectPtr<UVerticalBox> Menu;
    UPROPERTY() TObjectPtr<UVerticalBox> Players;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Vitals;
    UPROPERTY() TObjectPtr<UTextBlock> Prompt;
    UPROPERTY() TObjectPtr<UTextBlock> Feedback;
    UPROPERTY() TObjectPtr<class UProgressBar> ThrowPower;
    UPROPERTY() TObjectPtr<UTextBlock> Inspection;
    UPROPERTY() TObjectPtr<UVerticalBox> LootBox;
    UPROPERTY() TObjectPtr<UVerticalBox> TerminalBox;
    UPROPERTY() TObjectPtr<UTextBlock> TrialStatus;
    UPROPERTY() TObjectPtr<UTextBlock> WaveStatus;
    FString LastLoot;
    UFUNCTION() void AcceptTrial();
    UFUNCTION() void RefuseTrial();
    UFUNCTION() void CloseTerminal();
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotLabels;
    UPROPERTY() TArray<TObjectPtr<class UBorder>> SlotBorders;
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
