#include "PersonaPanel.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "PersonaFramework.h"
#include "PersonaBattle.h"
#include "PersonaBattleArena.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "EngineUtils.h"
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
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

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
void UPersonaLootRow::Transfer() { if(auto C=Cast<APersonaCharacter>(GetOwningPlayerPawn())) C->Interaction->ServerLoot(Target,Index,bDeposit,ExpectedId,Part); }
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
    Super::NativeOnInitialized();SetIsFocusable(true);
    auto Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
    auto Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(.007f,.009f,.012f,.94f)); Border->SetPadding(FMargin(22));
    auto MenuSlot = Canvas->AddChildToCanvas(Border); MenuSlot->SetPosition(FVector2D(24, 24)); MenuSlot->SetSize(FVector2D(550, 650));
    auto Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Border->SetContent(Scroll);
    Menu = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Menu);
    Menu->AddChild(Text(WidgetTree, TEXT("P E R S O N A"), 30));
    Menu->AddChild(Text(WidgetTree, TEXT("PERSONA 0.11.0"), 12));
    Status = Text(WidgetTree, TEXT(""), 14); Status->SetAutoWrapText(true); Menu->AddChild(Status);
    Menu->AddChild(Text(WidgetTree,TEXT("АДМИНИСТРАТОР / ` — закрыть"),18));
    Menu->AddChild(Text(WidgetTree,TEXT("ПРЕДМЕТЫ"),16));
    ItemSearch=WidgetTree->ConstructWidget<UEditableTextBox>();ItemSearch->SetHintText(FText::FromString(TEXT("Найти предмет")));Menu->AddChild(ItemSearch);
    ItemSearch->OnTextChanged.AddDynamic(this,&ThisClass::SearchItems);
    ItemList=WidgetTree->ConstructWidget<UComboBoxString>();Menu->AddChild(ItemList);
    ItemQuantity=WidgetTree->ConstructWidget<USpinBox>();ItemQuantity->SetMinValue(1);ItemQuantity->SetMaxValue(999);ItemQuantity->SetDelta(1);ItemQuantity->SetMinFractionalDigits(0);ItemQuantity->SetMaxFractionalDigits(0);ItemQuantity->SetValue(1);Menu->AddChild(ItemQuantity);
    AddButton(Menu,TEXT("Выдать выбранный предмет"))->OnClicked.AddDynamic(this,&ThisClass::GiveItem);
    AddButton(Menu,TEXT("Выдать патроны для выбранного оружия"))->OnClicked.AddDynamic(this,&ThisClass::GiveAmmo);
    AddButton(Menu,TEXT("Режим: кооператив / 2 × 2"))->OnClicked.AddDynamic(this,&ThisClass::Mode);
    AddButton(Menu,TEXT("Перейти: испытательный зал / хаб"))->OnClicked.AddDynamic(this,&ThisClass::Travel);
    AddButton(Menu,TEXT("Создать 1 NPC"))->OnClicked.AddDynamic(this,&ThisClass::NPC1);
    AddButton(Menu,TEXT("Создать 10 NPC"))->OnClicked.AddDynamic(this,&ThisClass::NPC10);
    AddButton(Menu,TEXT("Создать 30 NPC"))->OnClicked.AddDynamic(this,&ThisClass::NPC30);
    AddButton(Menu,TEXT("Получить урон / восстановиться"))->OnClicked.AddDynamic(this,&ThisClass::Damage);
    AddButton(Menu,TEXT("Сытость +25"))->OnClicked.AddDynamic(this,&ThisClass::Food);
    Menu->AddChild(Text(WidgetTree,TEXT("КОНСОЛЬ / ↑↓ — история"),16));
    ConsoleOutput=Text(WidgetTree,TEXT(""),12);ConsoleOutput->SetAutoWrapText(true);Menu->AddChild(ConsoleOutput);
    ConsoleInput=WidgetTree->ConstructWidget<UEditableTextBox>();ConsoleInput->SetHintText(FText::FromString(TEXT("Команда · Enter")));ConsoleInput->OnTextCommitted.AddDynamic(this,&ThisClass::CommitConsole);Menu->AddChild(ConsoleInput);
    AddButton(Menu,TEXT("Выполнить команду"))->OnClicked.AddDynamic(this,&ThisClass::RunConsole);
    SearchItems(FText::GetEmpty());
    Vitals = Text(WidgetTree, TEXT(""), 16); auto VS = Canvas->AddChildToCanvas(Vitals); VS->SetAnchors(FAnchors(0,1)); VS->SetPosition(FVector2D(30,-60)); VS->SetAutoSize(true);
    Prompt = Text(WidgetTree, TEXT("+"), 16); auto PS = Canvas->AddChildToCanvas(Prompt); PS->SetAnchors(FAnchors(.5f,.5f)); PS->SetAlignment(FVector2D(.5f,.5f)); PS->SetAutoSize(true);
    Feedback=Text(WidgetTree,TEXT(""),14); auto FS=Canvas->AddChildToCanvas(Feedback);
    FS->SetAnchors(FAnchors(.5f,.56f)); FS->SetAlignment(FVector2D(.5f,.5f)); FS->SetAutoSize(true);
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
    TerminalFrame=WidgetTree->ConstructWidget<UBorder>(); TerminalFrame->SetBrushColor(FLinearColor(.007f,.009f,.012f,.98f)); TerminalFrame->SetPadding(FMargin(20));
    auto TB=Canvas->AddChildToCanvas(TerminalFrame);TB->SetAnchors(FAnchors(.5,.5));TB->SetAlignment(FVector2D(.5,.5));TB->SetSize(FVector2D(980,650));
    TerminalBox=WidgetTree->ConstructWidget<UVerticalBox>();TerminalFrame->SetContent(TerminalBox);
    TerminalBox->AddChild(Text(WidgetTree,TEXT("КАТАЛОГ ИСПЫТАНИЙ"),24));
    auto Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();TerminalBox->AddChild(Tabs);
    auto Tab=[&](const FString& Caption) { auto B=WidgetTree->ConstructWidget<UButton>();B->SetBackgroundColor(FLinearColor(.08f,.09f,.1f));B->AddChild(Text(WidgetTree,Caption,16));Tabs->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));return B; };
    Tab(TEXT("Совместные испытания"))->OnClicked.AddDynamic(this,&ThisClass::ShowCoop);
    Tab(TEXT("Встречное испытание"))->OnClicked.AddDynamic(this,&ThisClass::ShowBattle);
    CoopBox=WidgetTree->ConstructWidget<UVerticalBox>();TerminalBox->AddChildToVerticalBox(CoopBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TrialSearch=WidgetTree->ConstructWidget<UEditableTextBox>();TrialSearch->SetHintText(FText::FromString(TEXT("Поиск испытания или предмета: вода, дробовик, шприц…")));
    TrialSearch->OnTextChanged.AddDynamic(this,&ThisClass::SearchTrials);CoopBox->AddChild(TrialSearch);
    auto Columns=WidgetTree->ConstructWidget<UHorizontalBox>();CoopBox->AddChildToVerticalBox(Columns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto ListSize=WidgetTree->ConstructWidget<USizeBox>();ListSize->SetWidthOverride(240);Columns->AddChildToHorizontalBox(ListSize)->SetPadding(FMargin(0,12,18,12));
    auto ListScroll=WidgetTree->ConstructWidget<UScrollBox>();ListSize->SetContent(ListScroll);TrialList=WidgetTree->ConstructWidget<UVerticalBox>();ListScroll->AddChild(TrialList);
    auto CardScroll=WidgetTree->ConstructWidget<UScrollBox>();Columns->AddChildToHorizontalBox(CardScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto Card=WidgetTree->ConstructWidget<UVerticalBox>();CardScroll->AddChild(Card);
    Card->AddChild(Text(WidgetTree,TEXT("МОЖНО НАЙТИ И ВЫВЕЗТИ"),16));FindsRow=WidgetTree->ConstructWidget<UWrapBox>();FindsRow->SetInnerSlotPadding(FVector2D(8,8));Card->AddChild(FindsRow);
    StartingItems=Text(WidgetTree,TEXT(""),14);StartingItems->SetAutoWrapText(true);Card->AddChild(StartingItems);
    RequirementsRow=WidgetTree->ConstructWidget<UWrapBox>();RequirementsRow->SetInnerSlotPadding(FVector2D(8,8));Card->AddChild(RequirementsRow);
    TrialStatus=Text(WidgetTree,TEXT(""),16);TrialStatus->SetAutoWrapText(true);Card->AddChild(TrialStatus);
    AcceptTrialButton=AddButton(CoopBox,TEXT("Принять испытание"));AcceptTrialButton->OnClicked.AddDynamic(this,&ThisClass::AcceptTrial);
    BattleBox=WidgetTree->ConstructWidget<UVerticalBox>();TerminalBox->AddChildToVerticalBox(BattleBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BattleBox->SetVisibility(ESlateVisibility::Collapsed);
    auto Rules=Text(WidgetTree,TEXT("Равные команды из двух секторов. Победители собирают трофеи и вывозят их своим лифтом. Оставленные в хабе вещи сохраняются."),16);Rules->SetAutoWrapText(true);BattleBox->AddChild(Rules);
    BattleStatus=Text(WidgetTree,TEXT(""),16);BattleStatus->SetAutoWrapText(true);BattleBox->AddChild(BattleStatus);
    BattleOpenButton=AddButton(BattleBox,TEXT("Открыть сектор для вызовов"));BattleOpenButton->OnClicked.AddDynamic(this,&ThisClass::ToggleBattleOpen);
    AddButton(BattleBox,TEXT("Найти открытые сектора"))->OnClicked.AddDynamic(this,&ThisClass::FindOpponents);
    AddButton(BattleBox,TEXT("Только Steam-друзья"))->OnClicked.AddDynamic(this,&ThisClass::FindFriendOpponents);
    auto Opponents=WidgetTree->ConstructWidget<UScrollBox>();BattleBox->AddChildToVerticalBox(Opponents)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));BattleList=WidgetTree->ConstructWidget<UVerticalBox>();Opponents->AddChild(BattleList);
    BattleAcceptButton=AddButton(BattleBox,TEXT("Принять вызов"));BattleAcceptButton->OnClicked.AddDynamic(this,&ThisClass::AcceptBattle);
    BattleReadyButton=AddButton(BattleBox,TEXT("Готов к отправлению"));BattleReadyButton->OnClicked.AddDynamic(this,&ThisClass::ReadyBattle);
    BattleCancelButton=AddButton(BattleBox,TEXT("Отменить вызов"));BattleCancelButton->OnClicked.AddDynamic(this,&ThisClass::CancelBattle);
    AddButton(TerminalBox,TEXT("Закрыть · Tab / Esc"))->OnClicked.AddDynamic(this,&ThisClass::CloseTerminal);
    TerminalFrame->SetVisibility(ESlateVisibility::Collapsed);
    WaveStatus=Text(WidgetTree,TEXT(""),18); auto WS=Canvas->AddChildToCanvas(WaveStatus); WS->SetPosition(FVector2D(30,28)); WS->SetAutoSize(true);
    TravelVeil=WidgetTree->ConstructWidget<UBorder>();TravelVeil->SetBrushColor(FLinearColor::Black);TravelVeil->SetVisibility(ESlateVisibility::Collapsed);
    TravelVeil->SetHorizontalAlignment(HAlign_Center);TravelVeil->SetVerticalAlignment(VAlign_Center);TravelStatus=Text(WidgetTree,TEXT(""),20);TravelStatus->SetJustification(ETextJustify::Center);TravelVeil->SetContent(TravelStatus);
    auto VeilSlot=Canvas->AddChildToCanvas(TravelVeil);VeilSlot->SetAnchors(FAnchors(0,0,1,1));VeilSlot->SetOffsets(FMargin(0));VeilSlot->SetZOrder(100);
}
void UPersonaPanel::SetPanelOpen(bool Open)
{
    if (Menu && Menu->GetParent() && Menu->GetParent()->GetParent()) Menu->GetParent()->GetParent()->SetVisibility(Open ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
int32 UPersonaPanel::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    int32 Last=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    auto C=Cast<APersonaCharacter>(GetOwningPlayerPawn()); if(!C || !C->IsAlive()) return Last;
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->IsArena())
    {
        auto PC=GetOwningPlayer();const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),12);
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
        {
            if(*It==C || !It->IsAlive() || It->SavedTeam!=C->SavedTeam || !PC->LineOfSightTo(*It)) continue;
            FVector2D At;if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,It->GetMesh()->GetBoneLocation(TEXT("head"))+FVector(0,0,25),At,true)) continue;
            const auto State=It->GetPlayerState();const auto Name=State ? State->GetPlayerName() : FString(TEXT("Напарник"));
            FSlateDrawElement::MakeText(Elements,Last+1,Geometry.ToPaintGeometry(FVector2D(180,24),FSlateLayoutTransform(At-FVector2D(40,0))),TEXT("◆ ")+Name,Font,ESlateDrawEffect::None,FLinearColor(.85f,.94f,1.f));
        }
        ++Last;
    }
    if(C->bFieldMapOpen && !C->bTransit)
    {
        const auto D=APersonaTrialDirector::Get(GetWorld());
        const auto P=D ? D->Run.Forest.Player(C->PersistentId) : nullptr;
        if(P && P->HasMap && P->Outcome==EForestOutcome::Active)
            return DrawFieldMap(*D,*C,Geometry,Elements,Last+2);
    }
    const float Low=C->GetVitals()->GetHealth()<30 ? (1-C->GetVitals()->GetHealth()/30.f)*.12f : 0;
    if(C->DamageFlash<=0 && Low<=0) return Last;
    const FVector Source=C->GetBaseAimRotation().UnrotateVector(-C->DamageDirection);
    const float Weights[]={FMath::Max(0.f,float(Source.X)),FMath::Max(0.f,float(-Source.X)),FMath::Max(0.f,float(-Source.Y)),FMath::Max(0.f,float(Source.Y))};
    const FVector2D Size=Geometry.GetLocalSize(); const float Width=FMath::Min(Size.X,Size.Y)*.085f;
    const FSlateBrush* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    for(int32 Band=0;Band<12;++Band)
    {
        const float Offset=Width*Band/12, Thickness=Width/12+1;
        const float Fade=FMath::Square(1-Band/12.f);
        for(int32 Edge=0;Edge<4;++Edge)
        {
            const FVector2D At=Edge==0 ? FVector2D(0,Offset) : Edge==1 ? FVector2D(0,Size.Y-Offset-Thickness)
                : Edge==2 ? FVector2D(Offset,0) : FVector2D(Size.X-Offset-Thickness,0);
            const FVector2D Extent=Edge<2 ? FVector2D(Size.X,Thickness) : FVector2D(Thickness,Size.Y);
            const float Opacity=Fade*(Low+C->DamageFlash*(.12f+.3f*Weights[Edge]));
            FSlateDrawElement::MakeBox(Elements,Last+1,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),
                Brush,ESlateDrawEffect::None,FLinearColor(.65f,.005f,.01f,Opacity));
        }
    }
    return Last+1;
}
FReply UPersonaPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    auto PC=Cast<APersonaPlayerController>(GetOwningPlayer());
    if(PC && !PC->IsPanelOpen() && Event.GetKey()==EKeys::Tab) { PC->HandleTab();return FReply::Handled(); }
    if(PC && Event.GetKey()==EKeys::Escape) { PC->TogglePauseMenu();return FReply::Handled(); }
    if(PC && PC->IsPanelOpen() && ConsoleInput->HasKeyboardFocus() && (Event.GetKey()==EKeys::Up || Event.GetKey()==EKeys::Down))
    {
        HistoryIndex=FMath::Clamp(HistoryIndex+(Event.GetKey()==EKeys::Up ? -1 : 1),0,ConsoleHistory.Num());
        ConsoleInput->SetText(FText::FromString(ConsoleHistory.IsValidIndex(HistoryIndex) ? ConsoleHistory[HistoryIndex] : FString()));return FReply::Handled();
    }
    if(Event.GetKey()==EKeys::E) if(auto C=Cast<APersonaCharacter>(GetOwningPlayerPawn());C && C->Interaction->LootTarget) { C->Interaction->CloseLoot();return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UPersonaPanel::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    auto PC = Cast<APersonaPlayerController>(GetOwningPlayer()); if (!PC) return;
    auto GI = Cast<UPersonaGameInstance>(GetGameInstance()); auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>();
    auto GS = GetWorld()->GetGameState<APersonaGameState>();
    if(auto C=Cast<APersonaCharacter>(PC->GetPawn()); PC->bTerminalOpen && (!C || !C->IsAlive() || C->bTransit)) CloseTerminal();
    TerminalFrame->SetVisibility(PC->bTerminalOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    UpdateBattle();
    const auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();auto BS=APersonaBattleState::Get(GetWorld());
    const bool Obscure=Battle->ClosingCabin() || (Battle->Phase==EPersonaBattlePhase::Travelling && !Battle->IsArena()) || (Battle->IsArena() && BS && BS->Phase==EPersonaBattlePhase::Waiting);
    TravelVeilAlpha=FMath::FInterpConstantTo(TravelVeilAlpha,Obscure ? 1.f : 0.f,Delta,2.f);TravelVeil->SetRenderOpacity(TravelVeilAlpha);TravelVeil->SetVisibility(TravelVeilAlpha>0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    TravelStatus->SetText(FText::FromString(BS ? BS->Status : GI->Status));
    if(auto D=APersonaTrialDirector::Get(GetWorld()))
    {
        FString Signature;for(auto Trial:D->Trials) if(Trial) Signature+=Trial->GetPathName();
        if(Signature!=LastTrialSignature) { LastTrialSignature=Signature;RebuildTrials(); }
        TrialStatus->SetText(FText::FromString(SelectedTrial.IsNone() ? TEXT("Ничего не найдено. Измените запрос.") : D->TrialDetails(SelectedTrial)));
        AcceptTrialButton->SetIsEnabled(Battle->Phase==EPersonaBattlePhase::Idle && !D->Running() && !SelectedTrial.IsNone() && (SelectedTrial!=PersonaForest::TrialId || D->ForestWait()==0));
        WaveStatus->SetText(FText::FromString(D->Running() ? D->ScreenText() : FString()));
    }
    if(Battle->IsArena() && BS) WaveStatus->SetText(FText::FromString(TEXT("ВСТРЕЧНОЕ ИСПЫТАНИЕ\n")+BS->Status));
    Status->SetText(FText::FromString((GI ? GI->Status : TEXT("")) + FString::Printf(TEXT("\n%s | NPC %d | %s"),
        GS && GS->Mode == EPersonaMode::Teams ? TEXT("2 vs 2") : TEXT("Cooperative"), GS ? GS->NPCCount : 0,
        Online && Online->IsSteam() ? TEXT("Steam") : TEXT("Local / Null"))));
    if (auto C = Cast<APersonaCharacter>(PC->GetPawn()))
    {
        const auto A = C->GetVitals();
        Vitals->SetText(FText::FromString(FString::Printf(TEXT("Здоровье %03.0f    Силы %03.0f    Сытость %03.0f    Вода %03.0f"), A->GetHealth(), A->GetStamina(), A->GetHunger(), C->Water)));
        if(auto Director=APersonaTrialDirector::Get(GetWorld()); Director && Director->Run.Phase==EPersonaTrialPhase::Forest)
            WaveStatus->SetText(FText::FromString(Director->ForestStatus(C)));
        if(C->Water<=20 && C->IsAlive()) Vitals->SetColorAndOpacity(FSlateColor(FLinearColor(1,.55f,.31f)));
        else Vitals->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        if(C->bDryCollapse) WaveStatus->SetText(FText::FromString(TEXT("Обезвоживание. Напарник может поднять вас. Если группа погибла — E, чтобы очнуться.")));
        auto Inventory = C->Interaction;
        Prompt->SetVisibility(C->IsAlive() && !PC->IsPanelOpen() && !Inventory->LootTarget && C->Weapon->AimAlpha<.8f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Feedback->SetText(Inventory->FeedbackUntil>GetWorld()->GetTimeSeconds() ? FText::FromString(Inventory->Feedback) : FText::GetEmpty());
        ThrowPower->SetVisibility(Inventory->bCharging && Inventory->ThrowAlpha()>0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); ThrowPower->SetPercent(Inventory->ThrowAlpha());
        const float Now=GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
        // Workbench inspection starts on the server; its replicated action
        // also exposes the brief readout to the interacting remote player.
        Inspection->SetText(FText::FromString(C->Weapon->InspectUntil>Now || C->Weapon->Action==EPersonaWeaponAction::Inspect ? C->Weapon->Inspection() : C->bDead || C->bSpectator ? TEXT("Наблюдение за участниками") : TEXT("")));
        auto L=Inventory->LootTarget ? Inventory->LootTarget->FindComponentByClass<UPersonaLoot>() : nullptr;
        const auto Entries=L ? L->Contents(Inventory->LootPart) : nullptr;
        FString Signature=Entries ? Inventory->LootTarget->GetName()+Inventory->LootPart.ToString() : FString();
        if(Entries) { for(const auto& R:(*Entries)) Signature+=R.Id+FString::FromInt(R.Quantity); for(auto P:Inventory->Slots) if(P) Signature+=P->PersistentItemId+FString::FromInt(P->Quantity); }
        if(Signature!=LastLoot)
        {
            LastLoot=Signature; LootBox->ClearChildren();
            if(Entries)
            {
                LootBox->AddChild(Text(WidgetTree,!L->HasSharedInventory() && L->FindPart(Inventory->LootPart) ? L->FindPart(Inventory->LootPart)->Label : TEXT("СОДЕРЖИМОЕ"),16));
                for(int32 I=0; I<(*Entries).Num(); ++I)
                {
                    const auto& R=(*Entries)[I]; auto D=LoadObject<UPersonaItemDefinition>(nullptr,*R.Definition); auto Row=CreateWidget<UPersonaLootRow>(PC);
                    Row->Target=Inventory->LootTarget; Row->Part=Inventory->LootPart; Row->Index=I; Row->ExpectedId=R.Id; Row->SetLabel(FString::Printf(TEXT("Взять: %s ×%d"),D ? *D->DisplayName.ToString() : TEXT("Предмет"),R.Quantity)); LootBox->AddChild(Row);
                }
                if((*Entries).IsEmpty()) LootBox->AddChild(Text(WidgetTree,TEXT("Пусто"),14));
                if(L->bDeposit) for(int32 I=0; I<3; ++I) if(auto P=Inventory->Slots[I])
                {
                    auto Row=CreateWidget<UPersonaLootRow>(PC); Row->Target=Inventory->LootTarget; Row->Part=Inventory->LootPart; Row->Index=I; Row->ExpectedId=P->PersistentItemId; Row->bDeposit=true; Row->SetLabel(FString::Printf(TEXT("Положить: %s ×%d"),*P->ItemName().ToString(),P->Quantity)); LootBox->AddChild(Row);
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
void UPersonaPanel::AcceptTrial()
{
    if(!SelectedTrial.IsNone()) if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerAcceptTrial(SelectedTrial);
}
void UPersonaPanel::CloseTerminal() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) { PC->CloseOverlays(); } }
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

void UPersonaTrialRow::NativeOnInitialized()
{
    Super::NativeOnInitialized();auto Button=WidgetTree->ConstructWidget<UButton>();WidgetTree->RootWidget=Button;
    Button->SetBackgroundColor(FLinearColor(.08f,.09f,.09f));Label=Text(WidgetTree,TEXT(""),16);Label->SetAutoWrapText(true);Button->AddChild(Label);
    Button->OnClicked.AddDynamic(this,&ThisClass::Choose);
}
void UPersonaTrialRow::Setup(UPersonaPanel* InPanel,FName Id,const FString& Value) { Panel=InPanel;TrialId=Id;Label->SetText(FText::FromString(Value)); }
void UPersonaTrialRow::Choose() { if(Panel) Panel->SelectTrial(TrialId); }
void UPersonaPanel::SearchTrials(const FText&) { RebuildTrials(); }
void UPersonaPanel::SelectTrial(FName Id) { SelectedTrial=Id;RebuildTrials(); }
void UPersonaPanel::RebuildTrials()
{
    auto D=APersonaTrialDirector::Get(GetWorld());if(!D || !TrialList) return;
    TrialList->ClearChildren();TrialIds.Reset();
    const FString Query=UPersonaItemCatalog::SearchKey(TrialSearch->GetText().ToString());
    for(auto Trial:D->Trials) if(Trial)
    {
        FString Keywords=Trial->DisplayName.ToString();
        for(const auto* Finds:{&Trial->Finds,&Trial->Requirements}) for(const auto& Find:*Finds)
            if(auto Item=Find.Item.LoadSynchronous()) Keywords+=TEXT(" ")+Item->DisplayName.ToString();
        if(Query.IsEmpty() || UPersonaItemCatalog::SearchKey(Keywords).Contains(Query)) TrialIds.Add(Trial->TrialId);
    }
    if(!TrialIds.Contains(SelectedTrial)) SelectedTrial=TrialIds.IsEmpty() ? NAME_None : TrialIds[0];
    for(const auto& Id:TrialIds) if(auto Trial=D->FindTrial(Id))
    {
        auto Row=CreateWidget<UPersonaTrialRow>(GetOwningPlayer());Row->Setup(this,Id,(Id==SelectedTrial ? TEXT("● ") : TEXT(""))+Trial->DisplayName.ToString());
        TrialList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,8));
    }
    RebuildFinds();
}
void UPersonaPanel::RebuildFinds()
{
    FindsRow->ClearChildren();RequirementsRow->ClearChildren();StartingItems->SetText(FText::GetEmpty());
    auto D=APersonaTrialDirector::Get(GetWorld());auto Trial=D ? D->FindTrial(SelectedTrial) : nullptr;if(!Trial) return;
    auto Add=[&](UWrapBox* Row,const FPersonaTrialFind& Find,bool Required)
    {
        auto Item=Find.Item.LoadSynchronous();if(!Item) return;
        auto Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(68);Box->SetHeightOverride(68);
        auto Border=WidgetTree->ConstructWidget<UBorder>();Border->SetPadding(FMargin(4));Border->SetBrushColor(FLinearColor(.12f,.13f,.13f));Box->SetContent(Border);
        if(Item->Icon) { auto Icon=WidgetTree->ConstructWidget<UImage>();Icon->SetBrushFromTexture(Item->Icon);Border->SetContent(Icon); }
        else Border->SetContent(Text(WidgetTree,Item->DisplayName.ToString(),12));
        FString Tip=Item->DisplayName.ToString()+(Required ? TEXT("\nТребуется для входа.") : TEXT(""));
        const FString Props=UPersonaItemCatalog::Properties(*Item);if(!Props.IsEmpty()) Tip+=TEXT("\n")+Props;
        Box->SetToolTipText(FText::FromString(Tip));Row->AddChildToWrapBox(Box);
    };
    for(const auto& Find:Trial->Finds) Add(FindsRow,Find,false);
    for(const auto& Find:Trial->Requirements) Add(RequirementsRow,Find,true);
    if(SelectedTrial==PersonaForest::TrialId) StartingItems->SetText(FText::FromString(TEXT("Стартовое снаряжение: бумажная карта вылазки.\nОбязательных предметов для входа нет.")));
    else if(Trial->Requirements.IsEmpty()) StartingItems->SetText(FText::FromString(TEXT("Обязательных предметов для входа нет.")));
}
void UPersonaPanel::SearchItems(const FText& Value)
{
    if(!ItemList) return;ItemList->ClearOptions();ItemPaths.Reset();
    if(auto Catalog=UPersonaItemCatalog::Get()) for(const auto& Ref:Catalog->Items)
        if(auto Item=Ref.LoadSynchronous();Item && UPersonaItemCatalog::SearchKey(Item->DisplayName.ToString()).Contains(UPersonaItemCatalog::SearchKey(Value.ToString())))
        { ItemPaths.Add(Ref.ToSoftObjectPath().ToString());ItemList->AddOption(Item->DisplayName.ToString()); }
    if(!ItemPaths.IsEmpty()) ItemList->SetSelectedIndex(0);
}
void UPersonaPanel::GiveItem()
{
    const int32 Index=ItemList->GetSelectedIndex();if(ItemPaths.IsValidIndex(Index)) if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerGiveItem(ItemPaths[Index],FMath::RoundToInt(ItemQuantity->GetValue()));
}
void UPersonaPanel::GiveAmmo()
{
    const int32 Index=ItemList->GetSelectedIndex();if(!ItemPaths.IsValidIndex(Index)) return;
    auto Catalog=UPersonaItemCatalog::Get();auto Item=Catalog ? Catalog->Find(ItemPaths[Index]) : nullptr;
    if(Item && !Item->Ammunition.IsNull()) if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerGiveItem(Item->Ammunition.ToSoftObjectPath().ToString(),FMath::RoundToInt(ItemQuantity->GetValue()));
}
void UPersonaPanel::CommitConsole(const FText&,ETextCommit::Type Method) { if(Method==ETextCommit::OnEnter) RunConsole(); }
void UPersonaPanel::RunConsole()
{
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
    auto PC=Cast<APersonaPlayerController>(GetOwningPlayer());if(!PC || !PC->IsHost()) return;
    const FString Command=ConsoleInput->GetText().ToString().TrimStartAndEnd();if(Command.IsEmpty()) return;
    ConsoleHistory.Add(Command);if(ConsoleHistory.Num()>50) ConsoleHistory.RemoveAt(0);HistoryIndex=ConsoleHistory.Num();
    const FString Result=PC->ConsoleCommand(Command,false);
    ConsoleOutput->SetText(FText::FromString((ConsoleOutput->GetText().ToString()+TEXT("\n> ")+Command+TEXT("\n")+Result).Right(4000)));
    ConsoleInput->SetText(FText::GetEmpty());ConsoleInput->SetKeyboardFocus();
}
void UPersonaSectorRow::NativeOnInitialized()
{
    Super::NativeOnInitialized();auto B=WidgetTree->ConstructWidget<UButton>();WidgetTree->RootWidget=B;B->SetBackgroundColor(FLinearColor(.08f,.09f,.1f));
    Label=Text(WidgetTree,TEXT(""),16);B->AddChild(Label);B->OnClicked.AddDynamic(this,&ThisClass::Choose);
}
void UPersonaSectorRow::Setup(int32 In,const FString& Title) { Index=In;Label->SetText(FText::FromString(Title)); }
void UPersonaSectorRow::Choose() { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Challenge(Index); }
void UPersonaPanel::ShowCoop() { bBattlePage=false;CoopBox->SetVisibility(ESlateVisibility::Visible);BattleBox->SetVisibility(ESlateVisibility::Collapsed); }
void UPersonaPanel::ShowBattle() { bBattlePage=true;CoopBox->SetVisibility(ESlateVisibility::Collapsed);BattleBox->SetVisibility(ESlateVisibility::Visible);UpdateBattle(); }
void UPersonaPanel::ToggleBattleOpen() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) { auto S=APersonaBattleState::Get(GetWorld());PC->ServerBattleOpen(!S || !S->Open); } }
void UPersonaPanel::FindOpponents() { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Find(false); }
void UPersonaPanel::FindFriendOpponents() { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Find(true); }
void UPersonaPanel::AcceptBattle() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerBattleAnswer(true); }
void UPersonaPanel::CancelBattle() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) PC->ServerBattleAnswer(false); }
void UPersonaPanel::ReadyBattle() { if(auto PC=Cast<APersonaPlayerController>(GetOwningPlayer())) if(auto P=PC->GetPlayerState<APersonaPlayerState>()) PC->ServerBattleReady(!P->bBattleReady); }
void UPersonaPanel::UpdateBattle()
{
    if(!BattleBox) return;auto PC=Cast<APersonaPlayerController>(GetOwningPlayer());auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();auto S=APersonaBattleState::Get(GetWorld());
    if(!PC || !S) return;
    const bool Host=PC->IsHost(),Offered=S->Phase==EPersonaBattlePhase::Offered,Gathering=S->Phase==EPersonaBattlePhase::Gathering;
    FString Description=TEXT("1×1 — 4×4 · Огонь по своим включён\n")+S->Status;
    if(!S->Opponent.IsEmpty()) Description+=TEXT("\nСоперник: ")+S->Opponent;
    if(Gathering || S->Phase==EPersonaBattlePhase::Prepared) Description+=FString::Printf(TEXT("\nГотовность вашей группы: %d/%d. После подтверждения соберитесь на лифте."),S->Ready,S->GroupSize);
    if(!Host) Description+=TEXT("\nВызовами управляет организатор сектора.");BattleStatus->SetText(FText::FromString(Description));
    BattleOpenButton->SetIsEnabled(Host && S->Phase==EPersonaBattlePhase::Idle);
    CastChecked<UTextBlock>(BattleOpenButton->GetChildAt(0))->SetText(FText::FromString(S->Open ? TEXT("Закрыть приём вызовов") : TEXT("Открыть сектор для вызовов")));
    BattleAcceptButton->SetIsEnabled(Host && Offered && B->Ticket.Master!=B->Owner());
    BattleCancelButton->SetIsEnabled(Host && (Offered || Gathering || S->Phase==EPersonaBattlePhase::Prepared));BattleReadyButton->SetIsEnabled(Gathering);
    auto PS=PC->GetPlayerState<APersonaPlayerState>();CastChecked<UTextBlock>(BattleReadyButton->GetChildAt(0))->SetText(FText::FromString(PS && PS->bBattleReady ? TEXT("Снять готовность") : TEXT("Готов к отправлению")));
    FString Signature;for(const auto& L:B->Listings) Signature+=L.Owner+L.Title+FString::FromInt(L.Players)+(L.Friend ? TEXT("F") : TEXT(""));
    if(Signature!=LastBattleList)
    {
        LastBattleList=Signature;BattleList->ClearChildren();int32 Index=0;
        for(const auto& L:B->Listings)
        { auto Row=CreateWidget<UPersonaSectorRow>(PC);Row->Setup(Index++,FString::Printf(TEXT("%s · %d участн. %s · Вызвать"),*L.Title,L.Players,L.Friend ? TEXT("· друг") : TEXT("")));Row->SetIsEnabled(Host && S->Phase==EPersonaBattlePhase::Idle);BattleList->AddChild(Row); }
    }
}

