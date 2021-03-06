#pragma once

#include <chrono>

#include <Maths.h>
#include <Singleton.hpp>
#include "./Frustum/Frustum.h"
#include "../Platform/Common/Mouse/Mouse.h"
#include "../GlobalData/GlobalData.h"

#if defined(DISPLAY_LOGS)
#include <iostream>
#endif

namespace Engine
{
    class Camera : public Singleton<Camera>, public IMouseListener
    {
        friend Singleton<Camera>;

        public:

            struct CAMERA_UBO {
                Maths::Matrix4x4 projection;
                Maths::Matrix4x4 view;
                Maths::Vector3 position;

                bool operator!=(CAMERA_UBO other) { return this->projection != other.projection || this->view != other.view || this->position != other.position; }
            };

            // static Camera*& CreateInstance(InstancedBuffer& buffer) { if(Camera::instance == nullptr) Camera::instance = new Camera(buffer); return Camera::instance; }

            inline CAMERA_UBO& GetUniformBuffer() { return this->camera; }  // R�cup�re la transformation finale
            void SetPosition(Maths::Vector3 const& position);               // Modifie la position
            void Rotate(Maths::Vector3 const& rotation);                    // Rotation de la camera
            inline Frustum const& GetFrustum() { return this->frustum; }    // R�cup�re le frustum
            void Update(uint8_t frame_index);                                                  // Mise � jour de la cam�ra
            inline Maths::Vector3 GetFrontVector() const { return {-this->camera.view[2], -this->camera.view[6], -this->camera.view[10]}; }
            inline Maths::Vector3 GetRightVector() const { return {this->camera.view[0], this->camera.view[4], this->camera.view[8]}; }    
            inline Maths::Vector3 GetUpVector() const { return {-this->camera.view[1], -this->camera.view[5], -this->camera.view[9]}; }
            inline float GetNearClipDistance() const { return this->near_clip_distance; }
            inline float GetFarClipDistance() const { return this->far_clip_distance; }
            inline void Freeze() { this->frozen = true; }
            inline void UnFreeze() { this->frozen = false; }
            inline bool IsRtsMode() { return this->rts_mode; }
            inline void SetFpsMode() { this->rts_mode = false; }
            inline void SetRtsMode() { this->rts_mode = true; }

            ///////////////////////////
            ///    IMouseListener    //
            ///////////////////////////

            inline virtual void MouseMove(unsigned int x, unsigned int y) { this->rts_mode ? this->RtsMove(x, y) : this->FpsMove(x, y); }
            virtual void MouseButtonDown(MOUSE_BUTTON button);
            virtual void MouseButtonUp(MOUSE_BUTTON button);
            virtual void MouseWheelUp();
            virtual void MouseWheelDown();

        private:

            // static Camera* instance;
            float near_clip_distance;
            float far_clip_distance;

            bool rts_mode;
            bool frozen;
            bool rts_is_scrolling[2] = {false, false};
            float rts_scroll_speed = 2.5f / 1000.0f;
            float rts_scroll_initial_position[2];
            std::chrono::system_clock::time_point scroll_start[2];
            std::vector<CAMERA_UBO> last_ubo;

            CAMERA_UBO camera;                      // Uniform buffer de la cam�ra
            Point<uint32_t> mouse_origin;           // Position de la souris au moment du click

            float moving_vertical_angle;            // Rotation verticale en mouvement
            float moving_horizontal_angle;          // Rotation horizontale en mouvement
            float vertical_angle;                   // Rotation verticale
            float horizontal_angle;                 // Rotation horizontale

            Maths::Matrix4x4 translation;           // Matrice de translation
            Maths::Vector3 position;                // Position de la cam�ra
            Maths::Matrix4x4 rotation;              // Matrice de rotation

            Frustum frustum;                        // Frustum de la cam�ra

            Camera();
            virtual ~Camera();
            void RtsMove(unsigned int x, unsigned int y);
            void FpsMove(unsigned int x, unsigned int y);
            void RtsScroll();
    };
}