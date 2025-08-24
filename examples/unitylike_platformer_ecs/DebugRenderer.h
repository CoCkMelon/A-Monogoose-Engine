#pragma once
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <box2d/box2d.h>

extern "C" {
#include "ame/physics.h"
#include "ame/camera.h"
#include <SDL3/SDL.h>
}

// Simple debug line renderer for physics shapes
class DebugRenderer {
public:
    struct Line {
        glm::vec2 start;
        glm::vec2 end;
        glm::vec3 color;
    };
    
private:
    std::vector<Line> lines;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint shader = 0;
    GLint u_mvp = -1;
    GLint u_color = -1;
    bool initialized = false;
    
public:
    void Initialize() {
        if (initialized) return;
        
        // Compile shaders
        const char* vs_src = R"(
#version 450 core
layout(location=0) in vec2 a_pos;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
}
)";
        
        const char* fs_src = R"(
#version 450 core
uniform vec3 u_color;
out vec4 frag;
void main() {
    frag = vec4(u_color, 1.0);
}
)";
        
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src);
        shader = LinkProgram(vs, fs);
        
        u_mvp = glGetUniformLocation(shader, "u_mvp");
        u_color = glGetUniformLocation(shader, "u_color");
        
        // Create VAO/VBO
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        
        initialized = true;
    }
    
    void Clear() {
        lines.clear();
    }
    
    void AddLine(glm::vec2 start, glm::vec2 end, glm::vec3 color = {1.0f, 0.0f, 0.0f}) {
        lines.push_back({start, end, color});
    }
    
    void AddBox(glm::vec2 center, glm::vec2 size, glm::vec3 color = {1.0f, 0.0f, 0.0f}) {
        glm::vec2 half_size = size * 0.5f;
        glm::vec2 tl = center + glm::vec2(-half_size.x, half_size.y);
        glm::vec2 tr = center + glm::vec2(half_size.x, half_size.y);
        glm::vec2 bl = center + glm::vec2(-half_size.x, -half_size.y);
        glm::vec2 br = center + glm::vec2(half_size.x, -half_size.y);
        
        // Draw box outline
        AddLine(tl, tr, color);  // top
        AddLine(tr, br, color);  // right
        AddLine(br, bl, color);  // bottom
        AddLine(bl, tl, color);  // left
    }
    
    void RenderColliders(AmePhysicsWorld* physicsWorld, const AmeCamera* camera, int screenWidth, int screenHeight) {
        if (!initialized || !physicsWorld || !physicsWorld->world) return;
        
        Clear();
        
        // Iterate through all physics bodies and draw their collision boxes
        b2World* world = (b2World*)physicsWorld->world;
        for (b2Body* body = world->GetBodyList(); body; body = body->GetNext()) {
            if (body->GetType() != b2_staticBody) continue; // Only draw static colliders for now
            
            b2Vec2 pos = body->GetPosition();
            
            // Get the fixture (assuming box shape)
            for (b2Fixture* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
                b2Shape* shape = fixture->GetShape();
                if (shape->GetType() == b2Shape::e_polygon) {
                    b2PolygonShape* poly = (b2PolygonShape*)shape;
                    
                    // For now, assume it's a box and get the half-extents
                    // This is a simple approximation
                    float width = 0.0f, height = 0.0f;
                    
                    // Get the vertices and calculate bounding box
                    int vertexCount = poly->m_count;
                    if (vertexCount == 4) { // Box shape
                        b2Vec2 v0 = poly->m_vertices[0];
                        b2Vec2 v2 = poly->m_vertices[2];
                        width = abs(v2.x - v0.x);
                        height = abs(v2.y - v0.y);
                        
                        AddBox(glm::vec2(pos.x, pos.y), glm::vec2(width, height), glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                }
            }
        }
        
        RenderLines(camera, screenWidth, screenHeight);
    }
    
    void RenderLines(const AmeCamera* camera, int screenWidth, int screenHeight) {
        if (!initialized || lines.empty()) return;
        
        // Build vertex buffer
        std::vector<glm::vec2> vertices;
        vertices.reserve(lines.size() * 2);
        
        for (const auto& line : lines) {
            vertices.push_back(line.start);
            vertices.push_back(line.end);
        }
        
        // Upload to GPU
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec2), vertices.data(), GL_DYNAMIC_DRAW);
        
        // Set up rendering state
        glUseProgram(shader);
        glBindVertexArray(vao);
        
        // Create MVP matrix from camera
        float mvp[16];
        AmeCamera cam_copy = *camera;
        ame_camera_update(&cam_copy, 0.016f);
        // Use the pixel perfect matrix function instead
        int zoom = (cam_copy.zoom > 0) ? (int)cam_copy.zoom : 1;
        ame_camera_make_pixel_perfect(cam_copy.x, cam_copy.y, screenWidth, screenHeight, zoom, mvp);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        
        // Draw all lines with the same color for now
        glUniform3f(u_color, 0.0f, 1.0f, 0.0f); // Green color
        
        // Enable line rendering
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, (GLsizei)vertices.size());
        glLineWidth(1.0f);
    }
    
    void Shutdown() {
        if (!initialized) return;
        
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (shader) glDeleteProgram(shader);
        
        vao = vbo = shader = 0;
        initialized = false;
    }
    
private:
    GLuint CompileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, NULL);
        glCompileShader(shader);
        
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            SDL_Log("Shader compilation failed: %s", infoLog);
        }
        
        return shader;
    }
    
    GLuint LinkProgram(GLuint vs, GLuint fs) {
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        
        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, NULL, infoLog);
            SDL_Log("Program linking failed: %s", infoLog);
        }
        
        glDeleteShader(vs);
        glDeleteShader(fs);
        
        return program;
    }
};