int32 UPersonaPanel::DrawFieldMap(const APersonaTrialDirector& D,const APersonaCharacter& C,const FGeometry& G,FSlateWindowElementList& List,int32 Layer) const
{
    const FVector2D Size=G.GetLocalSize(), Sheet(Size.X*.66,Size.Y*.66);
    const FVector2D Corner((Size.X-Sheet.X)*.5,Size.Y*.21);
    const auto Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    auto Box=[&](FVector2D XY,FVector2D Ext,FLinearColor Ink,int Z=0)
    { FSlateDrawElement::MakeBox(List,Layer+Z,G.ToPaintGeometry(Ext,FSlateLayoutTransform(XY)),Brush,ESlateDrawEffect::None,Ink); };
    auto Label=[&](FVector2D XY,const FString& S,int Pt)
    { FSlateDrawElement::MakeText(List,Layer+5,G.ToPaintGeometry(FVector2D(Sheet.X,35),FSlateLayoutTransform(XY)),S,FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Pt),ESlateDrawEffect::None,FLinearColor(.09f,.085f,.06f)); };
    Box(Corner+FVector2D(6,9),Sheet,FLinearColor(0,0,0,.65f));Box(Corner,Sheet,FLinearColor(.68f,.64f,.52f),1);
    FBox2D Bound(ForceInit); Bound+=FVector2D::ZeroVector; Bound+=FVector2D(C.GetActorLocation());
    for(const auto& T:D.NorthAtlas.Tiles)
    { const FVector2D O(PersonaNorth::Origin(T.Tile)); Bound+=O; Bound+=O+FVector2D(PersonaNorth::TileSize); }
    const auto Ext=Bound.GetSize();
    const double Scale=FMath::Min((Sheet.X-74)/FMath::Max(3600.,Ext.X),(Sheet.Y-108)/FMath::Max(3600.,Ext.Y));
    auto Project=[&](FVector2D World){ auto V=(World-Bound.GetCenter())*Scale; return Corner+Sheet*.5+FVector2D(V.X,-V.Y); };
    const double Cell=PersonaNorth::TileSize/8.;
    for(const auto& T:D.NorthAtlas.Tiles)
    {
        const FVector2D O(PersonaNorth::Origin(T.Tile));
        for(int I=0;I<64;++I) if(T.Seen&(uint64(1)<<I))
        {
            const FVector2D At=O+FVector2D(I%8*Cell,(I/8+1)*Cell);
            float Shade=PersonaNorth::Height(D.Run.Seed,At.X,At.Y)<-100 ? .28f : .39f;
            Box(Project(At),FVector2D(Cell*Scale+1),FLinearColor(Shade,Shade+.025f,Shade-.05f),2);
        }
        if(T.Landmark!=ENorthSite::None)
        {
            ENorthSite Kind; const FVector2D Pos=Project(FVector2D(PersonaNorth::Site(D.Run.Seed,T.Tile,Kind)));
            Box(Pos-FVector2D(2),FVector2D(5),FLinearColor(.18f,.10f,.04f),3);
            Label(Pos+FVector2D(4,-12),PersonaNorth::SiteName(T.Landmark),9);
        }
    }
    const auto Home=Project(FVector2D::ZeroVector),You=Project(FVector2D(C.GetActorLocation()));
    Box(Home-FVector2D(6),FVector2D(12),FLinearColor(.12f,.11f,.09f),3);
    Box(You-FVector2D(3),FVector2D(7),FLinearColor(.46f,.08f,.04f),4);
    Label(Corner+FVector2D(19,9),TEXT("ПОЛЕВАЯ КАРТА   /   СЕВЕР ↑"),16);
    Label(Corner+FVector2D(19,Sheet.Y-30),TEXT("■ Лифт      • Ваше положение      M — сложить"),12);
    return Layer+5;
}
