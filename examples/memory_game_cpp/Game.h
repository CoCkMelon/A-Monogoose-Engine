#include <vector>
#include <random>
#include <algorithm>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

extern "C" {
#include "ame/scene2d.h"
}

#include "unitylike/Scene.h"

using namespace unitylike;

// All gameplay contained in this MongooseBehaviour script (Unity-like)
class MemoryGameController : public MongooseBehaviour {
public:
    void Awake() override {
        init_grid(6, 4, 80.0f, 100.0f, 10.0f);
    }
    void Update(float dt) override {
        (void)dt;
        // Unity-like input via asyncinput-backed facade
        const bool click = Input.GetMouseButtonDown(0);
        if (click && !completed_) {
            glm::vec2 p = Input.mousePosition();
            for (size_t i=0;i<cards_.size();++i) {
                Card& c = cards_[i];
                if (!c.matched && !c.revealed && point_in(p, c)) {
                    c.revealed = true;
                    if (first_ < 0) first_ = (int)i;
                    else if (second_ < 0) { second_ = (int)i; flip_timer_ = 0.7f; }
                    break;
                }
            }
        }
        if (second_ >= 0) {
            flip_timer_ -= dt;
            if (flip_timer_ <= 0.0f) {
                Card& a = cards_[(size_t)first_];
                Card& b = cards_[(size_t)second_];
                if (a.id == b.id) { a.matched = b.matched = true; }
                a.revealed = b.revealed = false;
                first_ = second_ = -1; flip_timer_ = 0.0f;
            }
        }
        completed_ = std::all_of(cards_.begin(), cards_.end(), [](const Card& c){return c.matched;});
    }
    void LateUpdate() override {}

    void Draw(AmeScene2DBatch* batch) {
        // Background
        draw_rect(batch, 0,0,800,600, 0.08f,0.08f,0.1f, 1.0f);
        // Cards
        for (const Card& c : cards_) {
            float r=0.2f,g=0.2f,b=0.25f;
            if (c.revealed || c.matched) { float h=(c.id%12)/12.0f; hsv_to_rgb(h,0.6f,0.95f,r,g,b); }
            draw_rect(batch, c.pos.x, c.pos.y, c.size.x, c.size.y, r,g,b,1.0f);
        }
    }

private:
    struct Card {
        int id = 0;
        bool revealed = false;
        bool matched = false;
        glm::vec2 pos{0.0f, 0.0f};
        glm::vec2 size{80.0f, 100.0f};
    };
    std::vector<Card> cards_;
    int first_ = -1;
    int second_ = -1;
    float flip_timer_ = 0.0f;
    bool completed_ = false;

    void init_grid(int cols, int rows, float card_w, float card_h, float pad) {
        int total = cols*rows;
        std::vector<int> ids(total);
        for (int i=0;i<total/2;i++){ ids[i*2]=i; ids[i*2+1]=i; }
        std::mt19937 rng{std::random_device{}()};
        std::shuffle(ids.begin(), ids.end(), rng);
        cards_.resize(total);
        for (int y=0;y<rows;y++){
            for (int x=0;x<cols;x++){
                int idx = y*cols + x;
                Card c; c.id=ids[idx]; c.size={card_w, card_h};
                c.pos = { 20.0f + x*(card_w+pad), 20.0f + y*(card_h+pad) };
                cards_[idx]=c;
            }
        }
    }
    static bool point_in(const glm::vec2& p, const Card& c) {
        return p.x>=c.pos.x && p.x<=c.pos.x+c.size.x && p.y>=c.pos.y && p.y<=c.pos.y+c.size.y;
    }
    static void draw_rect(AmeScene2DBatch* b, float x, float y, float w, float h, float r, float g, float bl, float a) {
        const float x0=x,y0=y,x1=x+w,y1=y+h;
        ame_scene2d_batch_push(b, 0, x0,y0, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x1,y0, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x1,y1, r,g,bl,a, 0,0);
        ame_scene2d_batch_push(b, 0, x0,y1, r,g,bl,a, 0,0);
    }
    static void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
        int i = int(h*6.0f);
        float f = h*6.0f - i;
        float p = v*(1.0f - s);
        float q = v*(1.0f - f*s);
        float t = v*(1.0f - (1.0f - f)*s);
        switch(i%6){
            case 0: r=v,g=t,b=p; break;
            case 1: r=q,g=v,b=p; break;
            case 2: r=p,g=v,b=t; break;
            case 3: r=p,g=q,b=v; break;
            case 4: r=t,g=p,b=v; break;
            case 5: r=v,g=p,b=q; break;
        }
    }
};
