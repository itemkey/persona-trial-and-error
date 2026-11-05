#include "PersonaOnline.h"
#include "PersonaFramework.h"
#include "PersonaSave.h"
#include "PersonaBattle.h"
#include "Engine/NetDriver.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Kismet/GameplayStatics.h"

namespace PersonaSession
{
    const FName Name = NAME_GameSession;
    const FName DirectoryName(TEXT("PersonaBattleDirectory"));
    const FName ProjectKey(TEXT("PERSONA_PROJECT"));
    const FName VersionKey(TEXT("PERSONA_VERSION"));
    const FString Project(TEXT("persona-trial-and-error"));
    const FString Version(TEXT("0.11.0"));
    const FName PurposeKey(TEXT("PERSONA_PURPOSE")),OwnerKey(TEXT("PERSONA_OWNER")),SectorKey(TEXT("PERSONA_SECTOR"));
    const FName OpenKey(TEXT("PERSONA_BATTLE_OPEN")),PlayersKey(TEXT("PERSONA_PLAYERS")),ControlKey(TEXT("PERSONA_CONTROL")),MatchKey(TEXT("PERSONA_MATCH"));
    const FName PortKey(TEXT("PERSONA_PORT"));
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
    if(auto OSS=IOnlineSubsystem::Get()) if(auto F=OSS->GetFriendsInterface();F.IsValid()) F->ReadFriendsList(0,EFriendsLists::ToString(EFriendsLists::Default));
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
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (!Saves->SaveWorld()) { SetStatus(Saves->LastError); return; }
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
    Settings.Set(PersonaSession::PurposeKey,FString(TEXT("Hub")),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::OwnerKey,GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Owner(),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::SectorKey,Saves->Active ? Saves->Active->Slot : FString(),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::OpenKey,false,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::PlayersKey,1,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    int32 HostPort=7777;FParse::Value(FCommandLine::Get(),TEXT("port="),HostPort);Settings.Set(PersonaSession::PortKey,HostPort,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    SetStatus(TEXT("Creating four-player session..."));
    if (!Sessions->CreateSession(0, PersonaSession::Name, Settings)) { bBusy = false; SetStatus(TEXT("Create session failed.")); }
}
void UPersonaOnline::Created(FName Name, bool Success)
{
    if(Name==PersonaSession::DirectoryName)
    { if(Success && !bDirectoryWanted) Sessions->DestroySession(Name);return; }
    if(Name!=PersonaSession::Name) return;
    bBusy = false;
    if (!Success) { SetStatus(TEXT("Create session failed. Check Steam login and connection.")); return; }
    SetStatus(TEXT("Session created. Invite friends or enter the trial."));
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (!Saves->Active && !Saves->NewSector()) { SetStatus(Saves->LastError); return; }
    const FString Options = Saves->Active->Teams ? TEXT("listen?Mode=Teams") : TEXT("listen?Mode=Coop");
    UGameplayStatics::OpenLevel(GetWorld(), FName(Saves->Active->CurrentMap), true, Options);
}
void UPersonaOnline::Find()
{ SearchPurpose=ESearchPurpose::Hubs;StartSearch(); }
void UPersonaOnline::FindBattles()
{ SearchPurpose=ESearchPurpose::Opponents;StartSearch(); }
void UPersonaOnline::FindBattleMatch(const FString& Match)
{ SearchPurpose=ESearchPurpose::Match;DesiredMatch=Match;StartSearch(); }
void UPersonaOnline::FindHome(const FString& Owner,const FString& Sector)
{ SearchPurpose=ESearchPurpose::Home;DesiredOwner=Owner;DesiredSector=Sector;StartSearch(); }
void UPersonaOnline::StartSearch()
{
    if (bBusy || !Sessions.IsValid()) return;
    bBusy = true;if(SearchPurpose==ESearchPurpose::Hubs) Results.Reset();
    if(SearchPurpose==ESearchPurpose::Opponents) GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Listings.Reset();
    Search = MakeShared<FOnlineSessionSearch>();
    Search->bIsLanQuery = !IsSteam();
    Search->MaxSearchResults = 200;
    Search->TimeoutInSeconds = 20;
    if (IsSteam()) Search->QuerySettings.Set(FName(TEXT("LOBBYSEARCH")), true, EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(PersonaSession::ProjectKey, PersonaSession::Project, EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(PersonaSession::VersionKey, PersonaSession::Version, EOnlineComparisonOp::Equals);
    Search->QuerySettings.Set(PersonaSession::PurposeKey,FString(SearchPurpose==ESearchPurpose::Match ? TEXT("SectorBattle") : SearchPurpose==ESearchPurpose::Opponents ? TEXT("SectorDirectory") : TEXT("Hub")),EOnlineComparisonOp::Equals);
    if(SearchPurpose==ESearchPurpose::Match) Search->QuerySettings.Set(PersonaSession::MatchKey,DesiredMatch,EOnlineComparisonOp::Equals);
    SetStatus(TEXT("Searching compatible PERSONA sessions..."));
    if (!Sessions->FindSessions(0, Search.ToSharedRef())) { bBusy = false; SetStatus(TEXT("Search could not start.")); }
}
void UPersonaOnline::Found(bool Success)
{
    bBusy = false;
    if (!Success || !Search.IsValid()) { SetStatus(TEXT("Search failed.")); return; }
    Search->SearchResults.RemoveAll([this](const FOnlineSessionSearchResult& R)
    {
        FString Project, Version,Purpose;
        return !R.Session.SessionSettings.Get(PersonaSession::ProjectKey, Project) || Project != PersonaSession::Project
            || !R.Session.SessionSettings.Get(PersonaSession::VersionKey, Version) || Version != PersonaSession::Version
            || !R.Session.SessionSettings.Get(PersonaSession::PurposeKey,Purpose) || Purpose!=(SearchPurpose==ESearchPurpose::Match ? TEXT("SectorBattle") : SearchPurpose==ESearchPurpose::Opponents ? TEXT("SectorDirectory") : TEXT("Hub"));
    });
    auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();
    if(SearchPurpose==ESearchPurpose::Opponents)
    {
        for(const auto& R:Search->SearchResults)
        {
            bool Open=false;R.Session.SessionSettings.Get(PersonaSession::OpenKey,Open);if(!Open) continue;
            FPersonaBattleListing L;R.Session.SessionSettings.Get(PersonaSession::OwnerKey,L.Owner);R.Session.SessionSettings.Get(PersonaSession::SectorKey,L.Sector);
            R.Session.SessionSettings.Get(PersonaSession::ControlKey,L.Endpoint);R.Session.SessionSettings.Get(PersonaSession::PlayersKey,L.Players);L.Title=R.Session.OwningUserName;
            if(IsSteam() && (!R.Session.OwningUserId.IsValid() || R.Session.OwningUserId->ToString()!=L.Owner)) continue;
            if(auto OSS=IOnlineSubsystem::Get()) if(auto F=OSS->GetFriendsInterface();F.IsValid() && R.Session.OwningUserId.IsValid()) L.Friend=F->IsFriend(0,*R.Session.OwningUserId,EFriendsLists::ToString(EFriendsLists::Default));
            if(L.Owner.IsEmpty() || L.Owner==Battle->Owner() || L.Endpoint.IsEmpty() || L.Players<1 || L.Players>4 || (Battle->FriendsOnly && !L.Friend)) continue;
            Battle->Listings.Add(L);
        }
        SetStatus(FString::Printf(TEXT("Готовых к бою секторов: %d"),Battle->Listings.Num()));return;
    }
    if(SearchPurpose==ESearchPurpose::Match || SearchPurpose==ESearchPurpose::Home)
    {
        for(const auto& R:Search->SearchResults)
        {
            FString Match,Owner,Sector;R.Session.SessionSettings.Get(PersonaSession::MatchKey,Match);R.Session.SessionSettings.Get(PersonaSession::OwnerKey,Owner);R.Session.SessionSettings.Get(PersonaSession::SectorKey,Sector);
            if((SearchPurpose==ESearchPurpose::Match && Match==DesiredMatch && Owner==Battle->Ticket.Master) || (SearchPurpose==ESearchPurpose::Home && Owner==DesiredOwner && Sector==DesiredSector)) { JoinResult(R);return; }
        }
        return;
    }
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
    if (Sessions->GetNamedSession(PersonaSession::Name))
    { if(SearchPurpose==ESearchPurpose::Match || SearchPurpose==ESearchPurpose::Home) SwitchToMenu();else SetStatus(TEXT("Leave the current session before joining another."));return; }
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
    if(!IsSteam()) if(auto S=Sessions->GetNamedSession(Name))
    {
        int32 Port=0;S->SessionSettings.Get(PersonaSession::PortKey,Port);FString Host,OldPort;
        if(Port>0 && Address.Split(TEXT(":"),&Host,&OldPort,ESearchCase::CaseSensitive,ESearchDir::FromEnd)) Address=Host+FString::Printf(TEXT(":%d"),Port);
    }
    if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController()) { bBusy=true;PC->ClientTravel(Address, TRAVEL_Absolute); }
}
void UPersonaOnline::Invited(bool Success, int32 ControllerId, TSharedPtr<const FUniqueNetId> User, const FOnlineSessionSearchResult& Result)
{
    if (Success) JoinResult(Result);
}
void UPersonaOnline::Leave()
{
    if (bBusy) return;
    AdvertiseDirectory(false,0,FString());
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (!Saves->SaveWorld()) { SetStatus(Saves->LastError); return; }
    Saves->PrepareTravel();
    if (Sessions.IsValid() && Sessions->GetNamedSession(PersonaSession::Name))
    {
        bBusy = bReturnAfterDestroy = true;
        if (!Sessions->DestroySession(PersonaSession::Name)) Destroyed(PersonaSession::Name, false);
    }
    else
    {
        SetStatus(TEXT("Session ended. Create or find a session to play together."));
        UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/Persona/Maps/L_MainMenu"));
    }
}
void UPersonaOnline::Destroyed(FName Name, bool Success)
{
    if(Name!=PersonaSession::Name) return;
    bBusy = false;
    if (bHostAfterDestroy) { bHostAfterDestroy = false; if (Success) Host(); return; }
    if (bReturnAfterDestroy)
    {
        bReturnAfterDestroy = false;
        SetStatus(TEXT("Session ended. Create or find a session to play together."));
        UGameplayStatics::OpenLevel(GetWorld(), TEXT("/Game/Persona/Maps/L_MainMenu"));
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
void UPersonaOnline::AdvertiseBattle(bool Open,int32 Players,const FString& Endpoint)
{
    AdvertiseDirectory(Open,Players,Endpoint);
    if(!Sessions.IsValid()) return;auto Named=Sessions->GetNamedSession(PersonaSession::Name);if(!Named) return;
    auto Settings=Named->SessionSettings;Settings.Set(PersonaSession::OpenKey,Open,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::PlayersKey,Players,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);Settings.Set(PersonaSession::ControlKey,Endpoint,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();Settings.bAllowJoinInProgress=Battle->Phase==EPersonaBattlePhase::Idle || Battle->Phase==EPersonaBattlePhase::Offered;
    if(auto Driver=GetWorld()->GetNetDriver()) if(auto Address=Driver->GetLocalAddr();Address.IsValid() && Address->GetPort()>0) Settings.Set(PersonaSession::PortKey,Address->GetPort(),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Sessions->UpdateSession(PersonaSession::Name,Settings,true);
}
bool UPersonaOnline::StartBattleSession(const FPersonaBattleTicket& Ticket)
{
    if(!Sessions.IsValid()) return false;auto Named=Sessions->GetNamedSession(PersonaSession::Name);if(!Named) return false;
    auto Settings=Named->SessionSettings;Settings.NumPublicConnections=8;Settings.bAllowJoinInProgress=true;Settings.bShouldAdvertise=true;
    Settings.Set(PersonaSession::PurposeKey,FString(TEXT("SectorBattle")),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(PersonaSession::MatchKey,Ticket.Match,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);Settings.Set(PersonaSession::OpenKey,false,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    // OSS Null copies settings but does not resize its open-slot counter. A
    // full four-player hub would consequently remain invisible as an arena.
    if(!IsSteam()) Named->NumOpenPublicConnections=FMath::Max(0,8-Named->RegisteredPlayers.Num());
    const bool Updated=Sessions->UpdateSession(PersonaSession::Name,Settings,true);
    AdvertiseDirectory(false,0,FString());
    if(Updated && !IsSteam())
    {
        // Refresh the LAN beacon too: it may have stopped while the hub was full.
        if(Named->SessionState==EOnlineSessionState::InProgress) Sessions->EndSession(PersonaSession::Name);
        if(Named->SessionState==EOnlineSessionState::Pending || Named->SessionState==EOnlineSessionState::Ended) Sessions->StartSession(PersonaSession::Name);
    }
    return Updated;
}
void UPersonaOnline::SwitchToMenu()
{
    AdvertiseDirectory(false,0,FString());
    if(bBusy && bReturnAfterDestroy) return;
    if(GetWorld()->GetMapName().Contains(TEXT("L_MainMenu")) && (!Sessions.IsValid() || !Sessions->GetNamedSession(PersonaSession::Name))) { bBusy=false;return; }
    bHostAfterDestroy=false;bReturnAfterDestroy=true;
    if(Sessions.IsValid() && Sessions->GetNamedSession(PersonaSession::Name))
    { bBusy=true;if(!Sessions->DestroySession(PersonaSession::Name)) Destroyed(PersonaSession::Name,false); }
    else { bBusy=false;bReturnAfterDestroy=false;UGameplayStatics::OpenLevel(GetWorld(),TEXT("/Game/Persona/Maps/L_MainMenu")); }
}
void UPersonaOnline::AdvertiseDirectory(bool Open,int32 Players,const FString& Endpoint)
{
    bDirectoryWanted=Open;if(!Sessions.IsValid()) return;
    auto Existing=Sessions->GetNamedSession(PersonaSession::DirectoryName);
    if(!Open) { if(Existing && Existing->SessionState!=EOnlineSessionState::Destroying) Sessions->DestroySession(PersonaSession::DirectoryName);return; }
    auto Save=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();if(!Save->Active) return;
    // Full game lobbies are omitted by LAN/Steam discovery. A separate,
    // non-playable directory entry keeps 4/4 hubs discoverable without adding
    // an unregistered fifth player slot to the actual game session.
    FOnlineSessionSettings S;S.NumPublicConnections=2;S.bIsLANMatch=!IsSteam();S.bShouldAdvertise=true;S.bAllowJoinInProgress=true;S.bUsesPresence=IsSteam();S.bUseLobbiesIfAvailable=IsSteam();S.bAllowInvites=false;
    S.Set(PersonaSession::ProjectKey,PersonaSession::Project,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);S.Set(PersonaSession::VersionKey,PersonaSession::Version,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(PersonaSession::PurposeKey,FString(TEXT("SectorDirectory")),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(PersonaSession::OwnerKey,GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Owner(),EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(PersonaSession::SectorKey,Save->Active->Slot,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);S.Set(PersonaSession::OpenKey,true,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    S.Set(PersonaSession::PlayersKey,Players,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);S.Set(PersonaSession::ControlKey,Endpoint,EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    if(Existing) { if(Existing->SessionState!=EOnlineSessionState::Creating && Existing->SessionState!=EOnlineSessionState::Destroying) Sessions->UpdateSession(PersonaSession::DirectoryName,S,true); }
    else Sessions->CreateSession(0,PersonaSession::DirectoryName,S);
}
