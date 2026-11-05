#include "PersonaInput.h"
#include "PersonaFramework.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
bool AdminKey(const FKeyEvent& E)
{
    // Windows VK_OEM_3 is unchanged by EN/RU layout; Tilde covers other hosts.
    return E.GetKey()==EKeys::Tilde || E.GetKeyCode()==0xC0;
}
APersonaPlayerController* LocalController()
{
    auto W=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    return W ? Cast<APersonaPlayerController>(W->GetFirstPlayerController()) : nullptr;
}
}
bool FPersonaInput::HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E)
{
    auto PC=LocalController(); if(!PC || !PC->IsLocalController()) return false;
    if(AdminKey(E) && PC->IsHost())
    {
        if(!E.IsRepeat() && !bAdminDown) PC->TogglePanel();
        bAdminDown=true; return true;
    }
    if(E.GetKey()==EKeys::Tab && !PC->IsPanelOpen() && !PC->IsPauseOpen())
    { if(!E.IsRepeat()) PC->HandleTab(); return true; }
    return false;
}
bool FPersonaInput::HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& E)
{
    if(!AdminKey(E)) return false;
    const bool Consumed=bAdminDown; bAdminDown=false; return Consumed;
}
