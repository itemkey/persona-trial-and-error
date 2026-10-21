#include "PersonaPanel.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaInteraction.h"
#include "PersonaOnline.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
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
    Menu->AddChild(Text(WidgetTree, TEXT("TRIAL SYSTEM / FOUNDATION 0.1"), 12));
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
    Menu->AddChild(Text(WidgetTree, TEXT("TAB - close panel\nWASD / mouse - move / look\nSHIFT - run | CTRL - crouch | SPACE - jump\nE - use / drop | Q - throw | V - voice"), 13));
    Vitals = Text(WidgetTree, TEXT(""), 16); auto VS = Canvas->AddChildToCanvas(Vitals); VS->SetAnchors(FAnchors(0,1)); VS->SetPosition(FVector2D(30,-60)); VS->SetAutoSize(true);
    Prompt = Text(WidgetTree, TEXT("+"), 16); auto PS = Canvas->AddChildToCanvas(Prompt); PS->SetAnchors(FAnchors(.5f,.5f)); PS->SetAlignment(FVector2D(.5f,.5f)); PS->SetAutoSize(true);
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
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
void UPersonaPanel::NativeTick(const FGeometry& Geometry, float Delta)
{
    Super::NativeTick(Geometry, Delta);
    auto PC = Cast<APersonaPlayerController>(GetOwningPlayer()); if (!PC) return;
    auto GI = Cast<UPersonaGameInstance>(GetGameInstance()); auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>();
    auto GS = GetWorld()->GetGameState<APersonaGameState>();
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
        Prompt->SetText(C->Interaction->Held ? FText::FromString(TEXT("E - drop | Q - throw")) :
            Target && Target->Implements<UPersonaInteractable>() ? IPersonaInteractable::Execute_InteractionLabel(Target) : FText::FromString(TEXT("+")));
    }
}
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
