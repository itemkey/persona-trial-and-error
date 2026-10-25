#include "PersonaPanel.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaInteraction.h"
#include "PersonaOnline.h"
#include "PersonaLoot.h"
#include "PersonaWeapon.h"
#include "PersonaHub.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/ScrollBox.h"

namespace
{
    UTextBlock* Text(UWidgetTree* Tree, const FString& Value, int32 Size = 16)
    {
        auto T = Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Value));
        auto Font = T->GetFont(); Font.Size = Size; T->SetFont(Font); T->SetColorAndOpacity(FLinearColor(.82f,.85f,.88f));
        return T;
    }
}
void UPersonaLootRow::NativeOnInitialized()
{
    Super::NativeOnInitialized(); auto B=WidgetTree->ConstructWidget<UButton>(); WidgetTree->RootWidget=B;
    Label=Text(WidgetTree,TEXT(""),14); B->SetBackgroundColor(FLinearColor(.05f,.06f,.07f,.95f)); B->AddChild(Label); B->OnClicked.AddDynamic(this,&ThisClass::Transfer);
}
void UPersonaLootRow::SetLabel(const FString& Value) { Label->SetText(FText::FromString(Value)); }
void UPersonaLootRow::Transfer() { if(auto C=Cast<APersonaCharacter>(GetOwningPlayerPawn())) C->Interaction->ServerLoot(Target,Index,bDeposit,ExpectedId); }
void UPersonaPlayerRow::NativeOnInitialized()
{
    Super::NativeOnInitialized(); auto Button = WidgetTree->ConstructWidget<UButton>(); WidgetTree->RootWidget = Button;
    Button->SetBackgroundColor(FLinearColor(.04f,.045f,.055f,.95f));
    Label = Text(WidgetTree, TEXT("Participant"), 14); Label->SetAutoWrapText(true);
    Button->AddChild(Label); Button->OnClicked.AddDynamic(this, &ThisClass::Mute);
}
void UPersonaPlayerRow::SetPlayer(APersonaPlayerState* InPlayer) { Player = InPlayer; }
void UPersonaPlayerRow::Mute() { if (auto PC = Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ToggleMute(Player); }
void UPersonaPlayerRow::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta); if (!Player || !Label) return;
    auto PC = Cast<APersonaPlayerController>(GetOwningPlayer());
    auto C = Cast<APersonaCharacter>(Player->GetPawn());
    const FString Name = Player->GetPlayerName().Left(24);
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s\nT%d | %s %s %s"), *Name, Player->Team+1,
        Player->bReady ? TEXT("READY") : TEXT("WAIT"), C && C->Voice->bTalking ? TEXT("[VOICE]") : TEXT(""),
        PC && PC->IsMuted(Player) ? TEXT("[MUTED]") : TEXT("[click: mute]"))));
}
UButton* UPersonaPanel::AddButton(UVerticalBox* Parent, const FString& Value)
{
    auto B = WidgetTree->ConstructWidget<UButton>(); B->SetBackgroundColor(FLinearColor(.04f,.045f,.055f,.95f));
    B->AddChild(Text(WidgetTree, Value)); Parent->AddChild(B); return B;
}
void UPersonaPanel::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
    auto Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(.007f,.009f,.012f,.94f)); Border->SetPadding(FMargin(22));
    auto MenuSlot = Canvas->AddChildToCanvas(Border); MenuSlot->SetPosition(FVector2D(24, 24)); MenuSlot->SetSize(FVector2D(510, 780));
    auto Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Border->SetContent(Scroll);
    Menu = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Menu);
    Menu->AddChild(Text(WidgetTree, TEXT("P E R S O N A"), 30));
    Menu->AddChild(Text(WidgetTree, TEXT("TRIAL SYSTEM / PERSONA 0.4"), 12));
    Status = Text(WidgetTree, TEXT(""), 14); Status->SetAutoWrapText(true); Menu->AddChild(Status);
    AddButton(Menu, TEXT("Create session / Host"))->OnClicked.AddDynamic(this, &ThisClass::Host);
    AddButton(Menu, TEXT("Find sessions"))->OnClicked.AddDynamic(this, &ThisClass::Find);
    SessionList = WidgetTree->ConstructWidget<UComboBoxString>(); Menu->AddChild(SessionList);
    AddButton(Menu, TEXT("Join selected"))->OnClicked.AddDynamic(this, &ThisClass::Join);
    AddButton(Menu, TEXT("Invite Steam friends"))->OnClicked.AddDynamic(this, &ThisClass::Invite);
    AddButton(Menu, TEXT("Leave session"))->OnClicked.AddDynamic(this, &ThisClass::Leave);
    Menu->AddChild(Text(WidgetTree, TEXT("PARTICIPANTS"), 14)); Players = WidgetTree->ConstructWidget<UVerticalBox>(); Menu->AddChild(Players);
    AddButton(Menu, TEXT("Toggle ready"))->OnClicked.AddDynamic(this, &ThisClass::Ready);
    AddButton(Menu, TEXT("Host: switch cooperative / 2 vs 2"))->OnClicked.AddDynamic(this, &ThisClass::Mode);
    AddButton(Menu, TEXT("Host: travel to chamber / hub"))->OnClicked.AddDynamic(this, &ThisClass::Travel);
    Menu->AddChild(Text(WidgetTree, TEXT("VOICE VOLUME / HOLD V TO TALK"), 14));
    auto Slider = WidgetTree->ConstructWidget<USlider>(); Slider->SetValue(1); Slider->OnValueChanged.AddDynamic(this, &ThisClass::Volume); Menu->AddChild(Slider);
    Menu->AddChild(Text(WidgetTree, TEXT("DEVELOPMENT TEST CONTROLS"), 14));
    AddButton(Menu, TEXT("Host: 1 NPC"))->OnClicked.AddDynamic(this, &ThisClass::NPC1);
    AddButton(Menu, TEXT("Host: 10 NPCs"))->OnClicked.AddDynamic(this, &ThisClass::NPC10);
    AddButton(Menu, TEXT("Host: 30 NPCs"))->OnClicked.AddDynamic(this, &ThisClass::NPC30);
    AddButton(Menu, TEXT("Test: damage self / reset when down"))->OnClicked.AddDynamic(this, &ThisClass::Damage);
    AddButton(Menu, TEXT("Test: food +25"))->OnClicked.AddDynamic(this, &ThisClass::Food);
    Menu->AddChild(Text(WidgetTree, TEXT("TAB — панель\nWASD / мышь — движение / взгляд\nSHIFT — бег | CTRL — присесть | SPACE — прыжок\nE — взаимодействовать | 1–3 — слот | V — голос\nG — отпустить / удерживать для броска\nЛКМ — огонь | ПКМ — прицел | R — зарядить\nF — помпа | T — осмотр | B — предохранитель | U — разрядить"), 13));
    Vitals = Text(WidgetTree, TEXT(""), 16); auto VS = Canvas->AddChildToCanvas(Vitals); VS->SetAnchors(FAnchors(0,1)); VS->SetPosition(FVector2D(30,-60)); VS->SetAutoSize(true);
    Prompt = Text(WidgetTree, TEXT("+"), 16); auto PS = Canvas->AddChildToCanvas(Prompt); PS->SetAnchors(FAnchors(.5f,.5f)); PS->SetAlignment(FVector2D(.5f,.5f)); PS->SetAutoSize(true);
    auto Hotbar = WidgetTree->ConstructWidget<UHorizontalBox>();
    auto HS = Canvas->AddChildToCanvas(Hotbar); HS->SetAnchors(FAnchors(.5f,1));
    HS->SetAlignment(FVector2D(.5f,1)); HS->SetPosition(FVector2D(0,-24)); HS->SetAutoSize(true);
    for (int32 I=0; I<3; ++I)
    {
        auto Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(148); Size->SetHeightOverride(62);
        auto Cell = WidgetTree->ConstructWidget<UBorder>(); Cell->SetPadding(FMargin(10,6)); Size->SetContent(Cell);
        auto Label = Text(WidgetTree,TEXT(""),12); Label->SetAutoWrapText(true); Cell->SetContent(Label);
        Hotbar->AddChildToHorizontalBox(Size)->SetPadding(FMargin(3,0)); SlotLabels.Add(Label); SlotBorders.Add(Cell);
    }
    ThrowPower=WidgetTree->ConstructWidget<UProgressBar>(); ThrowPower->SetFillColorAndOpacity(FLinearColor(.75f,.8f,.8f));
    auto TS=Canvas->AddChildToCanvas(ThrowPower); TS->SetAnchors(FAnchors(.5f,1)); TS->SetAlignment(FVector2D(.5,1)); TS->SetPosition(FVector2D(0,-96)); TS->SetSize(FVector2D(220,6));
    Inspection=Text(WidgetTree,TEXT(""),14); auto IS=Canvas->AddChildToCanvas(Inspection); IS->SetAnchors(FAnchors(.5f,1)); IS->SetAlignment(FVector2D(.5,1)); IS->SetPosition(FVector2D(0,-126)); IS->SetAutoSize(true);
    auto LootScroll=WidgetTree->ConstructWidget<UScrollBox>();
    LootBox=WidgetTree->ConstructWidget<UVerticalBox>(); LootScroll->AddChild(LootBox);
    auto LS=Canvas->AddChildToCanvas(LootScroll); LS->SetAnchors(FAnchors(.5,1)); LS->SetAlignment(FVector2D(.5,1)); LS->SetPosition(FVector2D(0,-160)); LS->SetSize(FVector2D(470,330));
    TerminalBox=WidgetTree->ConstructWidget<UVerticalBox>(); auto TB=Canvas->AddChildToCanvas(TerminalBox); TB->SetAnchors(FAnchors(.5,.5)); TB->SetAlignment(FVector2D(.5,.5)); TB->SetSize(FVector2D(510,280));
    TrialStatus=Text(WidgetTree,TEXT("SHOOTING"),20); TrialStatus->SetAutoWrapText(true); TerminalBox->AddChild(TrialStatus);
    AddButton(TerminalBox,TEXT("Принять назначение"))->OnClicked.AddDynamic(this,&ThisClass::AcceptTrial);
    AddButton(TerminalBox,TEXT("Отказаться (−10 уважения)"))->OnClicked.AddDynamic(this,&ThisClass::RefuseTrial);
    AddButton(TerminalBox,TEXT("Закрыть"))->OnClicked.AddDynamic(this,&ThisClass::CloseTerminal);
    TerminalBox->SetVisibility(ESlateVisibility::Collapsed);
    WaveStatus=Text(WidgetTree,TEXT(""),18); auto WS=Canvas->AddChildToCanvas(WaveStatus); WS->SetPosition(FVector2D(30,28)); WS->SetAutoSize(true);
}
void UPersonaPanel::SetPanelOpen(bool Open)
{
    if (Menu && Menu->GetParent() && Menu->GetParent()->GetParent()) Menu->GetParent()->GetParent()->SetVisibility(Open ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
FReply UPersonaPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Tab)
    {
        if (auto PC = Cast<APersonaPlayerController>(GetOwningPlayer())) PC->TogglePanel();
        return FReply::Handled();
    }
    if(Event.GetKey()==EKeys::E || Event.GetKey()==EKeys::Escape)
        if(auto C=Cast<APersonaCharacter>(GetOwningPlayerPawn()); C && C->Interaction->LootTarget) { C->Interaction->CloseLoot(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UPersonaPanel::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    auto PC = Cast<APersonaPlayerController>(GetOwningPlayer()); if (!PC) return;
    auto GI = Cast<UPersonaGameInstance>(GetGameInstance()); auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>();
    auto GS = GetWorld()->GetGameState<APersonaGameState>();
    TerminalBox->SetVisibility(PC->bTerminalOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if(auto D=APersonaTrialDirector::Get(GetWorld()))
    {
        TrialStatus->SetText(FText::FromString(FString::Printf(TEXT("SHOOTING\nУважение: %d   Усиление NPC: +%d%%\nВолны: 5 / 10 / 20 / 30\nПринятие и отказ доступны хосту."),D->Run.Respect,D->Run.Penalty*10)));
        WaveStatus->SetText(FText::FromString(D->Running() ? D->ScreenText() : FString()));
    }
    Status->SetText(FText::FromString((GI ? GI->Status : TEXT("")) + FString::Printf(TEXT("\n%s | NPC %d | %s"),
        GS && GS->Mode == EPersonaMode::Teams ? TEXT("2 vs 2") : TEXT("Cooperative"), GS ? GS->NPCCount : 0,
        Online && Online->IsSteam() ? TEXT("Steam") : TEXT("Local / Null"))));
    if (Online && Online->Results != LastResults)
    {
        LastResults = Online->Results; SessionList->ClearOptions();
        for (const FString& Result : LastResults) SessionList->AddOption(Result);
        if (LastResults.Num()) SessionList->SetSelectedIndex(0);
    }
    TArray<TWeakObjectPtr<APersonaPlayerState>> Current;
    if (GS) for (auto State : GS->PlayerArray) if (auto P = Cast<APersonaPlayerState>(State)) Current.Add(P);
    if (Current != LastPlayers)
    {
        LastPlayers = Current; Players->ClearChildren();
        for (auto P : Current) { auto Row = CreateWidget<UPersonaPlayerRow>(PC); Row->SetPlayer(P.Get()); Players->AddChild(Row); }
    }
    if (auto C = Cast<APersonaCharacter>(PC->GetPawn()))
    {
        const auto A = C->GetVitals();
        Vitals->SetText(FText::FromString(FString::Printf(TEXT("HEALTH %03.0f    STAMINA %03.0f    FOOD %03.0f    | TAB: session"), A->GetHealth(), A->GetStamina(), A->GetHunger())));
        AActor* Target = C->Interaction->TraceTarget();
        auto Inventory = C->Interaction;
        Prompt->SetText(Inventory->FeedbackUntil > GetWorld()->GetTimeSeconds() ? FText::FromString(Inventory->Feedback) :
            Target && Target->Implements<UPersonaInteractable>() ? IPersonaInteractable::Execute_InteractionLabel(Target) :
            FText::FromString(TEXT("+")));
        ThrowPower->SetVisibility(Inventory->bCharging && Inventory->ThrowAlpha()>0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); ThrowPower->SetPercent(Inventory->ThrowAlpha());
        const float Now=GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
        // Workbench inspection starts on the server; its replicated action
        // also exposes the brief readout to the interacting remote player.
        Inspection->SetText(FText::FromString(C->Weapon->InspectUntil>Now || C->Weapon->Action==EPersonaWeaponAction::Inspect ? C->Weapon->Inspection() : C->bDead || C->bSpectator ? TEXT("Наблюдение за участниками") : TEXT("")));
        auto L=Inventory->LootTarget ? Inventory->LootTarget->FindComponentByClass<UPersonaLoot>() : nullptr;
        FString Signature=L ? Inventory->LootTarget->GetName() : FString();
        if(L) { for(const auto& R:L->Items) Signature+=R.Id+FString::FromInt(R.Quantity); for(auto P:Inventory->Slots) if(P) Signature+=P->PersistentItemId+FString::FromInt(P->Quantity); }
        if(Signature!=LastLoot)
        {
            LastLoot=Signature; LootBox->ClearChildren();
            if(L)
            {
                LootBox->AddChild(Text(WidgetTree,TEXT("СОДЕРЖИМОЕ   /   E — закрыть"),16));
                for(int32 I=0; I<L->Items.Num(); ++I)
                {
                    const auto& R=L->Items[I]; auto D=LoadObject<UPersonaItemDefinition>(nullptr,*R.Definition); auto Row=CreateWidget<UPersonaLootRow>(PC);
                    Row->Target=Inventory->LootTarget; Row->Index=I; Row->ExpectedId=R.Id; Row->SetLabel(FString::Printf(TEXT("Взять: %s ×%d"),D ? *D->DisplayName.ToString() : TEXT("Предмет"),R.Quantity)); LootBox->AddChild(Row);
                }
                if(L->Items.IsEmpty()) LootBox->AddChild(Text(WidgetTree,TEXT("Пусто"),14));
                if(L->bDeposit) for(int32 I=0; I<3; ++I) if(auto P=Inventory->Slots[I])
                {
                    auto Row=CreateWidget<UPersonaLootRow>(PC); Row->Target=Inventory->LootTarget; Row->Index=I; Row->ExpectedId=P->PersistentItemId; Row->bDeposit=true; Row->SetLabel(FString::Printf(TEXT("Положить: %s ×%d"),*P->ItemName().ToString(),P->Quantity)); LootBox->AddChild(Row);
                }
            }
        }
        for (int32 I=0; I<3; ++I)
        {
            auto Item = Inventory->Slots.IsValidIndex(I) ? Inventory->Slots[I] : nullptr;
            const bool Selected = I == Inventory->SelectedSlot;
            SlotBorders[I]->SetBrushColor(Selected ? FLinearColor(.22f,.23f,.24f,.9f) : FLinearColor(.025f,.028f,.03f,.85f));
            const FString Contents = Item ? FString::Printf(TEXT("%s  ×%d"),*Item->ItemName().ToString(),Item->Quantity) : TEXT("—");
            SlotLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%d %s\n%s"),I+1,Selected ? TEXT("/ В РУКАХ") : TEXT(""),*Contents)));
        }
    }
}
void UPersonaPanel::AcceptTrial() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerTrialDecision(true); CloseTerminal(); }
void UPersonaPanel::RefuseTrial() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerTrialDecision(false); CloseTerminal(); }
void UPersonaPanel::CloseTerminal() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) { PC->bTerminalOpen=false; PC->ResetIgnoreMoveInput(); PC->ResetIgnoreLookInput(); PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); } }
void UPersonaPanel::Host() { GetGameInstance()->GetSubsystem<UPersonaOnline>()->Host(); }
void UPersonaPanel::Find() { GetGameInstance()->GetSubsystem<UPersonaOnline>()->Find(); }
void UPersonaPanel::Join() { GetGameInstance()->GetSubsystem<UPersonaOnline>()->Join(SessionList->GetSelectedIndex()); }
void UPersonaPanel::Leave() { GetGameInstance()->GetSubsystem<UPersonaOnline>()->Leave(); }
void UPersonaPanel::Invite() { GetGameInstance()->GetSubsystem<UPersonaOnline>()->InviteFriends(); }
void UPersonaPanel::Ready() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->PersonaReady(); }
void UPersonaPanel::Mode() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) { auto GS = GetWorld()->GetGameState<APersonaGameState>(); P->ServerSetMode(GS && GS->Mode == EPersonaMode::Teams ? EPersonaMode::Cooperative : EPersonaMode::Teams); } }
void UPersonaPanel::Travel() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) { auto GS = GetWorld()->GetGameState<APersonaGameState>(); P->ServerTravel(!GS || !GS->bInTrial); } }
void UPersonaPanel::NPC1() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->ServerNPCCount(1); }
void UPersonaPanel::NPC10() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->ServerNPCCount(10); }
void UPersonaPanel::NPC30() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->ServerNPCCount(30); }
void UPersonaPanel::Damage() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->ServerTestVitals(false); }
void UPersonaPanel::Food() { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->ServerTestVitals(true); }
void UPersonaPanel::Volume(float Value) { if (auto P = Cast<APersonaPlayerController>(GetOwningPlayer())) P->SetVoiceVolume(Value); }
