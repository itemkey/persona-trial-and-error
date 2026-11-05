#pragma once
#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

// One input route, ahead of text widgets, for the physical key under Escape.
class FPersonaInput final : public IInputProcessor
{
public:
    virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override;
    virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override;
private:
    bool bAdminDown=false;
};
