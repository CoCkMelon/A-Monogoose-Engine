// GameManager.h
#pragma once
#include <vector>
// #include "Coroutine.h"
#include "Card.h"
#include <flecs.h>

#include "../../cpp/unitylike/Scene.h"

using namespace unitylike;

class GameManager : public MonoBehaviour
{
public:
    static GameManager* Instance;

private:
    Card* firstCard = nullptr;
    Card* secondCard = nullptr;
    int matchesFound = 0;
    int totalPairs = 0;
    bool canFlip = true;

    void Awake() override
    {
        if (!Instance)
            Instance = this;
        else
            delete this;
    }

public:
    void Initialize(int totalPairs)
    {
        this->totalPairs = totalPairs;
        matchesFound = 0;
        canFlip = true;
    }

    void OnCardClicked(Card* card)
    {
        if (!canFlip || card == firstCard || card == secondCard)
            return;

        card->Flip();

        if (!firstCard)
        {
            firstCard = card;
        }
        else if (!secondCard)
        {
            secondCard = card;
            CheckMatch();
        }
    }

private:
    void CheckMatch()
    {
        canFlip = false;

        if (firstCard->frontSprite == secondCard->frontSprite)
        {
            firstCard->DisableCard();
            secondCard->DisableCard();
            matchesFound++;

            if (matchesFound == totalPairs)
            {
                Debug::Log("Congratulations! You've matched all pairs!");
            }

            ResetCards();
            canFlip = true;
        }
        else
        {
            flecs_time(FlipBackCardsAfterDelay());
        }
    }

    Coroutine FlipBackCardsAfterDelay()
    {
        co_yield WaitForSeconds(1.0f);
        firstCard->Flip();
        secondCard->Flip();
        ResetCards();
        canFlip = true;
    }

    void ResetCards()
    {
        firstCard = nullptr;
        secondCard = nullptr;
    }

    void RestartGame()
    {
        matchesFound = 0;
        firstCard = nullptr;
        secondCard = nullptr;
        canFlip = true;
        BoardManager::Instance->RestartGame();
    }
};
