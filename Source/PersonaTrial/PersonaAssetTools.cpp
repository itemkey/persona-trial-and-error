#include "PersonaAssetTools.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"

void UPersonaAssetTools::EnsureAttachmentSockets(USkeleton* Skeleton)
{
#if WITH_EDITOR
    if (!Skeleton) return;
    const TPair<FName,FName> Sockets[] = {
        {TEXT("Grip_R"),TEXT("hand_r")}, {TEXT("Grip_L"),TEXT("hand_l")}, {TEXT("Voice_Head"),TEXT("head")}
    };
    for (const auto& Entry : Sockets)
    {
        if (Skeleton->FindSocket(Entry.Key)) continue;
        Skeleton->Modify();
        auto Socket=NewObject<USkeletalMeshSocket>(Skeleton,NAME_None,RF_Transactional);
        Socket->SocketName=Entry.Key; Socket->BoneName=Entry.Value;
        Skeleton->Sockets.Add(Socket);
        Skeleton->MarkPackageDirty();
    }
#endif
}
bool UPersonaAssetTools::BuildNavigation(UWorld* World)
{
#if WITH_EDITOR
    auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    FlushAsyncLoading();
    // Commandlets do not advance the editor tick which normally releases this
    // lock after all map assets finish loading.
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
        UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->InitializeLevelCollisions();
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Nav->OnNavigationBoundsUpdated(*It);
    Nav->Build();
    auto Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
    if (Data) { Data->EnsureBuildCompletion(); Data->MarkPackageDirty(); }
    FNavLocation Point;
    const bool Valid=Nav->ProjectPointToNavigation(FVector(1000,0,100),Point,FVector(1500,1500,1000));
    UE_LOG(LogTemp,Display,TEXT("PERSONA NAVIGATION: world=%s projected=%d"),*GetNameSafe(World),Valid);
    return Valid;
#else
    return false;
#endif
}
