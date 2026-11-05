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
class UPersonaSectorRow : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    void Setup(int32 InIndex,const FString& Title);
private:
    int32 Index=0;
    UPROPERTY() TObjectPtr<UTextBlock> Label;
    UFUNCTION() void Choose();
};
UCLASS()
class UPersonaTrialRow : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<class UPersonaPanel> Panel;
    FName TrialId;
    FString Title;
    void Setup(class UPersonaPanel* InPanel,FName Id,const FString& Text);
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY() TObjectPtr<UTextBlock> Label;
    UFUNCTION() void Choose();
};

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
    FName Part;
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
    friend class UPersonaDiagnostics;
public:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    void SetPanelOpen(bool Open);
    void SelectTrial(FName Id);
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
    UPROPERTY() TObjectPtr<UVerticalBox> CoopBox;
    UPROPERTY() TObjectPtr<UVerticalBox> BattleBox;
    UPROPERTY() TObjectPtr<UVerticalBox> BattleList;
    UPROPERTY() TObjectPtr<UTextBlock> BattleStatus;
    UPROPERTY() TObjectPtr<UButton> BattleOpenButton;
    UPROPERTY() TObjectPtr<UButton> BattleAcceptButton;
    UPROPERTY() TObjectPtr<UButton> BattleCancelButton;
    UPROPERTY() TObjectPtr<UButton> BattleReadyButton;
    UPROPERTY() TObjectPtr<class UBorder> TravelVeil;
    UPROPERTY() TObjectPtr<UTextBlock> TravelStatus;
    float TravelVeilAlpha=0;
    bool bBattlePage=false;
    FString LastBattleList;
    UFUNCTION() void ShowCoop();
    UFUNCTION() void ShowBattle();
    UFUNCTION() void ToggleBattleOpen();
    UFUNCTION() void FindOpponents();
    UFUNCTION() void FindFriendOpponents();
    UFUNCTION() void AcceptBattle();
    UFUNCTION() void CancelBattle();
    UFUNCTION() void ReadyBattle();
    void UpdateBattle();
    UPROPERTY() TObjectPtr<UTextBlock> TrialStatus;
    UPROPERTY() TObjectPtr<UTextBlock> WaveStatus;
    int32 DrawFieldMap(const class APersonaTrialDirector& D,const class APersonaCharacter& C,const FGeometry& G,FSlateWindowElementList& List,int32 Layer) const;
    FString LastLoot;
    UFUNCTION() void AcceptTrial();
    UPROPERTY() TObjectPtr<class UBorder> TerminalFrame;
    UPROPERTY() TObjectPtr<UVerticalBox> TrialList;
    UPROPERTY() TObjectPtr<class UEditableTextBox> TrialSearch;
    UPROPERTY() TObjectPtr<class UWrapBox> FindsRow;
    UPROPERTY() TObjectPtr<class UWrapBox> RequirementsRow;
    UPROPERTY() TObjectPtr<UTextBlock> StartingItems;
    UFUNCTION() void SearchTrials(const FText& Text);
    void RebuildTrials();
    void RebuildFinds();
    FName SelectedTrial;
    FString LastTrialSignature;
    UPROPERTY() TObjectPtr<UButton> AcceptTrialButton;
    TArray<FName> TrialIds;
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
    UPROPERTY() TObjectPtr<UComboBoxString> ItemList;
    UPROPERTY() TObjectPtr<class UEditableTextBox> ItemSearch;
    UPROPERTY() TObjectPtr<class USpinBox> ItemQuantity;
    UPROPERTY() TObjectPtr<class UEditableTextBox> ConsoleInput;
    UPROPERTY() TObjectPtr<UTextBlock> ConsoleOutput;
    TArray<FString> ItemPaths;
    TArray<FString> ConsoleHistory;
    int32 HistoryIndex=0;
    UFUNCTION() void SearchItems(const FText& Text);
    UFUNCTION() void GiveItem();
    UFUNCTION() void GiveAmmo();
    UFUNCTION() void RunConsole();
    UFUNCTION() void CommitConsole(const FText& Text,ETextCommit::Type Method);
};
