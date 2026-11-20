#include "ColdLootGameMode.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor PanelColor(0.022f, 0.044f, 0.070f, 1.0f);
const FLinearColor SelectedColor(0.16f, 0.56f, 0.68f, 1.0f);
const FLinearColor OptionColor(0.20f, 0.32f, 0.40f, 1.0f);

void ApplyDisplayMode(UGameUserSettings& Settings, EWindowMode::Type Mode)
{
    if (Mode == EWindowMode::Windowed)
    {
        const FIntPoint Desktop = Settings.GetDesktopResolution();
        const FIntPoint Current = Settings.GetScreenResolution();
        if (Desktop.X > 0 && Desktop.Y > 0 && (Current.X >= Desktop.X || Current.Y >= Desktop.Y))
        {
            Settings.SetScreenResolution(FIntPoint(
                FMath::Min(1280, Desktop.X * 4 / 5), FMath::Min(720, Desktop.Y * 4 / 5)));
        }
    }
    Settings.SetFullscreenMode(Mode);
    Settings.ApplySettings(false);
    Settings.ConfirmVideoMode();
    Settings.SaveSettings();
}
}

void AColdLootHUD::ToggleGraphicsMenu()
{
    if (IsGraphicsMenuOpen())
    {
        CloseGraphicsMenu();
        return;
    }

    UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    UGameUserSettings* Settings = GEngine->GetGameUserSettings();
    if (!Viewport || !Settings || !PlayerOwner || !PlayerOwner->IsLocalController())
    {
        return;
    }

    const auto WindowChoices = SNew(SHorizontalBox);
    const EWindowMode::Type Modes[] = {EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen};
    const TCHAR* ModeLabels[] = {TEXT("Windowed"), TEXT("Borderless"), TEXT("Fullscreen")};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Modes); ++Index)
    {
        const EWindowMode::Type Mode = Modes[Index];
        WindowChoices->AddSlot().FillWidth(1.0f).Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .Tag(FName(ModeLabels[Index]))
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(8, 10))
            .Text(FText::FromString(ModeLabels[Index]))
            .ButtonColorAndOpacity_Lambda([Settings, Mode]
            {
                return Settings->GetFullscreenMode() == Mode ? SelectedColor : OptionColor;
            })
            .OnClicked_Lambda([Settings, Mode]
            {
                ApplyDisplayMode(*Settings, Mode);
                return FReply::Handled();
            })
        ];
    }

    const auto QualityChoices = SNew(SHorizontalBox);
    const TCHAR* QualityLabels[] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic")};
    for (int32 Level = 0; Level < UE_ARRAY_COUNT(QualityLabels); ++Level)
    {
        QualityChoices->AddSlot().FillWidth(1.0f).Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .Tag(FName(QualityLabels[Level]))
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(8, 10))
            .Text(FText::FromString(QualityLabels[Level]))
            .ButtonColorAndOpacity_Lambda([Settings, Level]
            {
                return Settings->GetOverallScalabilityLevel() == Level ? SelectedColor : OptionColor;
            })
            .OnClicked_Lambda([Settings, Level]
            {
                Settings->SetOverallScalabilityLevel(Level);
                Settings->ApplyNonResolutionSettings();
                Settings->SaveSettings();
                return FReply::Handled();
            })
        ];
    }

    GraphicsMenu = SNew(SBorder)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
        [
            SNew(SBox).WidthOverride(424.0f)
            [
                SNew(SBorder)
                .BorderBackgroundColor(PanelColor)
                .Padding(24.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Graphics")))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 22))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("Display mode")))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
                    [WindowChoices]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("Quality preset")))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
                    [QualityChoices]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                    [
                        SNew(SButton)
                        .ContentPadding(FMargin(14, 8))
                        .Text(FText::FromString(TEXT("Resume  ·  F2")))
                        .OnClicked_Lambda([this]
                        {
                            CloseGraphicsMenu();
                            return FReply::Handled();
                        })
                    ]
                ]
            ]
        ];
    Viewport->AddViewportWidgetContent(GraphicsMenu.ToSharedRef(), 100);
    bCursorBeforeGraphics = PlayerOwner->bShowMouseCursor;
    PlayerOwner->bShowMouseCursor = true;
    PlayerOwner->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false)
        .SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
    bPausedForGraphics = !GetWorld()->IsPaused() && PlayerOwner->SetPause(true);
}

void AColdLootHUD::CloseGraphicsMenu()
{
    if (!GraphicsMenu)
    {
        return;
    }
    if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
    {
        Viewport->RemoveViewportWidgetContent(GraphicsMenu.ToSharedRef());
    }
    GraphicsMenu.Reset();
    if (IsValid(PlayerOwner))
    {
        PlayerOwner->bShowMouseCursor = bCursorBeforeGraphics;
        PlayerOwner->SetInputMode(FInputModeGameOnly());
        if (bPausedForGraphics)
        {
            PlayerOwner->SetPause(false);
        }
    }
    bPausedForGraphics = false;
}

void AColdLootHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CloseGraphicsMenu();
    Super::EndPlay(EndPlayReason);
}
