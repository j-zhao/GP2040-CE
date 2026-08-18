#ifndef _GPWIDGET_H_
#define _GPWIDGET_H_

#include "GPGFX.h"
#include "GPGFX_UI.h"

typedef struct {
    double scaleX;
    double scaleY;
    uint16_t offsetX;
    uint16_t offsetY;
} GPViewportTransform;

class GPWidget : public GPGFX_UI {
    public:
        GPWidget() {}
        GPWidget(GPGFX* renderer) { setRenderer(renderer); }
        virtual ~GPWidget(){}
        virtual void draw() {}
        virtual int8_t update() { return 0; }

        void setPosition(uint16_t x, uint16_t y) { this->x = x; this->y = y; }

        void setStrokeColor(uint16_t color) { this->strokeColor = color; }
        void setFillColor(uint16_t color) { this->fillColor = color; }

        void setID(uint16_t id) { this->_ID = id; }
        uint16_t getID() { return this->_ID; }
        
        void setPriority(uint16_t priority) { this->_priority = priority; }
        uint16_t getPriority() { return this->_priority; }

        void setViewport(uint16_t top, uint16_t left, uint16_t bottom, uint16_t right) { this->_viewport.top = top; this->_viewport.left = left; this->_viewport.bottom = bottom; this->_viewport.right = right; }
        void setViewport(GPViewport viewport) { this->_viewport = viewport; }
        GPViewport getViewport() { return this->_viewport; }

        double getScaleX() { return ((double)(this->getViewport().right - this->getViewport().left) / (double)(getRenderer()->getDriver()->getMetrics()->width)); }
        double getScaleY() { return ((double)(this->getViewport().bottom - this->getViewport().top) / (double)(getRenderer()->getDriver()->getMetrics()->height)); }

        // Every element draws through the same viewport transform, so they stay
        // aligned with each other. The scales are made proportionate when one
        // axis is unscaled, then the scaled screen is centered in the viewport.
        // Both offsets are 0 while the viewport fills the screen.
        GPViewportTransform getViewportTransform() {
            double scaleX = this->getScaleX();
            double scaleY = this->getScaleY();

            if ((scaleX > 0.0f) & ((scaleY == 0.0f) || (scaleY == 1.0f))) {
                scaleY = scaleX;
            } else if (((scaleX == 0.0f) || (scaleX == 1.0f)) & (scaleY > 0.0f)) {
                scaleX = scaleY;
            }

            double centerX = ((double)(this->getViewport().right - this->getViewport().left) - ((double)getRenderer()->getDriver()->getMetrics()->width * scaleX)) / 2.0f;
            double centerY = ((double)(this->getViewport().bottom - this->getViewport().top) - ((double)getRenderer()->getDriver()->getMetrics()->height * scaleY)) / 2.0f;

            return {
                scaleX,
                scaleY,
                (centerX > 0.0f) ? (uint16_t)centerX : (uint16_t)0,
                (centerY > 0.0f) ? (uint16_t)centerY : (uint16_t)0
            };
        }

        void setVisibility(bool visible) { this->_visibility = visible; }
        bool getVisibility() { return this->_visibility; }
    protected:
        uint16_t x = 0;
        uint16_t y = 0;

        uint16_t strokeColor = 0;
        uint16_t fillColor = 0;
        uint16_t _ID = 0;
        uint16_t _priority = 0;
        bool _visibility = true;

        GPViewport _viewport;
};

#endif