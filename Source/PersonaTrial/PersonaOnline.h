#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "PersonaOnline.generated.h"

UCLASS()
class PERSONATRIAL_API UPersonaOnline : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable) void Host();
    UFUNCTION(BlueprintCallable) void Find();
    UFUNCTION(BlueprintCallable) void Join(int32 Index);
    UFUNCTION(BlueprintCallable) void Leave();
    UFUNCTION(BlueprintCallable) void InviteFriends();
    UPROPERTY(BlueprintReadOnly) TArray<FString> Results;
    UPROPERTY(BlueprintReadOnly) bool bBusy = false;
    void ClearSessionAfterFailure();
    bool IsSteam() const;
private:
    IOnlineSessionPtr Sessions;
    TSharedPtr<FOnlineSessionSearch> Search;
    FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, InviteHandle;
    bool bHostAfterDestroy = false;
    bool bReturnAfterDestroy = false;
    void SetStatus(const FString& Text);
    void Created(FName Name, bool Success);
    void Found(bool Success);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Destroyed(FName Name, bool Success);
    void Invited(bool Success, int32 ControllerId, TSharedPtr<const FUniqueNetId> User, const FOnlineSessionSearchResult& Result);
    void JoinResult(const FOnlineSessionSearchResult& Result);
};
