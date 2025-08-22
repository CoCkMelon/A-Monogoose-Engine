// Card.h
#pragma once
// #include "GameObject.h"
// #include "Sprite.h"
// #include "SpriteRenderer.h"
// #include "Collider2D.h"
#include "../../cpp/unitylike/Scene.h"
using namespace unitylike;

class Card : public MongooseBehaviour
{
public:
    Sprite* frontSprite;
    Sprite* backSprite;

private:
    bool faceUp = false;
    bool matched = false;
    SpriteRenderer* spriteRenderer;
    Collider2D* collider;

    void Awake() override
    {
        spriteRenderer = GetComponent<SpriteRenderer>();
        collider = GetComponent<Collider2D>();
        faceUp = false;
        spriteRenderer->sprite = backSprite;
    }

public:
    void Flip()
    {
        if (matched || !collider->enabled)
            return;

        faceUp = !faceUp;
        spriteRenderer->sprite = faceUp ? frontSprite : backSprite;
    }

    void DisableCard()
    {
        matched = true;
        collider->enabled = false;
    }

    void Update(float deltaTime) override
    {
        if (Input.GetMouseButtonDown(0)) {
            if (!matched && !faceUp)
            {
                GameManager::Instance->OnCardClicked(this);
            }
        }
    }
};
