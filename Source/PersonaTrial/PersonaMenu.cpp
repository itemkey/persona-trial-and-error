#include "PersonaMenu.h"
#include "PersonaFramework.h"
#include "PersonaOnline.h"
#include "PersonaSave.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
const FLinearColor Ink(.035f,.036f,.034f), Paper(.69f,.68f,.64f);
UTextBlock* Print(UWidgetTree* Tree, const FString& Text, int32 Size, bool Bold = false)
{
    auto T = Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Text)); T->SetColorAndOpacity(Ink);
    FSlateFontInfo Font = T->GetFont(); Font.Size = Size; Font.TypefaceFontName = Bold ? TEXT("Bold") : TEXT("Regular"); T->SetFont(Font);
    T->SetAutoWrapText(true); return T;
}
void Line(UWidgetTree* Tree, UVerticalBox* Box, float Height = 1)
{
    auto Size = Tree->ConstructWidget<USizeBox>(); Size->SetHeightOverride(Height);
    auto B = Tree->ConstructWidget<UBorder>(); B->SetBrushColor(Ink); Size->AddChild(B);
    Box->AddChildToVerticalBox(Size)->SetPadding(FMargin(0,12));
}
}
void UPersonaMenuAction::Click() { if (Menu) Menu->Activate(Operation,Index); }
void UPersonaMenu::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetIsFocusable(true);
    auto Background = WidgetTree->ConstructWidget<UBorder>(); WidgetTree->RootWidget = Background;
    Background->SetBrushColor(FLinearColor(.018f,.020f,.022f)); Background->SetPadding(FMargin(45,28));
    auto Scale = WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit); Background->AddChild(Scale);
    auto PageSize = WidgetTree->ConstructWidget<USizeBox>(); PageSize->SetWidthOverride(1160); PageSize->SetHeightOverride(880); Scale->AddChild(PageSize);
    auto Sheet = WidgetTree->ConstructWidget<UBorder>(); Sheet->SetBrushColor(Paper); Sheet->SetPadding(FMargin(48,35)); PageSize->AddChild(Sheet);
    auto Page = WidgetTree->ConstructWidget<UVerticalBox>(); Sheet->AddChild(Page);
    Page->AddChildToVerticalBox(Print(WidgetTree,TEXT("УПРАВЛЕНИЕ ЭКСПЕРИМЕНТАЛЬНЫХ ПРОЦЕДУР     /     ВНУТРЕННИЙ ДОКУМЕНТ"),13,true));
    Line(WidgetTree,Page,3);
    auto Columns = WidgetTree->ConstructWidget<UHorizontalBox>(); Page->AddChildToVerticalBox(Columns)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto LeftSize = WidgetTree->ConstructWidget<USizeBox>(); LeftSize->SetWidthOverride(260); Columns->AddChildToHorizontalBox(LeftSize)->SetPadding(FMargin(0,12,45,0));
    auto Left = WidgetTree->ConstructWidget<UVerticalBox>(); LeftSize->AddChild(Left);
    Left->AddChildToVerticalBox(Print(WidgetTree,TEXT("PERSONA"),43,true));
    Left->AddChildToVerticalBox(Print(WidgetTree,TEXT("TRIAL AND ERROR"),13))->SetPadding(FMargin(2,0,0,48));
    Left->AddChildToVerticalBox(Print(WidgetTree,TEXT("ЛИЧНОЕ\nДОСЬЕ"),35,true));
    Line(WidgetTree,Left);
    Left->AddChildToVerticalBox(Print(WidgetTree,TEXT("ОБЪЕКТ НАБЛЮДЕНИЯ\nЭкспериментальный субъект\n\nДОПУСК\nПервичная процедура\n\nПРЕДЕЛ ГРУППЫ\n04 субъекта"),16));
    auto Stamp = WidgetTree->ConstructWidget<UBorder>(); Stamp->SetBrushColor(Ink); Stamp->SetPadding(FMargin(14,10));
    auto StampText = Print(WidgetTree,TEXT("ДЕЛО ОТКРЫТО"),20,true); StampText->SetColorAndOpacity(Paper); Stamp->AddChild(StampText);
    Left->AddChildToVerticalBox(Stamp)->SetPadding(FMargin(0,36,24,0));
    Left->AddChildToVerticalBox(Print(WidgetTree,TEXT("СОХРАНЯТЬ ДО ЗАВЕРШЕНИЯ\nВСЕХ НАЗНАЧЕННЫХ ПРОЦЕДУР"),11))->SetPadding(FMargin(0,18));
    auto Right = WidgetTree->ConstructWidget<UVerticalBox>(); Columns->AddChildToHorizontalBox(Right)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Right->AddChildToVerticalBox(Print(WidgetTree,TEXT("ЛИСТ НАЗНАЧЕНИЙ"),15,true))->SetPadding(FMargin(0,18,0,15));
    auto Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Right->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Contents = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Contents);
    Status = Print(WidgetTree,TEXT(""),13); Right->AddChildToVerticalBox(Status)->SetPadding(FMargin(4,18,0,0));
    Line(WidgetTree,Page);
    Page->AddChildToVerticalBox(Print(WidgetTree,TEXT("ФОРМА П-01     /     ПОДПИСЬ СУБЪЕКТА НЕ ТРЕБУЕТСЯ                                               PERSONA  ·  0.4"),12));
    Show(bInGame ? TEXT("pause") : TEXT("main"));
}
void UPersonaMenu::NativeConstruct()
{
    Super::NativeConstruct(); Show(bInGame ? TEXT("pause") : TEXT("main"));
}
void UPersonaMenu::AddAction(const FString& Label, const FString& Operation, bool Enabled, int32 Index)
{
    auto B = WidgetTree->ConstructWidget<UButton>(); B->SetBackgroundColor(FLinearColor(1,1,1,0));
    FButtonStyle Style = B->GetStyle();
    Style.Normal.TintColor = FSlateColor(FLinearColor(0,0,0,0));
    Style.Hovered.TintColor = FSlateColor(FLinearColor(.48f,.47f,.43f));
    Style.Pressed.TintColor = FSlateColor(FLinearColor(.38f,.37f,.33f));
    Style.Disabled.TintColor = FSlateColor(FLinearColor(0,0,0,0)); B->SetStyle(Style); B->SetBackgroundColor(FLinearColor::White);
    auto LabelText = Print(WidgetTree,Label,20,true); auto ButtonSlot = Cast<UButtonSlot>(B->AddChild(LabelText));
    ButtonSlot->SetPadding(FMargin(16,14)); ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
    B->SetIsEnabled(Enabled); B->SetRenderOpacity(Enabled ? 1.f : .32f);
    if (!Enabled) B->SetToolTipText(FText::FromString(TEXT("Раздел ещё не доступен")));
    Contents->AddChildToVerticalBox(B)->SetPadding(FMargin(0,2));
    auto Action = NewObject<UPersonaMenuAction>(this); Action->Menu = this; Action->Operation = Operation; Action->Index = Index;
    Actions.Add(Action); B->OnClicked.AddDynamic(Action,&UPersonaMenuAction::Click);
}
void UPersonaMenu::Show(const FString& Page)
{
    CurrentPage = Page; Contents->ClearChildren(); Actions.Reset();
    if (Page == TEXT("main"))
    {
        AddAction(TEXT("Направление в новый сектор"),TEXT("new"));
        AddAction(TEXT("Проследовать в личный сектор"),TEXT("continue"));
        AddAction(TEXT("Перевод в действующий сектор"),TEXT("find"));
        AddAction(TEXT("Индивидуальная калибровка"),TEXT(""),false);
        AddAction(TEXT("Досье экспериментального субъекта"),TEXT(""),false);
        AddAction(TEXT("Обязательный инструктаж"),TEXT(""),false);
        AddAction(TEXT("Отчёт процедур"),TEXT(""),false);
        AddAction(TEXT("Прекратить процедуры"),TEXT("quit"));
    }
    else if (Page == TEXT("new"))
    {
        Contents->AddChildToVerticalBox(Print(WidgetTree,TEXT("НОВОЕ НАЗНАЧЕНИЕ\n\nБудет создан отдельный сектор. Состояние мира и участников хранится у организатора."),19));
        AddAction(TEXT("Совместное испытание · до 4 участников"),TEXT("create"),true,0);
        AddAction(TEXT("Встречное испытание · 2 × 2"),TEXT("create"),true,1);
    }
    else if (Page == TEXT("continue"))
    {
        SectorSlots.Reset(); const auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->ListSectors();
        if (Saves.IsEmpty()) Contents->AddChildToVerticalBox(Print(WidgetTree,TEXT("Архив пуст. Получите направление в новый сектор."),19));
        for (auto S : Saves)
        {
            SectorSlots.Add(S->Slot);
            AddAction(S->Title + TEXT("\n") + (S->CurrentMap.Contains(TEXT("TestChamber")) ? TEXT("Испытательный зал") : TEXT("Личный сектор"))
                + TEXT("  ·  ") + S->Updated.ToString(TEXT("%d.%m.%Y %H:%M UTC")),TEXT("load"),true,SectorSlots.Num()-1);
        }
    }
    else if (Page == TEXT("find"))
    {
        auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>(); ResultCount = Online->Results.Num();
        Contents->AddChildToVerticalBox(Print(WidgetTree,Online->IsSteam() ? TEXT("ДЕЙСТВУЮЩИЕ СЕКТОРЫ / STEAM") : TEXT("ДЕЙСТВУЮЩИЕ СЕКТОРЫ / ЛОКАЛЬНАЯ СЕТЬ"),17,true));
        AddAction(TEXT("Повторить поиск"),TEXT("refresh"));
        for (int32 I=0; I<Online->Results.Num(); ++I) AddAction(Online->Results[I],TEXT("join"),true,I);
    }
    else if (Page == TEXT("pause"))
    {
        Contents->AddChildToVerticalBox(Print(WidgetTree,TEXT("ПРОЦЕДУРА ВЫПОЛНЯЕТСЯ\n\nМир продолжает работать, пока открыто это меню."),19));
        AddAction(TEXT("Продолжить процедуру"),TEXT("resume"));
        AddAction(TEXT("Вернуться к листу назначений"),TEXT("leave"));
        AddAction(TEXT("Прекратить процедуры"),TEXT("quit"));
        Contents->AddChildToVerticalBox(Print(WidgetTree,TEXT("Организатор сохраняет сектор при выходе. При выходе участника его тело остаётся в мире."),14));
    }
    if (Page != TEXT("main") && Page != TEXT("pause")) AddAction(TEXT("← Вернуться к назначениям"),TEXT("main"));
}
void UPersonaMenu::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>();
    if (Status) Status->SetText(FText::FromString(CastChecked<UPersonaGameInstance>(GetGameInstance())->Status));
    if (CurrentPage == TEXT("find") && ResultCount != Online->Results.Num()) Show(TEXT("find"));
    Contents->SetIsEnabled(!Online->bBusy);
}
void UPersonaMenu::Activate(const FString& Op,int32 Index)
{
    auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>(); auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (Online->bBusy) return;
    if (Op == TEXT("create")) { if (Saves->NewSector(Index==1)) Online->Host(); }
    else if (Op == TEXT("load")) { if (SectorSlots.IsValidIndex(Index) && Saves->SelectSector(SectorSlots[Index])) Online->Host(); }
    else if (Op == TEXT("find") || Op == TEXT("refresh")) { Online->Find(); Show(TEXT("find")); }
    else if (Op == TEXT("join")) Online->Join(Index);
    else if (Op == TEXT("leave")) Online->Leave();
    else if (Op == TEXT("resume")) { if (auto PC = Cast<APersonaPlayerController>(GetOwningPlayer())) PC->TogglePauseMenu(); }
    else if (Op == TEXT("quit")) { if (Saves->SaveWorld()) UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false); }
    else Show(Op);
    if (!Saves->LastError.IsEmpty()) CastChecked<UPersonaGameInstance>(GetGameInstance())->Status = Saves->LastError;
}
void APersonaMenuController::BeginPlay()
{
    Super::BeginPlay(); if (!IsLocalController()) return;
    Menu = CreateWidget<UPersonaMenu>(this); Menu->AddToViewport(); bShowMouseCursor = true;
    FInputModeUIOnly Input; Input.SetWidgetToFocus(Menu->TakeWidget()); SetInputMode(Input);
}
APersonaMenuMode::APersonaMenuMode()
{
    PlayerControllerClass = APersonaMenuController::StaticClass(); DefaultPawnClass = nullptr; HUDClass = nullptr;
}
