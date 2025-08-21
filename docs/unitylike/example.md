# Example: PlayerController (Facade) – Concept

This mirrors the platformer behaviour using the façade. It’s documentation‑only to guide the initial implementation.

// C++ sketch
class PlayerController : public MongooseBehaviour {
public:
  void Start() override {
    body_ = &gameObject().GetComponent<Rigidbody2D>();
  }
  
  void Update(float dt) override {
    int dir = (Input.GetKey(KeyCode::D) ? 1 : 0) - (Input.GetKey(KeyCode::A) ? 1 : 0);
    glm::vec2 v = body_->velocity();
    v.x = move_speed * (float)dir;
    body_->velocity(v);
  }
  
  void FixedUpdate(float fdt) override {
    bool jump_edge = Input.GetKeyDown(KeyCode::Space) || Input.GetKeyDown(KeyCode::W) || Input.GetKeyDown(KeyCode::Up);
    if (jump_edge && (grounded_ || coyote_timer_ > 0.0f)) {
      glm::vec2 v = body_->velocity();
      v.y = -jump_speed; // up is negative in current examples
      body_->velocity(v);
      grounded_ = false;
      coyote_timer_ = 0.0f;
    }
  }
private:
  Rigidbody2D* body_ = nullptr;
  float move_speed = 50.0f;
  float jump_speed = 100.0f;
  bool grounded_ = false; // would come from a Grounded component
  float coyote_timer_ = 0.0f;
};

Setup outline
- Scene scene;
- auto player = scene.Create("Player");
- player.AddComponent<Transform>().position(glm::vec3(100,100,0));
- player.AddComponent<Rigidbody2D>(/* box size, dynamic */);
- player.AddComponent<Grounded>();
- player.AddScript<PlayerController>();
- scene.StepFixed(0.001f) in logic thread loop; render thread reads atomics for camera and sprite.
