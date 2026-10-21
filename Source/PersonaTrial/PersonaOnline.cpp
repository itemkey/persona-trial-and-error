#include "PersonaOnline.h"
#include "PersonaFramework.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Kismet/GameplayStatics.h"

namespace PersonaSession
{
    const FName Name = NAME_GameSession;
    const FName ProjectKey(TEXT("PERSONA_PROJECT"));
    const FName VersionKey(TEXT("PERSONA_VERSION"));
    const FString Project(TEXT("persona-trial-and-error"));
    const FString Version(TEXT("0.1.0"));
}
void UPersonaOnline::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get()) Sessions = OSS->GetSessionInterface();
    if (!Sessions.IsValid()) { SetStatus(TEXT("Online service unavailable. Start Steam, or use the Local launch script.")); return; }
    CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
    FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::Found));
    JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
    DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::Destroyed));
    InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::Invited));
}
void UPersonaOnline::Deinitialize()
{
    if (Sessions.IsValid())
    {
        Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
        Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
        Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
        Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
        Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
    }
    Super::Deinitialize();
}
bool UPersonaOnline::IsSteam() const
{
    const IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    return OSS && OSS->GetSubsystemName() == FName(TEXT("STEAM"));
}
void UPersonaOnline::SetStatus(const FString& Text)
{
    if (UPersonaGameInstance* GI = Cast<UPersonaGameInstance>(GetGameInstance())) GI->Status = Text;
    UE_LOG(LogTemp, Display, TEXT("PERSONA ONLINE: %s"), *Text);
}
void UPersonaOnline::Host()
{
    if (bBusy || !Sessions.IsValid()) return;
    bBusy = true;
    if (Sessions->GetNamedSession(PersonaSession::Name))
    {
        bHostAfterDestroy = true;
        if (!Sessions->DestroySession(PersonaSession::Name)) { bBusy = bHostAfterDestroy = false; SetStatus(TEXT("Unable to clear previous session.")); }
        return;
    }
    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = 4;
    Settings.bIsLANMatch = !IsSteam();
    Settings.bShouldAdvertise = true;
    Settings.bAllowJoinInProgress = true;
    Settings.bAllowInvites = true;
    Settings.bUsesPresence = IsSteam();
    Settings.bUseLobbiesIfAvailable = IsSteam();
    Settings.bAllowJoinViaPresence = IsSteam();
    Settings.Set(PersonaSession::ProjectKey, PersonaSession::Project, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::VersionKey, PersonaSession::Version, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    SetStatus(TEXT("Creating four-player session..."));
    if (!Sessions->CreateSession(0, PersonaSession::Name, Settings)) { bBusy = false; SetStatus(TEXT("Create session failed.")); }
}
void UPersonaOnline::Created(FName Name, bool Success)
{
    bBusy = false;
    if (!Success) { SetStatus(TEXT("Create session failed. Check Steam login and connection.")); return; }
    SetStatus(TEXT("Session created. Invite friends or enter the trial."));
    UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/Persona/Maps/L_Hub"), true, TEXT("listen"));
}
void UPersonaOnline::Find()
{
    if (bBusy || !Sessions.IsValid()) return;
    bBusy = true; Results.Reset();
    Search = MakeShared<FOnlineSessionSearch>();
    Search->bIsLanQuery = !IsSteam();
    Search->MaxSearchResults = 200;
    Search->TimeoutInSeconds = 20;
    if (IsSteam()) Search->QuerySettings.Set(FName(TEXT("LOBBYSEARCH")), true, EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(PersonaSession::ProjectKey, PersonaSession::Project, EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(PersonaSession::VersionKey, PersonaSession::Version, EOnlineComparisonOp::Equals);
    SetStatus(TEXT("Searching compatible PERSONA sessions..."));
    if (!Sessions->FindSessions(0, Search.ToSharedRef())) { bBusy = false; SetStatus(TEXT("Search could not start.")); }
}
void UPersonaOnline::Found(bool Success)
{
    bBusy = false;
    if (!Success || !Search.IsValid()) { SetStatus(TEXT("Search failed.")); return; }
    Search->SearchResults.RemoveAll([](const FOnlineSessionSearchResult& R)
    {
        FString Project, Version;
        return !R.Session.SessionSettings.Get(PersonaSession::ProjectKey, Project) || Project != PersonaSession::Project
            || !R.Session.SessionSettings.Get(PersonaSession::VersionKey, Version) || Version != PersonaSession::Version;
    });
    for (const auto& R : Search->SearchResults)
        Results.Add(FString::Printf(TEXT("%s | %d/4 | %d ms"), *R.Session.OwningUserName, 4-R.Session.NumOpenPublicConnections, R.PingInMs));
    SetStatus(FString::Printf(TEXT("Found %d compatible session(s)."), Results.Num()));
}
void UPersonaOnline::Join(int32 Index)
{
    if (Search.IsValid() && Search->SearchResults.IsValidIndex(Index)) JoinResult(Search->SearchResults[Index]);
}
void UPersonaOnline::JoinResult(const FOnlineSessionSearchResult& Result)
{
    if (bBusy || !Sessions.IsValid()) return;
    FString Project, Version;
    if (!Result.Session.SessionSettings.Get(PersonaSession::ProjectKey, Project) || Project != PersonaSession::Project
        || !Result.Session.SessionSettings.Get(PersonaSession::VersionKey, Version) || Version != PersonaSession::Version)
    { SetStatus(TEXT("Invite is for another project or build.")); return; }
    if (Sessions->GetNamedSession(PersonaSession::Name)) { SetStatus(TEXT("Leave the current session before joining another.")); return; }
    bBusy = true; SetStatus(TEXT("Joining session..."));
    if (!Sessions->JoinSession(0, PersonaSession::Name, Result)) { bBusy = false; SetStatus(TEXT("Join could not start.")); }
}
void UPersonaOnline::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    bBusy = false;
    FString Address;
    if (Result != EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(Name, Address))
    { SetStatus(TEXT("Join failed: session full, closed or unreachable.")); ClearSessionAfterFailure(); return; }
    SetStatus(TEXT("Joined session."));
    if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController()) PC->ClientTravel(Address, TRAVEL_Absolute);
}
void UPersonaOnline::Invited(bool Success, int32 ControllerId, TSharedPtr<const FUniqueNetId> User, const FOnlineSessionSearchResult& Result)
{
    if (Success) JoinResult(Result);
}
void UPersonaOnline::Leave()
{
    if (bBusy) return;
    if (Sessions.IsValid() && Sessions->GetNamedSession(PersonaSession::Name))
    {
        bBusy = bReturnAfterDestroy = true;
        if (!Sessions->DestroySession(PersonaSession::Name)) Destroyed(PersonaSession::Name, false);
    }
    else
    {
        SetStatus(TEXT("Session ended. Create or find a session to play together."));
        UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/Persona/Maps/L_Hub"));
    }
}
void UPersonaOnline::Destroyed(FName Name, bool Success)
{
    bBusy = false;
    if (bHostAfterDestroy) { bHostAfterDestroy = false; if (Success) Host(); return; }
    if (bReturnAfterDestroy)
    {
        bReturnAfterDestroy = false;
        SetStatus(TEXT("Session ended. Create or find a session to play together."));
        UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/Persona/Maps/L_Hub"));
    }
}
void UPersonaOnline::ClearSessionAfterFailure()
{
    bBusy = bHostAfterDestroy = bReturnAfterDestroy = false;
    if (Sessions.IsValid() && Sessions->GetNamedSession(PersonaSession::Name)) Sessions->DestroySession(PersonaSession::Name);
}
void UPersonaOnline::InviteFriends()
{
    if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
        if (auto UI = OSS->GetExternalUIInterface(); UI.IsValid()) UI->ShowInviteUI(0, PersonaSession::Name);
}
