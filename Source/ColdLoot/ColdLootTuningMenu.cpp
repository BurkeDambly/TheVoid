#include "ColdLootGameMode.h"

#include "ColdLootTuning.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "PenguinPawn.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor Panel(0.023f, 0.046f, 0.073f, 1.0f);
const FLinearColor Accent(0.37f, 0.87f, 1.0f);
const FLinearColor Secondary(0.66f, 0.78f, 0.85f);
const FLinearColor Controls(0.20f, 0.32f, 0.40f);

class SColdLootTuningMenu : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SColdLootTuningMenu) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AColdLootGameState>, GameState)
        SLATE_EVENT(FSimpleDelegate, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        State = Args._GameState;
        OnClose = Args._OnClose;
        bCanEdit = State.IsValid() && State->HasAuthority();
        ChildSlot
        [
            SNew(SBorder)
            .Padding(18.0f)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.68f))
            [
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(SBox).WidthOverride(660.0f).HeightOverride(570.0f)
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(Panel)
                        .Padding(FMargin(24.0f, 19.0f))
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Playtest tuning")))
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24))
                                .ColorAndOpacity(Accent)
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(bCanEdit
                                    ? TEXT("Changes apply live. A value of 1.00x uses this mode's default.")
                                    : TEXT("These are the live session values. The host controls them.")))
                                .ColorAndOpacity(Secondary)
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                            [
                                SNew(SSearchBox)
                                .HintText(FText::FromString(TEXT("Find a setting")))
                                .OnTextChanged(this, &SColdLootTuningMenu::FilterSettings)
                            ]
                            + SVerticalBox::Slot().FillHeight(1.0f)
                            [
                                SAssignNew(SettingsList, SScrollBox)
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                                [
                                    SNew(STextBlock).ColorAndOpacity(Secondary)
                                    .Text_Lambda([this] { return Status; })
                                ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                                [
                                    SNew(SButton)
                                    .Visibility(bCanEdit ? EVisibility::Visible : EVisibility::Collapsed)
                                    .ContentPadding(FMargin(12, 8))
                                    .Text(FText::FromString(TEXT("Reset session")))
                                    .OnClicked(this, &SColdLootTuningMenu::Reset)
                                ]
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                                [
                                    SNew(SButton)
                                    .Visibility(bCanEdit ? EVisibility::Visible : EVisibility::Collapsed)
                                    .ContentPadding(FMargin(12, 8))
                                    .Text(FText::FromString(TEXT("Save preset")))
                                    .OnClicked(this, &SColdLootTuningMenu::Save)
                                ]
                                + SHorizontalBox::Slot().AutoWidth()
                                [
                                    SNew(SButton)
                                    .ButtonColorAndOpacity(Controls)
                                    .ContentPadding(FMargin(14, 8))
                                    .Text(FText::FromString(TEXT("Resume  ·  F3")))
                                    .OnClicked(this, &SColdLootTuningMenu::Dismiss)
                                ]
                            ]
                        ]
                    ]
                ]
            ]
        ];
        FilterSettings(FText::GetEmpty());
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        return Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::F3 ? Dismiss() : FReply::Unhandled();
    }

private:
    void FilterSettings(const FText& QueryText)
    {
        const FString Query = QueryText.ToString().TrimStartAndEnd();
        const TCHAR* LastGroup = nullptr;
        SettingsList->ClearChildren();
        for (const FColdLootTuningSetting& Setting : FColdLootTuning::Settings())
        {
            if (!Query.IsEmpty() && !FString(Setting.Label).Contains(Query)
                && !FString(Setting.Group).Contains(Query) && !FString(Setting.Description).Contains(Query))
            {
                continue;
            }
            if (!LastGroup || FCString::Strcmp(LastGroup, Setting.Group) != 0)
            {
                SettingsList->AddSlot().Padding(0, 9, 14, 5)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Setting.Group))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
                    .ColorAndOpacity(Accent)
                ];
                LastGroup = Setting.Group;
            }
            SettingsList->AddSlot().Padding(0, 1, 14, 1)[MakeRow(Setting)];
        }
        if (!LastGroup)
        {
            SettingsList->AddSlot().Padding(0, 12)
            [SNew(STextBlock).Text(FText::FromString(TEXT("No matching settings."))).ColorAndOpacity(Secondary)];
        }
        SettingsList->ScrollToStart();
    }

    TSharedRef<SWidget> MakeRow(const FColdLootTuningSetting& Setting)
    {
        const EColdLootTuning Id = Setting.Id;
        const bool bCount = Id == EColdLootTuning::SkipCount;
        return SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.039f, 0.072f, 0.106f))
            .Padding(FMargin(10, 5))
            .ToolTipText(FText::FromString(Setting.Description))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(FText::FromString(Setting.Label))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBox).WidthOverride(92.0f)
                    [
                        SNew(SNumericEntryBox<float>)
                        .IsEnabled(bCanEdit)
                        .AllowSpin(bCanEdit)
                        .MinValue(Setting.Minimum)
                        .MaxValue(Setting.Maximum)
                        .MinSliderValue(Setting.Minimum)
                        .MaxSliderValue(Setting.Maximum)
                        .MinFractionalDigits(bCount ? 0 : 2)
                        .MaxFractionalDigits(bCount ? 0 : 2)
                        .Delta(bCount ? 1.0f : 0.05f)
                        .Value_Lambda([this, Id]() -> TOptional<float>
                        {
                            return State.IsValid() ? TOptional<float>(State->GetTuning().Get(Id)) : TOptional<float>();
                        })
                        .OnValueCommitted_Lambda([this, Id](float Value, ETextCommit::Type Commit)
                        {
                            if (Commit != ETextCommit::OnCleared)
                            {
                                SetValue(Id, Value);
                            }
                        })
                        .OnValueChanged_Lambda([this, Id](float Value) { SetValue(Id, Value); })
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(14.0f)[SNew(STextBlock).Text(FText::FromString(bCount ? TEXT("") : TEXT("×")))]]
            ];
    }

    void SetValue(EColdLootTuning Id, float Value)
    {
        if (State.IsValid() && State->SetTuning(Id, Value))
        {
            Status = FText::GetEmpty();
        }
    }

    FReply Reset()
    {
        if (State.IsValid())
        {
            State->ResetTuning();
            Status = FText::FromString(TEXT("Session reset"));
        }
        return FReply::Handled();
    }

    FReply Save()
    {
        Status = FText::FromString(State.IsValid() && State->SaveTuningPreset()
            ? TEXT("Preset saved") : TEXT("Preset could not be saved"));
        return FReply::Handled();
    }

    FReply Dismiss()
    {
        OnClose.ExecuteIfBound();
        return FReply::Handled();
    }

    TWeakObjectPtr<AColdLootGameState> State;
    TSharedPtr<SScrollBox> SettingsList;
    FSimpleDelegate OnClose;
    FText Status;
    bool bCanEdit = false;
};
}

void AColdLootHUD::ToggleTuningMenu()
{
    if (IsTuningMenuOpen())
    {
        CloseTuningMenu();
        return;
    }
    UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    AColdLootGameState* State = GetWorld()->GetGameState<AColdLootGameState>();
    if (!Viewport || !State || !PlayerOwner || !PlayerOwner->IsLocalController())
    {
        return;
    }

    CloseGraphicsMenu();
    if (APenguinPawn* Penguin = Cast<APenguinPawn>(GetOwningPawn()))
    {
        Penguin->MoveForward(0.0f);
        Penguin->MoveRight(0.0f);
        Penguin->StopSprint();
        Penguin->StopSlideBrake();
        Penguin->StopAim();
        Penguin->StopFire();
        Penguin->ShotRequestTime = 0.0f;
    }
    TuningMenu = SNew(SColdLootTuningMenu)
        .GameState(TWeakObjectPtr<AColdLootGameState>(State))
        .OnClose(FSimpleDelegate::CreateUObject(this, &AColdLootHUD::CloseTuningMenu));
    Viewport->AddViewportWidgetContent(TuningMenu.ToSharedRef(), 100);
    bCursorBeforeTuning = PlayerOwner->bShowMouseCursor;
    PlayerOwner->bShowMouseCursor = true;
    PlayerOwner->FlushPressedKeys();
    PlayerOwner->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(TuningMenu)
        .SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
}

void AColdLootHUD::CloseTuningMenu()
{
    if (!TuningMenu)
    {
        return;
    }
    if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
    {
        Viewport->RemoveViewportWidgetContent(TuningMenu.ToSharedRef());
    }
    TuningMenu.Reset();
    if (IsValid(PlayerOwner))
    {
        PlayerOwner->bShowMouseCursor = bCursorBeforeTuning;
        PlayerOwner->FlushPressedKeys();
        PlayerOwner->SetInputMode(FInputModeGameOnly());
    }
}
